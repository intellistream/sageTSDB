"""Research engine training, ablations, model export and sensor CSV diagnosis."""

import argparse
import csv
import hashlib
import json
import math
import random
import sys
from collections import Counter
from pathlib import Path

import torch

from .diagnosis import DiagnosisConfig, SRTFDLearner
from .posterior import Event, PDAConfig, PDAJoinEngine, StreamingPDA


def write_json(path, value):
    Path(path).write_text(
        json.dumps(value, ensure_ascii=False, indent=2, allow_nan=False) + "\n", encoding="utf-8"
    )


def write_csv(path, rows):
    with Path(path).open("w", newline="", encoding="utf-8") as output:
        writer = csv.DictWriter(output, fieldnames=list(rows[0]))
        writer.writeheader()
        writer.writerows(rows)


def poisson(rng, rate):
    limit, product, n = math.exp(-rate), 1.0, 0
    while product > limit:
        product *= rng.random()
        n += 1
    return n - 1


def posterior_experiment(output, seed=2026, windows=60, steps=32):
    """Four ablations on one causal event timeline, with independent truth."""
    if windows < 6:
        raise ValueError("posterior experiment needs at least six windows")
    output.mkdir(parents=True, exist_ok=True)
    rng = random.Random(seed)
    events, actions, truth = [], [], {}
    cumulative_complete = 0
    window_us, base = 100_000, 1_000_000
    for window in range(windows):
        start, end = base + window * window_us, base + (window + 1) * window_us
        cutoff = end + 2000
        phase = min(2, window * 3 // windows)
        median = 1.0 if phase == 0 else 25 + 15 * math.sin(window * 0.7) if phase == 1 else 2.0
        sigma = 0.15 if phase != 1 else 0.85
        full = {"S": Counter(), "R": Counter()}
        latest = cutoff + 1
        for stream in ("S", "R"):
            for key in range(8):
                rate = 5 + 2 * (key % 3) + 1.5 * math.sin(window / 6)
                for _ in range(poisson(rng, rate)):
                    timestamp = start + rng.randrange(window_us)
                    delay = max(1, int(rng.lognormvariate(math.log(median), sigma) * 1000))
                    event = Event(stream, timestamp, timestamp + delay, key, rng.random())
                    actions.append((event.arrival_us, 0, len(events), event))
                    events.append(event)
                    full[stream][key] += 1
                    latest = max(latest, event.arrival_us)
        truth[window] = sum(n * full["R"][key] for key, n in full["S"].items())
        actions.append((cutoff, 1, window, (start, end, cutoff)))
        cumulative_complete = max(cumulative_complete, latest)
        actions.append((cumulative_complete, 2, window, (start, end)))
    engines = {
        name: StreamingPDA(PDAJoinEngine(PDAConfig(mode=name, steps=steps, seed=seed)))
        for name in ("ema", "vi")
    }
    rows = []
    for timestamp, kind, window, payload in sorted(actions, key=lambda a: a[:3]):
        for name, stream in engines.items():
            if kind == 0:
                stream.ingest(payload)
            elif kind == 1:
                result = stream.estimate(*payload)
                full_truth = truth[window]
                rows.append(
                    {
                        "window": window,
                        "variant": name,
                        "phase": min(2, window * 3 // windows),
                        "cutoff_us": timestamp,
                        "full_join_truth": full_truth,
                        "observed_join": result["observed_join"],
                        "posterior_join_mean": result["posterior_join_mean"],
                        "observed_relative_error": abs(result["observed_join"] - full_truth)
                        / max(1, full_truth),
                        "posterior_relative_error": abs(result["posterior_join_mean"] - full_truth)
                        / max(1, full_truth),
                        "predictive_lower": result["predictive_lower"],
                        "predictive_upper": result["predictive_upper"],
                        "covered": result["predictive_lower"]
                        <= full_truth
                        <= result["predictive_upper"],
                        "mode": result["mode"],
                        "elbo_before": result["elbo_before"],
                        "elbo_after": result["elbo_after"],
                        "steps": result["optimization_steps"],
                        "prior_windows": result["prior_windows"],
                        "disorder": result["out_of_order_fraction"],
                        "delay_log_variance": result["delay_log_variance"],
                        "computation_ms": result["computation_ms"],
                    }
                )
            else:
                stream.advance(timestamp)
                stream.certify(payload[1])
                stream.learn_complete(*payload)
                stream.evict_before(payload[1])
    write_csv(output / "windows.csv", rows)
    write_csv(
        output / "events.csv",
        [
            dict(
                stream=e.stream,
                event_us=e.event_us,
                arrival_us=e.arrival_us,
                key=e.key,
                value=e.value,
            )
            for e in sorted(events, key=lambda e: e.arrival_us)
        ],
    )
    summary = {
        "seed": seed,
        "windows": windows,
        "data": "synthetic marked Poisson streams",
        "clock": "causal arrival order; source-certified feedback after prediction",
        "variants": {},
    }
    for name, stream in engines.items():
        selected = [r for r in rows if r["variant"] == name]
        summary["variants"][name] = {
            "mean_observed_relative_error": sum(r["observed_relative_error"] for r in selected)
            / windows,
            "mean_posterior_relative_error": sum(r["posterior_relative_error"] for r in selected)
            / windows,
            "predictive_95_coverage": sum(r["covered"] for r in selected) / windows,
            "mean_computation_ms": sum(r["computation_ms"] for r in selected) / windows,
            "mode_counts": dict(Counter(r["mode"] for r in selected)),
            "optimized_windows": sum(r["steps"] > 0 for r in selected),
            "elbo_improved_windows": sum(r["elbo_after"] > r["elbo_before"] for r in selected),
        }
        stream.engine.save(output / (name + ".checkpoint"))
    write_json(output / "summary.json", summary)
    print("PDA/VI ablations: " + str(output / "summary.json"), flush=True)
    return summary


def load_tep(directory):
    splits = {part: ([], []) for part in ("train", "validation", "test")}
    hashes = {}
    for label in range(22):
        file = directory / ("d%02d_te.dat" % label)
        raw = file.read_bytes()
        hashes[file.name] = hashlib.sha256(raw).hexdigest()
        rows = [
            [float(value) for value in line.split()]
            for line in raw.decode().splitlines()
            if line.strip()
        ]
        if len(rows) != 960 or any(len(row) != 52 for row in rows):
            raise ValueError("TEP requires 960 x 52 matrices: " + str(file))
        # Fault streams' first 160 rows are normal and cannot receive fault labels.
        for part, begin, end in (
            ("train", 0 if label == 0 else 160, 640),
            ("validation", 640, 800),
            ("test", 800, 960),
        ):
            x, y = splits[part]
            x.extend(rows[begin:end])
            y.extend([label] * (end - begin))
    return {name: (torch.tensor(x), torch.tensor(y)) for name, (x, y) in splits.items()}, hashes


def diagnosis_experiment(output, tep_root, seed=2026, epochs=40, patience=8):
    from .reused import train_srtfd_original

    return train_srtfd_original(output, tep_root, seed, epochs, patience)


def sensor_csv(path):
    with Path(path).open(newline="", encoding="utf-8") as input_file:
        reader = csv.DictReader(input_file)
        fields = sorted(
            (f for f in reader.fieldnames if f.startswith("f") and f[1:].isdigit()),
            key=lambda name: int(name[1:]),
        )
        if not fields or fields != ["f%d" % i for i in range(len(fields))]:
            raise ValueError("sensor CSV must contain contiguous f0,...,fN columns")
        rows = list(reader)
    if not rows:
        raise ValueError("empty sensor CSV")
    x = torch.tensor([[float(row[f]) for f in fields] for row in rows])
    y = torch.tensor([int(row.get("label", -1)) for row in rows])
    return x, y


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--threads", type=int, default=2)
    sub = parser.add_subparsers(dest="command", required=True)
    demo = sub.add_parser("demo", help="run posterior ablations and real TEP continual training")
    demo.add_argument("--output", type=Path, default=Path("reused_outputs"))
    demo.add_argument("--tep-root", type=Path)
    demo.add_argument("--posterior-only", action="store_true")
    demo.add_argument("--epochs", type=int, default=40)
    demo.add_argument("--patience", type=int, default=8)
    demo.add_argument("--pecj-root", type=Path)
    demo.add_argument("--component", type=Path)
    demo.add_argument("--seed", type=int, default=2026)
    demo.add_argument("--windows", type=int, default=60)
    demo.add_argument("--vi-steps", type=int, default=32)
    export = sub.add_parser(
        "export", help="export using the same PyTorch version as target LibTorch"
    )
    export.add_argument("--model", type=Path, required=True)
    export.add_argument("--output", type=Path, required=True)
    infer = sub.add_parser("diagnose", help="run an existing neural checkpoint on sensor CSV")
    infer.add_argument("--model", type=Path, required=True)
    infer.add_argument("--input", type=Path, required=True)
    infer.add_argument("--output", type=Path, required=True)
    train = sub.add_parser(
        "train", help="initial or resumed continual training from labeled sensor CSV"
    )
    train.add_argument("--input", type=Path, required=True)
    train.add_argument("--resume", type=Path)
    train.add_argument("--classes", type=int)
    train.add_argument("--steps", type=int, default=64)
    train.add_argument("--output", type=Path, required=True)
    join = sub.add_parser("join", help="posterior Join of events visible at an arrival cutoff")
    join.add_argument("--input", type=Path, required=True)
    join.add_argument("--model", type=Path)
    join.add_argument("--keys", type=int, default=8)
    join.add_argument("--mode", choices=("ema", "vi"), default="vi")
    join.add_argument("--start-us", type=int, required=True)
    join.add_argument("--end-us", type=int, required=True)
    join.add_argument("--cutoff-us", type=int, required=True)
    join.add_argument("--output", type=Path, required=True)
    pecj = sub.add_parser(
        "pecj-demo", help="run the original PECJ neural models and native AI operator"
    )
    pecj.add_argument("--pecj-root", type=Path)
    pecj.add_argument("--component", type=Path)
    pecj.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    if args.threads <= 0:
        parser.error("threads must be positive")
    torch.set_num_threads(args.threads)
    if args.command == "demo":
        if min(args.epochs, args.patience) <= 0:
            parser.error("epochs and patience must be positive")
        if not args.posterior_only and args.tep_root is None:
            parser.error("--tep-root is required unless --posterior-only is specified")
        args.output.mkdir(parents=True, exist_ok=True)
        if args.posterior_only:
            posterior_experiment(
                args.output / "analytic_baselines", args.seed, args.windows, args.vi_steps
            )
        else:
            from .reused import pecj_original_demo

            pecj_original_demo(args.output / "pecj", args.pecj_root, args.component)
            diagnosis_experiment(
                args.output / "diagnosis", args.tep_root, args.seed, args.epochs, args.patience
            )
    elif args.command == "export":
        args.output.parent.mkdir(parents=True, exist_ok=True)
        SRTFDLearner.load(args.model).export_torchscript(args.output)
        print("Exported with torch %s: %s" % (torch.__version__, args.output))
    elif args.command == "diagnose":
        learner = SRTFDLearner.load(args.model)
        x, _ = sensor_csv(args.input)
        prediction = learner.predict(x)
        args.output.parent.mkdir(parents=True, exist_ok=True)
        write_csv(
            args.output,
            [
                dict(
                    sample=i,
                    predicted_class=int(prediction["class"][i]),
                    confidence=float(prediction["confidence"][i]),
                    anomaly_score=float(prediction["anomaly_score"][i]),
                )
                for i in range(len(x))
            ],
        )
    elif args.command == "pecj-demo":
        from .reused import pecj_original_demo

        pecj_original_demo(args.output, args.pecj_root, args.component)
    elif args.command == "join":
        engine = (
            PDAJoinEngine.load(args.model)
            if args.model
            else PDAJoinEngine(PDAConfig(keys=args.keys, mode=args.mode))
        )
        with args.input.open(newline="", encoding="utf-8") as input_file:
            records = [
                Event(
                    row["stream"],
                    int(row["event_us"]),
                    int(row["arrival_us"]),
                    int(row["key"]),
                    float(row.get("value", 0)),
                )
                for row in csv.DictReader(input_file)
                if int(row["arrival_us"]) <= args.cutoff_us
            ]
        result = engine.estimate(records, args.start_us, args.end_us, args.cutoff_us)
        args.output.parent.mkdir(parents=True, exist_ok=True)
        write_json(args.output, result)
    else:
        x, y = sensor_csv(args.input)
        if args.resume:
            learner = SRTFDLearner.load(args.resume)
        else:
            if args.classes is None:
                parser.error("--classes is required for initial training")
            learner = SRTFDLearner(DiagnosisConfig(input_dim=x.shape[1], num_classes=args.classes))
            learner.calibrate(x)
        args.output.mkdir(parents=True, exist_ok=True)
        updates = []
        for begin in range(0, len(x), learner.config.max_update_samples):
            update = learner.observe(
                x[begin : begin + learner.config.max_update_samples],
                y[begin : begin + learner.config.max_update_samples],
                args.steps,
            )
            updates.append({k: v for k, v in update.items() if k != "prediction_before_update"})
        learner.save(args.output / "srtfd.checkpoint")
        learner.export_torchscript(args.output / "srtfd.ts")
        write_json(args.output / "training.json", updates)


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError, RuntimeError) as error:
        print("Research engine failed: " + str(error), file=sys.stderr)
        sys.exit(1)
