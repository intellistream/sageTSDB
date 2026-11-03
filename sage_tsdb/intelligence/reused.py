"""Reproducible demos using upstream PECJ assets and the upstream SRTFD class."""

import json
import subprocess
from pathlib import Path

import torch
from torch.nn import functional as F

from .diagnosis import DiagnosisConfig, SRTFDLearner
from .pecj import PECJNeuralModels


def pecj_original_demo(output, root=None, component=None):
    from .cli import write_csv, write_json

    output = Path(output).resolve()
    output.mkdir(parents=True, exist_ok=True)
    networks = PECJNeuralModels(root)
    statistics = {}
    for kind in networks.models:
        archive = networks.directory / ("tensor_" + kind + "_x.pt")
        normalized = torch.jit.load(str(archive)).state_dict()["0"][:8]
        result = networks.predict(kind, normalized * networks.scales[kind])
        statistics[kind] = {name: tensor.tolist() for name, tensor in result.items()}
    rows = {"S": [], "R": []}
    for stream in rows:
        rows[stream] = [
            dict(
                timestamp_us=1_000_000 + i * 12 + (2 if stream == "R" else 0),
                key=i % 5,
                value=10 + i,
            )
            for i in range(1000)
        ]
        write_csv(output / (stream + ".csv"), rows[stream])
    native = {}
    if component is not None:
        executable = Path(component).resolve() / "bin/sage_tsdb_pecj"
        if not executable.is_file() and (Path(component) / "sage_tsdb_pecj").is_file():
            executable = (Path(component) / "sage_tsdb_pecj").resolve()
        if not executable.is_file():
            raise FileNotFoundError("PECJ executable missing: " + str(executable))
        for operator in ("IAWJ", "IMA", "AI"):
            command = [
                str(executable),
                "--operator",
                operator,
                "--s-file",
                str(output / "S.csv"),
                "--r-file",
                str(output / "R.csv"),
                "--model-root",
                str(networks.root),
                "--model-prefix",
                "linearVAE",
                "--window-start-us",
                "1000000",
                "--window-end-us",
                "1012000",
            ]
            if operator == "AI":
                command += ["--watermark-ms", "6"]
            run = subprocess.run(command, text=True, capture_output=True, timeout=60)
            (output / (operator + ".log")).write_text(run.stdout + run.stderr)
            if run.returncode:
                raise RuntimeError("Upstream PECJ %s failed; see %s" % (operator, output))
            begin = run.stdout.rfind('{\n  "success"')
            if begin < 0:
                raise RuntimeError("PECJ JSON result not found")
            status = json.loads(run.stdout[begin:])
            if not status["success"] or status.get("error"):
                raise RuntimeError("PECJ returned a failure/degraded backend")
            if operator == "AI" and "The estimated selectivity is" not in run.stdout:
                raise RuntimeError(
                    "PECJ AI returned without running its upstream neural estimators"
                )
            native[operator] = status
        if native["IAWJ"]["join_count"] != 200000:
            raise RuntimeError("IAWJ did not match the independent Join oracle")
    summary = {
        "networks": networks.provenance,
        "statistics": statistics,
        "native_operators": native,
        "network_definitions_added": False,
        "note": "Original PECJ linearVAE assets; their domain accuracy is not assumed.",
    }
    write_json(output / "summary.json", summary)
    return summary


