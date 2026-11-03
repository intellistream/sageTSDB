"""Application integration of causal posterior Join and neural diagnosis."""

from dataclasses import asdict

from .posterior import StreamingPDA


class SageResultSink:
    """Persist results through the existing Python TimeSeriesDB.add API.

    Python database timestamps use milliseconds; exact microsecond boundaries
    and the full posterior payload remain in string fields.
    """

    def __init__(self, database):
        self.database = database

    def __call__(self, result):
        import json

        timestamp = result["cutoff_us"] // 1000
        self.database.add(
            timestamp=timestamp,
            value=result["join"]["posterior_join_mean"],
            tags={"operator": "pda", "mode": result["join"]["mode"]},
            fields={
                "payload": json.dumps(result, allow_nan=False),
                "cutoff_us": str(result["cutoff_us"]),
            },
        )
        if result["diagnosis"] is not None:
            for index, predicted in enumerate(result["diagnosis"]["class"]):
                self.database.add(
                    timestamp=timestamp,
                    value=float(predicted),
                    tags={"operator": "srtfd", "sample": str(index)},
                    fields={
                        "confidence": str(result["diagnosis"]["confidence"][index]),
                        "cutoff_us": str(result["cutoff_us"]),
                    },
                )


class IntelligentStreamProcessor:
    """Compose the two engines with a caller-supplied persistent result sink.

    Sensor vectors must be prepared by the caller with its alignment/quality
    policy. Posterior count compensation does not fabricate missing sensor
    values. A low-quality window never trains or runs the diagnostic model.
    """

    def __init__(self, posterior, diagnosis, sink=None, max_events=100_000):
        self.stream = StreamingPDA(posterior, max_events)
        self.diagnosis = diagnosis
        self.sink = sink

    def ingest(self, event):
        self.stream.ingest(event)

    def execute(
        self,
        start_us,
        end_us,
        cutoff_us,
        sensor_features=None,
        labels=None,
        quality_ok=True,
        train=False,
    ):
        result = {
            "start_us": start_us,
            "end_us": end_us,
            "cutoff_us": cutoff_us,
            "join": self.stream.estimate(start_us, end_us, cutoff_us),
            "diagnosis": None,
            "quality_ok": bool(quality_ok),
        }
        if quality_ok and sensor_features is not None:
            if train:
                update = self.diagnosis.observe(sensor_features, labels)
                prediction = update.pop("prediction_before_update")
                result["training"] = update
            else:
                prediction = self.diagnosis.predict(sensor_features)
            result["diagnosis"] = {name: value.tolist() for name, value in prediction.items()}
        if self.sink is not None:
            self.sink(result)
        return result

    def save(self, directory):
        from pathlib import Path
        from .checkpoint import save_checkpoint

        path = Path(directory)
        path.mkdir(parents=True, exist_ok=True)
        self.stream.engine.save(path / "posterior.checkpoint")
        self.diagnosis.save(path / "srtfd.checkpoint")
        save_checkpoint(
            path / "stream.checkpoint",
            {
                "format": 1,
                "max_events": self.stream.max_events,
                "clock_us": self.stream.clock_us,
                "source_watermark_us": self.stream.source_watermark_us,
                "retained_start_us": self.stream.retained_start_us,
                "events": [asdict(event) for event in self.stream.events],
            },
        )

    @classmethod
    def load(cls, directory, sink=None):
        from pathlib import Path
        from .checkpoint import load_checkpoint
        from .diagnosis import SRTFDLearner
        from .posterior import Event, PDAJoinEngine

        path = Path(directory)
        state = load_checkpoint(path / "stream.checkpoint")
        if state["format"] != 1:
            raise ValueError("unsupported streaming checkpoint")
        processor = cls(
            PDAJoinEngine.load(path / "posterior.checkpoint"),
            SRTFDLearner.load(path / "srtfd.checkpoint"),
            sink,
            state["max_events"],
        )
        for key in ("clock_us", "source_watermark_us", "retained_start_us"):
            setattr(processor.stream, key, state[key])
        processor.stream.events = [Event(**event) for event in state["events"]]
        return processor
