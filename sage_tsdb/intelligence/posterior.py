"""Analytic comparison baselines only; neural networks are reused from PECJ.

The generative model and its assumptions are documented in RESEARCH_ENGINE.md.
Inference never accepts a record arriving after the requested cutoff. Global
EMA statistics learn only from explicitly source-certified complete windows.
"""

import math
import time
from dataclasses import asdict, dataclass
from numbers import Integral
from typing import Sequence

import torch
from torch import nn

from .checkpoint import load_checkpoint, save_checkpoint


@dataclass(frozen=True)
class Event:
    stream: str
    event_us: int
    arrival_us: int
    key: int
    value: float = 0.0

    def __post_init__(self):
        if any(
            not isinstance(value, Integral) or isinstance(value, bool)
            for value in (self.event_us, self.arrival_us, self.key)
        ):
            raise ValueError("event/arrival microseconds and key must be integers")
        if min(self.event_us, self.arrival_us, self.key) < 0:
            raise ValueError("event times and keys must be nonnegative")
        if self.stream not in ("S", "R") or self.arrival_us < self.event_us:
            raise ValueError("stream must be S/R and arrival must follow event time")
        if not math.isfinite(self.value):
            raise ValueError("event value must be finite")


@dataclass
class PDAConfig:
    keys: int = 8
    mode: str = "vi"  # analytic baselines only; neural inference uses upstream PECJ
    steps: int = 32
    learning_rate: float = 0.035
    samples: int = 16
    predictive_samples: int = 512
    quadrature: int = 48
    ema_alpha: float = 0.15
    initial_count_per_key: float = 4.0
    initial_delay_ms: float = 8.0
    initial_delay_sigma: float = 0.5
    high_variance: float = 0.45
    low_variance: float = 0.20
    high_disorder: float = 0.28
    low_disorder: float = 0.12
    min_dwell: int = 3
    seed: int = 2026

    def __post_init__(self):
        if any(
            isinstance(value, float) and not math.isfinite(value) for value in asdict(self).values()
        ):
            raise ValueError("posterior configuration must be finite")
        if self.mode not in ("ema", "vi"):
            raise ValueError(
                "Only analytic ema/vi baselines are supported here; use native PECJ AI for neural inference"
            )
        if (
            min(
                self.keys,
                self.steps,
                self.samples,
                self.predictive_samples,
                self.quadrature,
                self.min_dwell,
            )
            <= 0
        ):
            raise ValueError("dimensions and iteration budgets must be positive")
        if not 0 < self.ema_alpha <= 1 or self.learning_rate <= 0:
            raise ValueError("invalid learning rate")
        if min(self.initial_count_per_key, self.initial_delay_ms, self.initial_delay_sigma) <= 0:
            raise ValueError("prior rates and delay parameters must be positive")
        if not 0 <= self.low_disorder < self.high_disorder <= 1:
            raise ValueError("disorder hysteresis thresholds must be ordered")
        if not 0 <= self.low_variance < self.high_variance:
            raise ValueError("variance hysteresis thresholds must be ordered")


