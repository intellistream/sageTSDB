"""Small executable baselines for TASK.md, independent of SRTFD/Torch.

These algorithms demonstrate the data flow; they do not implement the research
PDA/VI/dual-mode neural models or the SRTFD paper.
"""

import math
from collections import defaultdict, deque


def distance(a, b):
    return math.sqrt(sum((x - y) ** 2 for x, y in zip(a, b)))


def center(points):
    return tuple(sum(axis) / len(points) for axis in zip(*points))


class OnlineModes:
    """Gated EMA centers with stable IDs and a compact novelty buffer.

    A run of nearby novel points creates a new operating mode. Isolated spikes
    never update a center. Predictions always precede updates. A sustained fault
    can also become a mode: distinguishing it from a valid regime needs labels.
    """

    def __init__(self, initial, radius=1.0, patience=10, max_modes=4):
        self.modes = {0: tuple(initial)}
        self.last_seen = {0: 0}
        self.radius = radius
        self.patience = patience
        self.max_modes = max_modes
        self.pending = deque(maxlen=patience)
        self.next_id = 1
        self.step = 0

    def predict(self, point):
        mode = min(self.modes, key=lambda k: distance(point, self.modes[k]))
        score = distance(point, self.modes[mode]) / self.radius
        return score, mode

    def observe(self, point):
        score, mode = self.predict(point)
        self.step += 1
        created = None
        if score <= 1.0:
            self.modes[mode] = tuple(0.95 * a + 0.05 * b
                                     for a, b in zip(self.modes[mode], point))
            self.last_seen[mode] = self.step
            self.pending.clear()
        else:
            if self.pending and distance(point, center(self.pending)) > self.radius:
                self.pending.clear()
            self.pending.append(tuple(point))
            if len(self.pending) == self.patience:
                if len(self.modes) == self.max_modes:
                    oldest = min(self.last_seen, key=self.last_seen.get)
                    del self.modes[oldest]
                    del self.last_seen[oldest]
                created = self.next_id
                self.next_id += 1
                self.modes[created] = center(self.pending)
                self.last_seen[created] = self.step
                self.pending.clear()
        return score, mode, created


class ReplayDiagnosis:
    """Nearest-centroid learner with bounded labeled replay.

    balanced=True selects a geometric coreset independently per class; the
    comparison baseline retains only the latest capacity samples globally.
    Pseudo labels require both a distance gate and a centroid-margin gate.
    Confidence is a heuristic margin, not a calibrated probability.
    """

    def __init__(self, balanced=True, per_class=12, max_classes=3):
        self.balanced = balanced
        self.per_class = per_class
        self.max_classes = max_classes
        self.memory = []

    def predict(self, point):
        groups = defaultdict(list)
        for features, label in self.memory:
            groups[label].append(features)
        ranked = sorted((distance(point, center(points)), label)
                        for label, points in groups.items())
        if not ranked:
            return -1, 0.0, math.inf
        nearest, label = ranked[0]
        margin = (1.0 - nearest / max(ranked[1][0], 1e-12)
                  if len(ranked) > 1 else 0.0)
        return label, margin, nearest

    def learn(self, point, label):
        known = {label_ for _, label_ in self.memory}
        if label not in known and len(known) >= self.max_classes:
            raise ValueError("class capacity exceeded; increase max_classes")
        self.memory.append((tuple(point), label))
        if not self.balanced:
            self.memory = self.memory[-self.per_class * self.max_classes:]
            return
        same_class = [p for p, c in self.memory if c == label]
        if len(same_class) <= self.per_class:
            return
        # Seed near the mean, then greedily cover the remaining feature space.
        mean = center(same_class)
        first = min(range(len(same_class)), key=lambda i: distance(same_class[i], mean))
        selected = [same_class.pop(first)]
        while same_class and len(selected) < self.per_class:
            farthest = max(range(len(same_class)),
                           key=lambda i: min(distance(same_class[i], p) for p in selected))
            selected.append(same_class.pop(farthest))
        self.memory = [(p, c) for p, c in self.memory if c != label]
        self.memory.extend((p, label) for p in selected)

    def observe(self, point, revealed_label=None, use_pseudo=True):
        prediction, margin, nearest = self.predict(point)
        source = "none"
        if revealed_label is not None:
            self.learn(point, revealed_label)
            source = "label"
        elif use_pseudo and margin >= 0.8 and nearest <= 0.6:
            self.learn(point, prediction)
            source = "pseudo"
        return prediction, source


class WarningDetector:
    """Frozen normal calibration; warn after consecutive large residuals."""

    def __init__(self, warmup=8, threshold=4.0, consecutive=2):
        self.warmup = warmup
        self.threshold = threshold
        self.consecutive = consecutive
        self.normal = []
        self.mean = None
        self.scale = None
        self.streak = 0

    def observe(self, features):
        if features is None:
            self.streak = 0
            return None, False
        if self.mean is None:
            self.normal.append(tuple(features))
            if len(self.normal) == self.warmup:
                self.mean = center(self.normal)
                self.scale = tuple(max(0.15, math.sqrt(sum((p[i] - m) ** 2
                                                           for p in self.normal)
                                                      / len(self.normal)))
                                   for i, m in enumerate(self.mean))
            return None, False
        score = math.sqrt(sum(((x - m) / s) ** 2
                              for x, m, s in zip(features, self.mean, self.scale))
                          / len(features))
        self.streak = self.streak + 1 if score >= self.threshold else 0
        return score, self.streak >= self.consecutive
