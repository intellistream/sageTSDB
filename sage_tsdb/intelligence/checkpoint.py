"""Versioned JSON metadata + tensor-only PyTorch checkpoint.

Separating metadata also supports torch 1.13's restricted weights-only loader,
which cannot unpickle Python floating-point metadata in ordinary checkpoints.
No executable Python objects or unrestricted pickle loader are used.
"""

import io
import json
import zipfile

import torch


def save_checkpoint(path, state):
    tensors = {}

    def encode(value):
        if isinstance(value, torch.Tensor):
            name = "tensor_%d" % len(tensors)
            tensors[name] = value.detach().cpu()
            return {"tensor": name}
        if isinstance(value, dict):
            return {"dict": [[encode(k), encode(v)] for k, v in value.items()]}
        if isinstance(value, (tuple, list)):
            return {"tuple" if isinstance(value, tuple) else "list": [encode(x) for x in value]}
        if value is None or isinstance(value, (str, int, float, bool)):
            return value
        raise TypeError("unsupported checkpoint metadata: " + type(value).__name__)

    manifest = json.dumps({"format": 1, "state": encode(state)}, allow_nan=False)
    buffer = io.BytesIO()
    torch.save(tensors, buffer)
    with zipfile.ZipFile(path, "w", compression=zipfile.ZIP_STORED) as archive:
        archive.writestr("manifest.json", manifest)
        archive.writestr("tensors.pt", buffer.getvalue())


def load_checkpoint(path):
    with zipfile.ZipFile(path) as archive:
        manifest = json.loads(archive.read("manifest.json"))
        if manifest["format"] != 1:
            raise ValueError("unsupported tensor checkpoint container")
        tensors = torch.load(
            io.BytesIO(archive.read("tensors.pt")), map_location="cpu", weights_only=True
        )

    def decode(value):
        if not isinstance(value, dict):
            return value
        if "tensor" in value:
            return tensors[value["tensor"]]
        if "dict" in value:
            return {decode(k): decode(v) for k, v in value["dict"]}
        if "tuple" in value:
            return tuple(decode(x) for x in value["tuple"])
        if "list" in value:
            return [decode(x) for x in value["list"]]
        raise ValueError("invalid checkpoint metadata")

    return decode(manifest["state"])
