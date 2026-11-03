"""Direct reuse of PECJ's saved TorchScript statistics networks."""

import hashlib

import torch

from .upstream import repository


class PECJNeuralModels:
    """Load original model files and scaling factors; define no learned layers."""

    def __init__(self, root=None, prefix="linearVAE"):
        if prefix != "linearVAE":
            raise ValueError("This adapter validates the original linearVAE asset set")
        self.root = repository("PECJ", root)
        self.directory = self.root / "torchscripts" / prefix
        self.models, self.scales, self.provenance = {}, {}, {}
        for kind in ("selectivity", "sRate", "rRate"):
            file = self.directory / (prefix + "_" + kind + ".pt")
            model = torch.jit.load(str(file), map_location="cpu").eval()
            self.models[kind] = model
            scale_file = self.directory / ("tensor_" + kind + "_s.pt")
            scale = float(torch.jit.load(str(scale_file)).state_dict()["0"].flatten()[0])
            if not scale > 0:
                raise ValueError("invalid upstream PECJ scaling factor")
            self.scales[kind] = scale
            self.provenance[kind] = {
                "file": "torchscripts/" + prefix + "/" + file.name,
                "sha256": hashlib.sha256(file.read_bytes()).hexdigest(),
                "scaling_sha256": hashlib.sha256(scale_file.read_bytes()).hexdigest(),
                "dimensions": list(model.getDimension()),
                "scale": scale,
            }

    def predict(self, kind, observations):
        if kind not in self.models:
            raise ValueError("unknown PECJ statistic")
        x = torch.as_tensor(observations, dtype=torch.float32)
        model = self.models[kind]
        if x.ndim != 2 or x.shape[1] != model.getDimension()[0] or not torch.isfinite(x).all():
            raise ValueError("PECJ observations must match the upstream network dimensions")
        with torch.no_grad():
            model(x / self.scales[kind])
            mean, sigma = model.getMuEstimation()
        return {"mean": mean * self.scales[kind], "sigma": sigma * self.scales[kind]}