def train_srtfd_original(output, tep_root, seed=2026, epochs=40, patience=8):
    from .cli import load_tep, write_csv, write_json

    output = Path(output)
    output.mkdir(parents=True, exist_ok=True)
    splits, hashes = load_tep(Path(tep_root))
    learner = SRTFDLearner(DiagnosisConfig(seed=seed, distillation_weight=0.0))
    train_x, train_y = splits["train"]
    learner.calibrate(train_x)
    # Fully labeled baseline first. No invented network and no novel-class pseudo labels.
    learner.model.seen_classes.fill_(True)
    generator = torch.Generator().manual_seed(seed)
    # Fixed random evaluation order avoids using class-grouped batches as extra context.
    for name in ("validation", "test"):
        x, y = splits[name]
        order = torch.randperm(len(y), generator=torch.Generator().manual_seed(seed + 1))
        splits[name] = x[order], y[order]
    best, best_epoch, stale, history = -1.0, 0, 0, []
    for epoch in range(1, epochs + 1):
        learner.model.train()
        order = torch.randperm(len(train_y), generator=generator)
        total_loss, count = 0.0, 0
        with torch.random.fork_rng():
            torch.manual_seed(seed + epoch)
            for begin in range(0, len(order), learner.config.batch_size):
                chosen = order[begin : begin + learner.config.batch_size]
                learner.optimizer.zero_grad()
                loss = F.cross_entropy(learner.model(train_x[chosen]), train_y[chosen])
                if not torch.isfinite(loss):
                    raise RuntimeError("non-finite loss while training upstream SRTFD")
                loss.backward()
                torch.nn.utils.clip_grad_norm_(learner.model.parameters(), 10)
                learner.optimizer.step()
                total_loss += float(loss.detach()) * len(chosen)
                count += len(chosen)
                learner.gradient_steps += 1
        learner.updates += 1
        validation = learner.evaluate(*splits["validation"])
        record = {
            "epoch": epoch,
            "train_loss": total_loss / count,
            "validation_macro_recall": validation["macro_recall"],
            "validation_normal_recall": validation["recall_by_class"]["0"],
        }
        history.append(record)
        print(
            "Original SRTFD epoch %d: loss=%.4f validation_macro=%.4f normal=%.4f"
            % (
                epoch,
                record["train_loss"],
                record["validation_macro_recall"],
                record["validation_normal_recall"],
            ),
            flush=True,
        )
        if validation["macro_recall"] > best + 1e-5:
            best, best_epoch, stale = validation["macro_recall"], epoch, 0
            learner.save(output / "best.checkpoint")
        else:
            stale += 1
        if stale >= patience:
            break
    learner = SRTFDLearner.load(output / "best.checkpoint")
    for label in range(learner.config.num_classes):
        mask = train_y == label
        learner._update_coreset(train_x[mask], train_y[mask])
    learner.model.eval()
    learner.save(output / "srtfd.checkpoint")
    learner.export_torchscript(output / "srtfd.ts")
    test = learner.evaluate(*splits["test"])
    x, y = splits["test"]
    chosen = torch.cat([(y == label).nonzero().flatten()[:2] for label in range(22)])
    predictions = learner.predict(x[chosen])
    write_csv(
        output / "inference_probe.csv",
        [
            dict(
                timestamp_us=1_000_000 + i * 1000,
                truth_class=int(y[index]),
                expected_class=int(predictions["class"][i]),
                **{"f%d" % j: value for j, value in enumerate(x[index].tolist())},
            )
            for i, index in enumerate(chosen)
        ],
    )
    write_csv(output / "history.csv", history)
    summary = {
        "network": learner.upstream,
        "network_definitions_added": False,
        "parameters": sum(p.numel() for p in learner.model.parameters()),
        "seed": seed,
        "source_sha256": hashes,
        "training": "fully supervised baseline",
        "pseudo_labels": 0,
        "epochs_run": len(history),
        "best_epoch": best_epoch,
        "validation_macro_recall": best,
        "test": test,
        "inference_batch_size": learner.config.inference_batch_size,
        "batch_semantics": "Unmodified upstream 2-D forward; fixed batches in Python and C++",
        "gradient_steps": learner.gradient_steps,
        "torch_version": str(torch.__version__),
        "split": "train normal 0:640 / fault 160:640; validation 640:800; test 800:960",
        "limitations": "Single simulation-run chronological split; not independent-factory validation.",
    }
    write_json(output / "summary.json", summary)
    return summary
