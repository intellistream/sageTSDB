"""Artifacts and the real PECJ CLI adapter (Python standard library only)."""

import csv
import json
import math
import subprocess
import time
from collections import Counter
from pathlib import Path


def write_json(path, value):
    Path(path).write_text(json.dumps(value, ensure_ascii=False, indent=2,
                                    allow_nan=False) + "\n", encoding="utf-8")


def write_csv(path, rows, fields=None):
    rows = list(rows)
    with Path(path).open("w", newline="", encoding="utf-8") as output:
        writer = csv.DictWriter(output, fieldnames=fields or list(rows[0]))
        writer.writeheader()
        writer.writerows(rows)


def exact_pairs(s, r):
    left = Counter(p["key"] for p in s)
    right = Counter(p["key"] for p in r)
    return sum(count * right[key] for key, count in left.items())


def binary_metrics(truth, predicted):
    tp = sum(t and p for t, p in zip(truth, predicted))
    fp = sum(not t and p for t, p in zip(truth, predicted))
    fn = sum(t and not p for t, p in zip(truth, predicted))
    negatives = sum(not t for t in truth)
    return {"tp": tp, "fp": fp, "fn": fn,
            "precision": tp / (tp + fp) if tp + fp else 0.0,
            "recall": tp / (tp + fn) if tp + fn else 0.0,
            "false_positive_rate": fp / negatives if negatives else 0.0}


def decode_status(output):
    # The shipped CLI writes PECJ log lines before its final JSON object.
    decoder = json.JSONDecoder()
    for i, ch in enumerate(output):
        if ch != "{":
            continue
        try:
            value, _ = decoder.raw_decode(output[i:])
        except json.JSONDecodeError:
            continue
        if isinstance(value, dict) and "success" in value and "join_count" in value:
            return value
    raise RuntimeError("PECJ did not return ComputeStatus JSON")


class PecjCLI:
    def __init__(self, component):
        self.executable = Path(component).resolve() / "bin" / "sage_tsdb_pecj"
        if not self.executable.is_file():
            raise FileNotFoundError("PECJ CLI not found: " + str(self.executable))

    def execute(self, directory, s, r, start, end, window_id=1):
        directory = Path(directory).resolve()
        directory.mkdir(parents=True, exist_ok=True)
        fields = ["timestamp_us", "key", "value"]
        for name, records in (("s", s), ("r", r)):
            write_csv(directory / (name + ".csv"),
                      ({k: p[k] for k in fields} for p in records), fields)
        command = [str(self.executable), "--s-file", str(directory / "s.csv"),
                   "--r-file", str(directory / "r.csv"), "--operator", "IAWJ",
                   "--window-start-us", str(start), "--window-end-us", str(end),
                   "--window-id", str(window_id)]
        began = time.perf_counter()
        completed = subprocess.run(command, cwd=directory, text=True,
                                   capture_output=True, timeout=60)
        elapsed_ms = (time.perf_counter() - began) * 1000
        (directory / "pecj.log").write_text(completed.stdout + completed.stderr,
                                           encoding="utf-8")
        if completed.returncode:
            raise RuntimeError("PECJ exited %d; see %s" %
                               (completed.returncode, directory / "pecj.log"))
        status = decode_status(completed.stdout)
        if not status["success"] or status.get("error"):
            raise RuntimeError("PECJ failure or degraded backend: " + str(status))
        for field in ("join_count", "input_s_count", "input_r_count"):
            if not isinstance(status[field], int) or status[field] < 0:
                raise RuntimeError("Invalid PECJ count: " + field)
        expected_counts = tuple(sum(start <= p["timestamp_us"] < end for p in records)
                                for records in (s, r))
        if (status["input_s_count"], status["input_r_count"]) != expected_counts:
            raise RuntimeError("PECJ input counts disagree with the supplied window")
        if not math.isfinite(status["computation_time_ms"]):
            raise RuntimeError("Invalid PECJ computation time")
        status["cli_wall_time_ms"] = elapsed_ms
        write_json(directory / "status.json", status)
        return status
