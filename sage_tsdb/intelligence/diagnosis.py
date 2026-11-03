"""Direct reuse of SRTFD/models/network.py with training/deployment adapters."""

import copy
from dataclasses import asdict, dataclass
from pathlib import Path
from typing import List, Optional

import torch
from torch import nn
from torch.nn import functional as F

from .checkpoint import load_checkpoint, save_checkpoint
from .upstream import srtfd_class


@dataclass
class DiagnosisConfig:
    input_dim: int = 52
    num_classes: int = 22
    upstream_root: Optional[str] = None
    inference_batch_size: int = 100
    memory_per_class: int = 24
    batch_size: int = 100
    replay_batch: int = 64
    online_steps: int = 8
    learning_rate: float = 0.0001
    pseudo_threshold: float = 0.97
    pseudo_weight: float = 0.3
    distillation_weight: float = 0.2
    max_update_samples: int = 4096
    seed: int = 2026

    def __post_init__(self):
        if (
            min(
                self.input_dim,
                self.num_classes,
                self.memory_per_class,
                self.batch_size,
                self.replay_batch,
                self.online_steps,
                self.inference_batch_size,
                self.max_update_samples,
            )
            <= 0
        ):
            raise ValueError("network dimensions, memory and budgets must be positive")
        if not 0 < self.learning_rate or not 0.5 <= self.pseudo_threshold <= 1:
            raise ValueError("invalid optimization or pseudo-label threshold")
        if min(self.pseudo_weight, self.distillation_weight) < 0:
            raise ValueError("invalid loss weight")


class SRTFDAdapter(nn.Module):
    """Pre/postprocessing only. All learned layers belong to upstream GCFAggMVC.

    Upstream forward receives its original 2-D [batch, feature] tensor. Its
    batch-context behavior is preserved, with an explicit, reproducible batch
    size shared by eager and exported inference.
    """

    def __init__(self, config):
        super().__init__()
        network_class, _ = srtfd_class(config.upstream_root)
        self.network = network_class(
            config.input_dim, config.input_dim, config.num_classes, torch.device("cpu")
        )
        self.inference_batch_size = config.inference_batch_size
        self.register_buffer("normal_mean", torch.zeros(config.input_dim))
        self.register_buffer("normal_scale", torch.ones(config.input_dim))
        self.register_buffer("seen_classes", torch.zeros(config.num_classes, dtype=torch.bool))

    def normalized(self, x):
        return ((x.float() - self.normal_mean) / self.normal_scale).clamp(-20, 20)

    def forward(self, x):
        outputs = torch.jit.annotate(List[torch.Tensor], [])
        for begin in range(0, x.size(0), self.inference_batch_size):
            batch = self.normalized(x[begin : begin + self.inference_batch_size])
            outputs.append(self.network(batch).masked_fill(~self.seen_classes, -1e9))
        return torch.cat(outputs, dim=0)

    def embeddings(self, x):
        return self.network.encoders(self.normalized(x))


