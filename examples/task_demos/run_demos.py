#!/usr/bin/env python3
"""Run four reproducible TASK.md demos; use --help for component selection."""

import argparse
import copy
import random
import subprocess
import sys
from collections import Counter
from pathlib import Path

from common import PecjCLI, binary_metrics, exact_pairs, write_csv, write_json
from models import OnlineModes, ReplayDiagnosis, WarningDetector, center, distance


def demo_join(output, seed, cli):
    """Caller-side arrival cutoffs, then real complete-batch IAWJ execution."""
    rng = random.Random(seed)
    rows, events = [], []
    for window in range(4):
        start = 1_000_000 + window * 20_000
        end = start + 20_000
        streams = {}
        for stream in ("s", "r"):
            records = []
            # Include out-of-window records in the audit log, then filter [start,end).
            for i in range(22):
                timestamp = start - 1 if i == 0 else end if i == 21 else start + i * 900
                delay = rng.randrange(1500) + (16_000 if i % 4 == 0 else 0)
                record = {"timestamp_us": timestamp, "key": i % 5,
                          "value": 10 + i, "arrival_us": timestamp + delay}
                records.append(record)
                events.append(dict(window=window, stream=stream, **record))
            streams[stream] = [p for p in records if start <= p["timestamp_us"] < end]
        truth = exact_pairs(streams["s"], streams["r"])
        for wait in (0, 4000, 16_000):
            visible = {name: sorted((p for p in records if p["arrival_us"] <= end + wait),
                                    key=lambda p: p["arrival_us"])
                       for name, records in streams.items()}
            expected = exact_pairs(visible["s"], visible["r"])
            status = cli.execute(output / ("w%d_wait%d" % (window, wait)),
                                 visible["s"], visible["r"], start, end, window + 1)
            if status["join_count"] != expected:
                raise RuntimeError("IAWJ differs from independent offline equality Join")
            rows.append({"window": window, "wait_us": wait, "full_truth": truth,
                         "visible_truth": expected, "join_count": status["join_count"],
                         "missing_pairs": truth - expected,
                         "relative_error_to_full": (truth - expected) / truth,
                         "engine_ms": status["computation_time_ms"],
                         "cli_wall_ms": status["cli_wall_time_ms"]})
    write_csv(output / "events.csv", events)
    write_csv(output / "windows.csv", rows)
    return {"backend": "real PECJ CLI / IAWJ", "seed": seed,
            "all_visible_joins_match_oracle": True,
            "mean_error_by_wait_us": {
                str(wait): sum(r["relative_error_to_full"] for r in rows if r["wait_us"] == wait) / 4
                for wait in (0, 4000, 16_000)},
            "scope": "Arrival cutoffs are simulated by caller. No PDA/VI compensation is measured."}


def demo_drift(output, seed, _cli):
    rng = random.Random(seed)
    normal = [(rng.gauss(0, 0.12), rng.gauss(0, 0.12)) for _ in range(40)]
    initial = center(normal)
    adaptive = OnlineModes(initial)
    rows, creations = [], []
    for i in range(480):
        # Smooth drift, a step to a new mode, then recurrence of the old mode.
        shift = min(i, 159) * 0.003 if i < 160 else 3.0 if i < 320 else 0.0
        anomaly = i % 47 == 23
        point = (shift + rng.gauss(0, 0.12) + (8 if anomaly else 0),
                 shift * 0.7 + rng.gauss(0, 0.12))
        frozen_score = distance(point, initial)
        score, mode, created = adaptive.observe(point)
        if created is not None:
            creations.append({"sample": i, "mode_id": created})
        rows.append({"sample": i, "phase": i // 160, "x": point[0], "y": point[1],
                     "truth_anomaly": anomaly, "frozen_score": frozen_score,
                     "adaptive_score": score, "frozen_alarm": frozen_score > 1,
                     "adaptive_alarm": score > 1, "mode_id_before_update": mode,
                     "created_mode_id": created, "active_modes": len(adaptive.modes)})
    write_csv(output / "predictions.csv", rows)
    truth = [r["truth_anomaly"] for r in rows]
    return {"backend": "Python online EMA microcluster baseline", "seed": seed,
            "frozen": binary_metrics(truth, [r["frozen_alarm"] for r in rows]),
            "adaptive": binary_metrics(truth, [r["adaptive_alarm"] for r in rows]),
            "mode_creation_events": creations,
            "final_modes": {str(k): v for k, v in adaptive.modes.items()},
            "scope": "Predictions precede updates; labels are used only for evaluation. "
                     "Persistent faults may be absorbed as modes without supervision."}