class PDAJoinEngine:
    """Full-window count posterior, completed Join distribution and ELBO trace.

    Rates are expected counts per reference window. Source feedback must use a
    constant window duration, so overlapping windows cannot duplicate evidence.
    Repeated estimate calls do not update global priors or duplicate feedback.
    """

    FORMAT = 2

    def __init__(self, config=None):
        self.config = config or PDAConfig()
        c = self.config
        self.dimensions = 2 * c.keys + 4
        self.prior_loc = torch.tensor(
            [math.log(c.initial_count_per_key)] * (2 * c.keys)
            + [math.log(c.initial_delay_ms)] * 2
            + [math.log(c.initial_delay_sigma)] * 2
        )
        self.prior_log_scale = torch.tensor([-0.4] * (2 * c.keys) + [-0.7] * 4)
        self.generator = torch.Generator().manual_seed(c.seed)
        self.active_mode = "ema"
        self.dwell = 0
        self.windows_learned = 0
        self.last_feedback_end = None
        self.reference_duration = None

    def _observations(self, events, start, end, cutoff):
        events = tuple(events)
        if end <= start or cutoff < end:
            raise ValueError("require start < end <= cutoff")
        counts = torch.zeros(2, self.config.keys)
        logs, disorder, features = [], [], []
        for event in events:
            if event.arrival_us > cutoff:
                raise ValueError("future arrival passed to posterior inference")
            if not 0 <= event.key < self.config.keys:
                raise ValueError("key outside configured finite vocabulary")
        for stream_id, stream in enumerate(("S", "R")):
            selected = sorted(
                (e for e in events if e.stream == stream and start <= e.event_us < end),
                key=lambda e: e.arrival_us,
            )
            delays = torch.tensor(
                [max(0.001, (e.arrival_us - e.event_us) / 1000) for e in selected]
            )
            log_delay = delays.log()
            logs.append(log_delay)
            max_time, inverted = start - 1, 0
            for event in selected:
                counts[stream_id, event.key] += 1
                inverted += event.event_us < max_time
                max_time = max(max_time, event.event_us)
            ratio = inverted / max(1, len(selected))
            disorder.append(ratio)
            mean = float(log_delay.mean()) if len(delays) else 0.0
            variance = float(log_delay.var(unbiased=False)) if len(delays) else 0.0
            features.extend(
                [
                    math.log1p(len(selected)) / 5,
                    mean / 5,
                    min(variance, 25.0) / 5,
                    ratio,
                    math.log1p((cutoff - end) / 1000) / 5,
                ]
            )
        duration_ms = (end - start) / 1000
        bins = (torch.arange(self.config.quadrature) + 0.5) / self.config.quadrature
        ages_ms = (cutoff - start) / 1000 - duration_ms * bins
        return {
            "counts": counts,
            "logs": logs,
            "log_ages": ages_ms.clamp_min(0.001).log(),
            "duration_ms": duration_ms,
            "features": torch.cat((counts.flatten().log1p() / 5, torch.tensor(features))),
            "variance": max((float(x.var(unbiased=False)) if len(x) else 0.0) for x in logs),
            "disorder": max(disorder),
        }

    def _exposure(self, latent, observation):
        k = self.config.keys
        mu = latent[:, 2 * k : 2 * k + 2]
        sigma = latent[:, 2 * k + 2 :].clamp(-3, 2).exp()
        z = (observation["log_ages"][None, None, :] - mu[:, :, None]) / sigma[:, :, None]
        return (0.5 * (1 + torch.erf(z / math.sqrt(2)))).mean(-1)

    def elbo(self, loc, log_scale, observation, noise):
        """Reparameterized point-process likelihood minus analytic Normal KL."""
        k = self.config.keys
        latent = loc + noise * log_scale.exp()
        log_rates = latent[:, : 2 * k].reshape(-1, 2, k).clamp(-15, 15)
        rates = log_rates.exp()
        exposure = self._exposure(latent, observation)
        log_likelihood = (observation["counts"][None] * log_rates).sum((1, 2))
        log_likelihood = log_likelihood - (rates.sum(-1) * exposure).sum(-1)
        for stream in range(2):
            data = observation["logs"][stream]
            if len(data):
                mu = latent[:, 2 * k + stream, None]
                log_sigma = latent[:, 2 * k + 2 + stream, None].clamp(-3, 2)
                terms = (
                    -0.5 * ((data - mu) / log_sigma.exp()).square()
                    - log_sigma
                    - data
                    - 0.5 * math.log(2 * math.pi)
                    - math.log(observation["duration_ms"])
                )
                log_likelihood = log_likelihood + terms.sum(-1)
        variance_ratio = (2 * (log_scale - self.prior_log_scale)).exp()
        standardized = (loc - self.prior_loc) / self.prior_log_scale.exp()
        kl = (
            self.prior_log_scale - log_scale + 0.5 * (variance_ratio + standardized.square() - 1)
        ).sum()
        return log_likelihood.mean() - kl

    def _ema_posterior(self, observation):
        # Moment-match lognormal prior to Gamma, update Poisson exposure/counts,
        # then match the resulting Gamma back into mean-field lognormal form.
        k = self.config.keys
        loc, log_scale = self.prior_loc.clone(), self.prior_log_scale.clone()
        exposure = self._exposure(loc[None], observation)[0]
        variance = (2 * log_scale[: 2 * k]).exp()
        mean = (loc[: 2 * k] + 0.5 * variance).exp()
        alpha = 1 / variance.expm1()
        beta = alpha / mean
        alpha = alpha + observation["counts"].flatten()
        beta = beta + exposure[:, None].expand(2, k).flatten()
        posterior_variance = torch.log1p(1 / alpha)
        loc[: 2 * k] = (alpha / beta).log() - 0.5 * posterior_variance
        log_scale[: 2 * k] = 0.5 * posterior_variance.log()
        return loc, log_scale

    def estimate(self, events: Sequence[Event], start_us, end_us, cutoff_us):
        began = time.perf_counter()
        if self.last_feedback_end is not None and start_us < self.last_feedback_end:
            raise ValueError("inference window overlaps already learned complete history")
        if self.reference_duration is not None and end_us - start_us != self.reference_duration:
            raise ValueError("window duration differs from calibrated rate reference")
        observation = self._observations(events, start_us, end_us, cutoff_us)
        mode = self.config.mode
        base_loc, base_scale = self._ema_posterior(observation)
        half = torch.randn(
            max(1, self.config.samples // 2), self.dimensions, generator=self.generator
        )
        noise = torch.cat((half, -half))
        before, after, steps = None, None, 0
        if mode == "ema":
            loc, log_scale = base_loc, base_scale
            before = after = float(self.elbo(loc, log_scale, observation, noise))
        else:
            loc, log_scale = nn.Parameter(base_loc), nn.Parameter(base_scale)
            optimizer = torch.optim.Adam([loc, log_scale], lr=self.config.learning_rate)
            before = float(self.elbo(loc, log_scale.clamp(-4, 0.5), observation, noise).detach())
            best_value = before
            best_state = (loc.detach().clone(), log_scale.detach().clone())
            for _ in range(self.config.steps):
                optimizer.zero_grad()
                loss = -self.elbo(loc, log_scale.clamp(-4, 0.5), observation, noise)
                if not torch.isfinite(loss):
                    break
                loss.backward()
                norm = nn.utils.clip_grad_norm_([loc, log_scale], 20)
                if not torch.isfinite(norm):
                    break
                optimizer.step()
                steps += 1
                with torch.no_grad():
                    candidate = float(self.elbo(loc, log_scale.clamp(-4, 0.5), observation, noise))
                if math.isfinite(candidate) and candidate > best_value:
                    best_value = candidate
                    best_state = (loc.detach().clone(), log_scale.detach().clone())
            loc, log_scale = best_state
            after = best_value
        k = self.config.keys
        with torch.no_grad():
            latent = (
                loc
                + torch.randn(
                    self.config.predictive_samples, self.dimensions, generator=self.generator
                )
                * log_scale.exp()
            )
            exposure = self._exposure(latent, observation)
            rates = latent[:, : 2 * k].reshape(-1, 2, k).clamp(-15, 15).exp()
            missing_mean = rates * (1 - exposure[:, :, None])
            completed_mean = observation["counts"][None] + missing_mean
            estimate = (completed_mean[:, 0] * completed_mean[:, 1]).sum(-1).mean()
            rate_totals = rates.sum(-1)
            key_selectivity = (
                (rates[:, 0] * rates[:, 1]).sum(-1) / rate_totals.prod(-1).clamp_min(1e-12)
            ).mean()
            missing = torch.poisson(missing_mean, generator=self.generator)
            completed = observation["counts"][None] + missing
            joins = (completed[:, 0] * completed[:, 1]).sum(-1)
            observed = int((observation["counts"][0] * observation["counts"][1]).sum())
            lower, upper = (float(x) for x in torch.quantile(joins, torch.tensor([0.025, 0.975])))
        return {
            "observed_join": observed,
            "posterior_join_mean": float(estimate),
            "predictive_lower": lower,
            "predictive_upper": upper,
            "completion_probability": exposure.mean(0).tolist(),
            "expected_stream_counts": completed_mean.sum(-1).mean(0).tolist(),
            "missing_count_mean": missing_mean.sum(-1).mean(0).tolist(),
            "posterior_key_selectivity": float(key_selectivity),
            "mode": mode,
            "elbo_before": before,
            "elbo_after": after,
            "optimization_steps": steps,
            "delay_log_variance": observation["variance"],
            "out_of_order_fraction": observation["disorder"],
            "prior_windows": self.windows_learned,
            "posterior_loc": loc.detach().tolist(),
            "posterior_log_scale": log_scale.detach().tolist(),
            "computation_ms": (time.perf_counter() - began) * 1000,
        }

    def update_complete_window(self, events, start_us, end_us, as_of_us, source_watermark_us):
        """Learn once, after the source certifies no more records for this window.

        A source watermark is a correctness contract, not an inference heuristic.
        It must be provided by ingestion; an end+fixed-delay timer is insufficient.
        """
        if source_watermark_us < end_us or as_of_us < source_watermark_us:
            raise ValueError("complete feedback requires an already observed source watermark")
        if self.last_feedback_end is not None and start_us < self.last_feedback_end:
            raise ValueError("duplicate, overlapping or out-of-order feedback")
        if self.reference_duration is not None and end_us - start_us != self.reference_duration:
            raise ValueError("feedback duration changed")
        observation = self._observations(events, start_us, end_us, as_of_us)
        c, k = self.config, self.config.keys
        target = self.prior_loc.clone()
        counts = observation["counts"].flatten()
        target[: 2 * k] = (counts + 0.5).log()
        for stream in range(2):
            data = observation["logs"][stream]
            if len(data):
                target[2 * k + stream] = data.mean()
                target[2 * k + 2 + stream] = data.std(unbiased=False).clamp_min(0.08).log()
        alpha = 1.0 if self.windows_learned == 0 else c.ema_alpha
        self.prior_loc.lerp_(target, alpha)
        # Fixed nonzero process noise keeps streaming priors responsive to drift.
        rate_scale = (1 / (counts + 1) + 0.08).sqrt().clamp(0.2, 0.8)
        self.prior_log_scale[: 2 * k] = rate_scale.log()
        self.reference_duration = end_us - start_us
        self.windows_learned += 1
        self.last_feedback_end = end_us

    def save(self, path):
        save_checkpoint(
            path,
            {
                "format": self.FORMAT,
                "config": asdict(self.config),
                "prior_loc": self.prior_loc,
                "prior_log_scale": self.prior_log_scale,
                "rng": self.generator.get_state(),
                "active_mode": self.active_mode,
                "dwell": self.dwell,
                "windows_learned": self.windows_learned,
                "last_feedback_end": self.last_feedback_end,
                "reference_duration": self.reference_duration,
            },
        )

    @classmethod
    def load(cls, path):
        state = load_checkpoint(path)
        if state["format"] != cls.FORMAT:
            raise ValueError("unsupported PDA checkpoint format")
        engine = cls(PDAConfig(**state["config"]))
        for name in (
            "prior_loc",
            "prior_log_scale",
            "active_mode",
            "dwell",
            "windows_learned",
            "last_feedback_end",
            "reference_duration",
        ):
            setattr(engine, name, state[name])
        engine.generator.set_state(state["rng"])
        return engine


class StreamingPDA:
    """Bounded arrival-ordered adapter; separate source/event and arrival clocks."""

    def __init__(self, engine=None, max_events=100_000):
        if max_events <= 0:
            raise ValueError("max_events must be positive")
        self.engine = engine or PDAJoinEngine()
        self.max_events = max_events
        self.events = []
        self.clock_us = -1
        self.source_watermark_us = -1
        self.retained_start_us = -1

    def advance(self, arrival_us):
        if arrival_us < self.clock_us:
            raise ValueError("arrival clock cannot go backwards")
        self.clock_us = arrival_us

    def ingest(self, event):
        if event.event_us < self.source_watermark_us:
            raise ValueError("record violates the source completeness watermark")
        if len(self.events) >= self.max_events:
            raise OverflowError("event buffer full; certify and evict complete windows")
        if not 0 <= event.key < self.engine.config.keys:
            raise ValueError("unknown key")
        self.advance(event.arrival_us)
        self.events.append(event)

    def estimate(self, start_us, end_us, cutoff_us):
        if start_us < self.retained_start_us:
            raise ValueError("window has been evicted")
        self.advance(cutoff_us)
        return self.engine.estimate(self.events, start_us, end_us, cutoff_us)

    def certify(self, source_watermark_us):
        if not self.source_watermark_us <= source_watermark_us <= self.clock_us:
            raise ValueError("invalid source watermark")
        self.source_watermark_us = source_watermark_us

    def learn_complete(self, start_us, end_us):
        if start_us < self.retained_start_us:
            raise ValueError("complete window has already been evicted")
        self.engine.update_complete_window(
            self.events, start_us, end_us, self.clock_us, self.source_watermark_us
        )

    def evict_before(self, event_us):
        if event_us > self.source_watermark_us:
            raise ValueError("cannot evict uncertified event time")
        self.events = [e for e in self.events if e.event_us >= event_us]
        self.retained_start_us = max(self.retained_start_us, event_us)