class SRTFDLearner:
    """CPU continual neural classifier with predict-before-update semantics.

    Labels use -1 for missing. Pseudo labels train transiently but are never
    inserted into trusted replay. No evaluation method fits normalization,
    updates weights, adds labels to the active set, or changes the replay set.
    """

    FORMAT = 2

    def __init__(self, config=None):
        self.config = config or DiagnosisConfig()
        with torch.random.fork_rng():
            torch.manual_seed(self.config.seed)
            self.model = SRTFDAdapter(self.config)
        _, self.upstream = srtfd_class(self.config.upstream_root)
        self.optimizer = torch.optim.Adam(
            self.model.parameters(), lr=self.config.learning_rate, weight_decay=1e-5
        )
        self.generator = torch.Generator().manual_seed(self.config.seed)
        self.memory = {}  # true labeled raw features only, keyed by fault ID
        self.normalizer_fitted = False
        self.updates = 0
        self.gradient_steps = 0

    def _features(self, x):
        data = torch.as_tensor(x, dtype=torch.float32).detach().cpu()
        if data.ndim != 2 or data.shape[1] != self.config.input_dim:
            raise ValueError("expected [samples, input_dim] sensor matrix")
        if not torch.isfinite(data).all():
            raise ValueError("sensor features must be finite")
        return data

    def calibrate(self, training_features):
        if self.normalizer_fitted or self.updates:
            raise ValueError("normalization is frozen after initial training calibration")
        x = self._features(training_features)
        if len(x) < 2:
            raise ValueError("normalization needs at least two training samples")
        self.model.normal_mean.copy_(x.mean(0))
        self.model.normal_scale.copy_(x.std(0, unbiased=False).clamp_min(1e-4))
        self.normalizer_fitted = True

    def predict(self, features):
        x = self._features(features)
        if not self.normalizer_fitted:
            raise ValueError("calibrate on training data before diagnosis")
        was_training = self.model.training
        self.model.eval()
        with torch.no_grad():
            probabilities = self.model(x).softmax(-1)
            confidence, prediction = probabilities.max(-1)
            if not self.model.seen_classes.any():
                prediction.fill_(-1)
                confidence.zero_()
            score = 1 - probabilities[:, 0]
        self.model.train(was_training)
        return {
            "class": prediction,
            "confidence": confidence,
            "probabilities": probabilities,
            "anomaly_score": score,
            "is_anomaly": prediction > 0,
        }

    def _balanced_indices(self, labels, count):
        classes = labels.unique(sorted=True)
        if not len(classes):
            return torch.empty(0, dtype=torch.long)
        order = classes[torch.randperm(len(classes), generator=self.generator)]
        indices = []
        for i in range(count):
            group = (labels == order[i % len(order)]).nonzero().flatten()
            draw = int(torch.randint(len(group), (1,), generator=self.generator))
            indices.append(int(group[draw]))
        return torch.tensor(indices)

    def _replay(self):
        if not self.memory:
            return torch.empty(0, self.config.input_dim), torch.empty(0, dtype=torch.long)
        x = torch.cat(list(self.memory.values()))
        y = torch.cat(
            [
                torch.full((len(points),), label, dtype=torch.long)
                for label, points in self.memory.items()
            ]
        )
        indices = self._balanced_indices(y, min(len(y), self.config.replay_batch))
        return x[indices], y[indices]

    def _pseudo_gate(self, features, predictions):
        accepted = torch.zeros(len(features), dtype=torch.bool)
        with torch.no_grad():
            for label, trusted in self.memory.items():
                normalized = self.model.normalized(trusted)
                prototype = normalized.mean(0)
                radius = (normalized - prototype).square().mean(1).sqrt().max().clamp_min(0.5)
                residual = (self.model.normalized(features) - prototype).square().mean(1).sqrt()
                accepted |= (predictions == label) & (residual <= radius * 1.5)
        return accepted

    def _update_coreset(self, x, y):
        self.model.eval()
        with torch.no_grad():
            for label in y.unique(sorted=True).tolist():
                incoming = x[y == label]
                candidates = torch.cat((self.memory.get(label, incoming[:0]), incoming))
                # Identical points must not displace a distinct representative.
                candidates = torch.unique(candidates, dim=0)
                if len(candidates) <= self.config.memory_per_class:
                    self.memory[label] = candidates.clone()
                    continue
                embeddings = self.model.embeddings(candidates)
                embeddings = F.normalize(embeddings, dim=1)
                distances = (embeddings - embeddings.mean(0)).square().sum(1)
                first = int(distances.argmin())
                selected = [first]
                minimum = (embeddings - embeddings[first]).square().sum(1)
                for _ in range(self.config.memory_per_class - 1):
                    minimum[selected] = -1
                    farthest = int(minimum.argmax())
                    selected.append(farthest)
                    minimum = torch.minimum(
                        minimum, (embeddings - embeddings[farthest]).square().sum(1)
                    )
                self.memory[label] = candidates[selected].clone()

    def observe(self, features, labels=None, steps=None, use_pseudo=False):
        x = self._features(features)
        if not len(x) or len(x) > self.config.max_update_samples:
            raise ValueError("empty or oversized online update batch")
        if labels is None:
            y = torch.full((len(x),), -1, dtype=torch.long)
        else:
            raw_labels = torch.as_tensor(labels).detach().cpu()
            if raw_labels.shape != (len(x),) or not torch.isfinite(raw_labels).all():
                raise ValueError("expected one finite label per sensor vector")
            y = raw_labels.long()
            if not torch.equal(raw_labels, y) or ((y < -1) | (y >= self.config.num_classes)).any():
                raise ValueError("labels must be -1 or an integer configured class")
        budget = self.config.online_steps if steps is None else steps
        if not isinstance(budget, int) or budget <= 0:
            raise ValueError("training step budget must be positive")
        predictions = self.predict(x)  # Absolutely before any labels are revealed.
        trusted = y >= 0
        novel_labels = trusted & ~self.model.seen_classes[y.clamp_min(0)]
        use_pseudo = use_pseudo and not bool(novel_labels.any())
        pseudo = (
            (
                (~trusted)
                & (predictions["confidence"] >= self.config.pseudo_threshold)
                & self._pseudo_gate(x, predictions["class"])
            )
            if use_pseudo
            else torch.zeros_like(trusted)
        )
        accepted = trusted | pseudo
        targets = y.clone()
        targets[pseudo] = predictions["class"][pseudo]
        weights = torch.ones(len(x))
        weights[pseudo] = self.config.pseudo_weight
        old_classes = self.model.seen_classes.clone()
        teacher = copy.deepcopy(self.model).eval() if old_classes.sum() > 1 else None
        self.model.seen_classes[y[trusted].unique()] = True
        current_x, current_y, current_w = x[accepted], targets[accepted], weights[accepted]
        losses = []
        with torch.random.fork_rng():
            torch.manual_seed(self.config.seed + self.updates)
            for _ in range(budget if len(current_y) else 0):
                chosen = self._balanced_indices(
                    current_y, min(len(current_y), self.config.batch_size)
                )
                batch_x, batch_y = current_x[chosen], current_y[chosen]
                batch_w = current_w[chosen]
                replay_x, replay_y = self._replay()
                if len(replay_y):
                    batch_x = torch.cat((batch_x, replay_x))
                    batch_y = torch.cat((batch_y, replay_y))
                    batch_w = torch.cat((batch_w, torch.ones(len(replay_y))))
                self.model.train()
                self.optimizer.zero_grad()
                logits = self.model(batch_x)
                classification = (
                    F.cross_entropy(logits, batch_y, reduction="none") * batch_w
                ).mean()
                loss = classification
                if teacher is not None and len(replay_y) and self.config.distillation_weight:
                    with torch.no_grad():
                        target_logits = teacher(replay_x)[:, old_classes]
                    replay_logits = logits[-len(replay_y) :, old_classes]
                    distillation = (
                        F.kl_div(
                            F.log_softmax(replay_logits / 2, dim=1),
                            F.softmax(target_logits / 2, dim=1),
                            reduction="batchmean",
                        )
                        * 4
                    )
                    loss = loss + self.config.distillation_weight * distillation
                if not torch.isfinite(loss):
                    raise RuntimeError("non-finite SRTFD training objective")
                loss.backward()
                gradient = nn.utils.clip_grad_norm_(self.model.parameters(), 10)
                if not torch.isfinite(gradient):
                    raise RuntimeError("non-finite SRTFD training gradient")
                self.optimizer.step()
                self.gradient_steps += 1
                losses.append(float(loss.detach()))
        self._update_coreset(x[trusted], y[trusted])
        self.model.eval()
        self.updates += 1
        return {
            "prediction_before_update": predictions,
            "labeled": int(trusted.sum()),
            "pseudo": int(pseudo.sum()),
            "unlabeled_rejected": int((~accepted).sum()),
            "loss_first": losses[0] if losses else None,
            "loss_last": losses[-1] if losses else None,
            "gradient_steps": len(losses),
            "memory_by_class": {str(k): len(v) for k, v in self.memory.items()},
        }

    def evaluate(self, features, labels):
        x = self._features(features)
        y = torch.as_tensor(labels, dtype=torch.long)
        if y.shape != (len(x),) or ((y < 0) | (y >= self.config.num_classes)).any():
            raise ValueError("invalid evaluation labels")
        prediction = self.predict(x)["class"]
        recalls = {
            str(label): float((prediction[y == label] == label).float().mean())
            for label in y.unique(sorted=True).tolist()
        }
        return {
            "accuracy": float((prediction == y).float().mean()),
            "recall_by_class": recalls,
            "macro_recall": sum(recalls.values()) / max(1, len(recalls)),
        }

    def save(self, path):
        save_checkpoint(
            path,
            {
                "format": self.FORMAT,
                "upstream": self.upstream,
                "config": asdict(self.config),
                "model": self.model.state_dict(),
                "optimizer": self.optimizer.state_dict(),
                "memory": self.memory,
                "normalizer_fitted": self.normalizer_fitted,
                "updates": self.updates,
                "gradient_steps": self.gradient_steps,
                "rng": self.generator.get_state(),
            },
        )

    @classmethod
    def load(cls, path):
        state = load_checkpoint(path)
        if state["format"] != cls.FORMAT:
            raise ValueError(
                "Legacy custom-network checkpoint is unsupported; retrain the original SRTFD network"
            )
        learner = cls(DiagnosisConfig(**state["config"]))
        if learner.upstream != state["upstream"]:
            raise ValueError("SRTFD source hash does not match the checkpoint provenance")
        learner.model.load_state_dict(state["model"])
        learner.optimizer.load_state_dict(state["optimizer"])
        learner.memory = state["memory"]
        learner.normalizer_fitted = state["normalizer_fitted"]
        learner.updates, learner.gradient_steps = state["updates"], state["gradient_steps"]
        learner.generator.set_state(state["rng"])
        learner.model.eval()
        return learner

    def export_torchscript(self, path):
        if not self.normalizer_fitted or not self.model.seen_classes.any():
            raise ValueError("cannot export an untrained diagnosis model")
        self.model.eval()
        scripted = torch.jit.script(copy.deepcopy(self.model))
        scripted.save(str(path))
        return Path(path)