def sample_class(rng, label, shift=0.0):
    means = ((0.0, 0.0), (4.0, 0.0), (0.0, 4.0))
    return tuple(m + shift + rng.gauss(0, 0.22) for m in means[label])


def heldout_scores(models, seed, shift):
    rng = random.Random(seed)
    scores = {}
    for name, model in models.items():
        # Regenerate the identical held-out set for each model. Never train on it.
        rng.seed(seed)
        per_class = {}
        for label in range(3):
            correct = sum(model.predict(sample_class(rng, label, shift))[0] == label
                          for _ in range(60))
            per_class[str(label)] = correct / 60
        scores[name] = {"recall_by_class": per_class,
                        "macro_recall": sum(per_class.values()) / 3,
                        "memory_by_class": dict(Counter(str(c) for _, c in model.memory))}
    return scores


def demo_continual(output, seed, _cli):
    rng = random.Random(seed)
    balanced = ReplayDiagnosis()
    recent = ReplayDiagnosis(balanced=False)
    # Initial labeled calibration knows normal and an old fault, not the new one.
    for _ in range(18):
        for label in (0, 1):
            point = sample_class(rng, label)
            balanced.learn(point, label)
            recent.learn(point, label)
    models = {"frozen": copy.deepcopy(balanced), "recent_fifo": recent,
              "balanced_coreset": balanced}
    checkpoints = {"initial": heldout_scores(models, seed + 100, 0.0)}
    rows = []
    for phase in range(2):
        for step in range(360):
            # Old fault disappears for a whole phase, then returns rarely.
            draw = rng.random()
            label = (0 if draw < 0.8 else 2) if phase == 0 else (
                0 if draw < 0.8 else 1 if draw < 0.9 else 2)
            shift = 0.35 * (phase + 1)
            point = sample_class(rng, label, shift)
            # Reveal only 20% of labels, after prediction. Selection is label independent.
            revealed = label if rng.random() < 0.2 else None
            for name, model in models.items():
                if name == "frozen":
                    prediction, _, _ = model.predict(point)
                    source = "none"
                else:
                    prediction, source = model.observe(point, revealed)
                rows.append({"phase": phase, "step": step, "model": name,
                             "x": point[0], "y": point[1], "truth_class": label,
                             "revealed_label": revealed, "prediction_before_update": prediction,
                             "update_source": source, "memory_size": len(model.memory)})
        checkpoints["after_phase_%d" % phase] = heldout_scores(models, seed + 100 + phase, shift)
    write_csv(output / "predictions.csv", rows)
    counts = {name: dict(Counter(r["update_source"] for r in rows if r["model"] == name))
              for name in models}
    return {"backend": "Python nearest-centroid + replay baseline", "seed": seed,
            "class_names": {"0": "normal", "1": "old_fault", "2": "new_fault"},
            "checkpoints": checkpoints, "updates": counts,
            "memory_limit_samples": 36, "balanced_per_class_limit": 12,
            "scope": "Synthetic labels, 20% reveal rate; confidence-gated pseudo labels. "
                     "Coreset/replay prototype, not the SRTFD neural implementation."}


def matched_features(s, r):
    """Application-side unique-key alignment: PECJ exposes counts, not pairs."""
    left, right = {p["key"]: p["value"] for p in s}, {p["key"]: p["value"] for p in r}
    if len(left) != len(s) or len(right) != len(r):
        raise ValueError("warning demo requires unique keys within each sensor stream")
    keys = sorted(left.keys() & right.keys())
    if not keys:
        return None, 0
    return (sum(left[k] for k in keys) / len(keys),
            sum(right[k] for k in keys) / len(keys)), len(keys)


