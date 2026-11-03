"""Locate and load upstream networks/assets without redefining their layers."""

import hashlib
import importlib.util
import os
import sys
from pathlib import Path


def repository(name, explicit=None):
    candidates = []
    if explicit:
        candidates.append(Path(explicit))
    elif os.environ.get(name + "_DIR"):
        candidates.append(Path(os.environ[name + "_DIR"]))
    else:
        for parent in Path(__file__).resolve().parents:
            candidates.extend((parent / name, parent / "share/upstream" / name))
    marker = "models/network.py" if name == "SRTFD" else "torchscripts/linearVAE"
    for candidate in candidates:
        if (candidate / marker).exists():
            return candidate.resolve()
        if name == "PECJ" and (candidate / "benchmark" / marker).exists():
            return candidate.resolve() / "benchmark"
    raise FileNotFoundError("Cannot locate upstream %s; set %s_DIR" % (name, name))


def srtfd_class(explicit=None):
    source = repository("SRTFD", explicit) / "models/network.py"
    digest = hashlib.sha256(source.read_bytes()).hexdigest()
    module_name = "_sage_upstream_srtfd_" + digest[:16]
    if module_name not in sys.modules:
        spec = importlib.util.spec_from_file_location(module_name, source)
        module = importlib.util.module_from_spec(spec)
        sys.modules[module_name] = module
        spec.loader.exec_module(module)
    return sys.modules[module_name].GCFAggMVC, {
        "repository": "SRTFD",
        "file": "models/network.py",
        "sha256": digest,
        "class": "GCFAggMVC",
    }