def demo_warning(output, seed, cli):
    rng = random.Random(seed)
    detector = WarningDetector()
    rows, events = [], []
    first_warning = None
    # Simulation clock: 1-second event windows; this is not real wall-clock streaming.
    failure_time = 23_000_000
    for window in range(24):
        start = 1_000_000 + window * 1_000_000
        end = start + 1_000_000
        cutoff = end + 100_000
        degradation = max(0, window - 10)
        streams = {}
        for name, base, slope in (("s", 60.0, 0.32), ("r", 2.0, 0.24)):
            records = []
            for i in range(12):
                timestamp = start + 20_000 + i * 75_000
                # One degraded-data window after calibration tests quality gating.
                delay = 2_000_000 if window == 9 and i % 2 == 0 and name == "r" else rng.randrange(200_000)
                p = {"timestamp_us": timestamp, "key": window * 100 + i,
                     "value": base + degradation * slope + rng.gauss(0, 0.13),
                     "arrival_us": timestamp + delay}
                events.append(dict(window=window, stream=name, **p))
                records.append(p)
            streams[name] = [p for p in records if p["arrival_us"] <= cutoff]
        status = cli.execute(output / ("w%02d" % window), streams["s"], streams["r"],
                             start, end, window + 1)
        features, matched = matched_features(streams["s"], streams["r"])
        if status["join_count"] != matched:
            raise RuntimeError("PECJ Join count disagrees with application-side unique-key alignment")
        coverage = matched / 12
        quality_ok = coverage >= 0.9
        score, warning = detector.observe(features if quality_ok else None)
        if warning and first_warning is None:
            first_warning = cutoff
        rows.append({"window": window, "decision_time_us": cutoff,
                     "join_count": status["join_count"], "coverage": coverage,
                     "quality_ok": quality_ok,
                     "temperature_mean": features[0] if features else None,
                     "vibration_mean": features[1] if features else None,
                     "score": score, "warning": warning,
                     "engine_ms": status["computation_time_ms"],
                     "cli_wall_ms": status["cli_wall_time_ms"]})
    write_csv(output / "events.csv", events)
    write_csv(output / "diagnosis.csv", rows)
    return {"backend": "real PECJ IAWJ + Python calibrated residual detector", "seed": seed,
            "failure_time_us": failure_time, "first_warning_time_us": first_warning,
            "lead_time_us": failure_time - first_warning if first_warning is not None else None,
            "warned_before_failure": first_warning is not None and first_warning < failure_time,
            "data_quality_rejections": sum(not r["quality_ok"] for r in rows),
            "scope": "Feature alignment and result CSV persistence are application-side. "
                     "Lead time uses arrival cutoff on a synthetic clock; no CXL or hardware claim."}


DEMOS = {"join": demo_join, "drift": demo_drift,
         "continual": demo_continual, "warning": demo_warning}


def find_component():
    for parent in Path(__file__).resolve().parents:
        if (parent / "bin/sage_tsdb_pecj").is_file():
            return parent
        candidate = parent / "dist/sage-tsdb-pecj-component-0.2.0-linux-x86_64"
        if (candidate / "bin/sage_tsdb_pecj").is_file():
            return candidate
    return None


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--demo", choices=["all"] + list(DEMOS), default="all")
    parser.add_argument("--component", type=Path, default=None,
                        help="extracted PECJ component root (auto-detected in repository)")
    parser.add_argument("--output", type=Path, default=Path("demo_outputs"))
    parser.add_argument("--seed", type=int, default=2026)
    args = parser.parse_args()
    selected = list(DEMOS) if args.demo == "all" else [args.demo]
    cli = None
    if any(name in ("join", "warning") for name in selected):
        component = args.component if args.component is not None else find_component()
        if component is None:
            parser.error("Join/warning demos require --component /path/to/extracted/package")
        cli = PecjCLI(component)
    args.output.mkdir(parents=True, exist_ok=True)
    summaries = {}
    for name in selected:
        output = args.output / name
        output.mkdir(parents=True, exist_ok=True)
        summary = DEMOS[name](output, args.seed, cli)
        write_json(output / "summary.json", summary)
        summaries[name] = summary
        print("[%s] %s" % (name, output / "summary.json"), flush=True)
    write_json(args.output / "summary.json", summaries)
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, RuntimeError, ValueError, subprocess.SubprocessError) as error:
        print("Demo failed: " + str(error), file=sys.stderr)
        sys.exit(1)
