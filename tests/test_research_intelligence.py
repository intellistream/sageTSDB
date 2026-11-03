"""Causal/statistical contracts and genuine neural learning, CPU only."""

import copy
import math
import tempfile
import unittest
from pathlib import Path

try:
    import torch
except ImportError as error:
    raise unittest.SkipTest("optional research tests require PyTorch") from error

from sage_tsdb.intelligence import (
    DiagnosisConfig,
    Event,
    PDAConfig,
    PDAJoinEngine,
    SRTFDLearner,
    StreamingPDA,
    IntelligentStreamProcessor,
    SageResultSink,
    PECJNeuralModels,
)


torch.set_num_threads(1)


def events():
    return [
        Event(stream, i * 1000 + 100, i * 1000 + 300, i % 2)
        for stream in ("S", "R")
        for i in range(8)
    ]


class PosteriorContracts(unittest.TestCase):
    def test_invalid_times_and_nonfinite_configuration_are_rejected(self):
        with self.assertRaises(ValueError):
            Event("S", 1.5, 2, 0)
        with self.assertRaises(ValueError):
            Event("S", 1, 2, -1)
        with self.assertRaises(ValueError):
            PDAConfig(initial_delay_ms=math.nan)

    def engine(self, mode="vi", **options):
        return PDAJoinEngine(
            PDAConfig(keys=2, mode=mode, steps=24, predictive_samples=256, **options)
        )

    def test_finite_elbo_has_nonzero_finite_gradient(self):
        engine = self.engine()
        observation = engine._observations(events(), 0, 10_000, 10_000)
        loc = engine.prior_loc.clone().requires_grad_()
        scale = engine.prior_log_scale.clone().requires_grad_()
        noise = torch.zeros(8, engine.dimensions)
        elbo = engine.elbo(loc, scale, observation, noise)
        elbo.backward()
        self.assertTrue(torch.isfinite(elbo))
        self.assertTrue(torch.isfinite(loc.grad).all())
        self.assertGreater(float(loc.grad.abs().sum()), 0)

    def test_empty_complete_exposure_likelihood_matches_poisson_compensator(self):
        engine = self.engine(initial_delay_ms=0.01, initial_delay_sigma=0.1)
        observation = engine._observations([], 0, 10_000, 1_000_000)
        noise = torch.zeros(1, engine.dimensions)
        elbo = engine.elbo(engine.prior_loc, engine.prior_log_scale, observation, noise)
        self.assertAlmostEqual(float(elbo), -4 * engine.config.initial_count_per_key, places=4)

    def test_vi_optimizes_actual_elbo(self):
        result = self.engine().estimate(events(), 0, 10_000, 10_000)
        self.assertGreater(result["elbo_after"], result["elbo_before"])
        self.assertGreater(result["optimization_steps"], 0)
        self.assertTrue(all(math.isfinite(x) for x in result["posterior_loc"]))

    def test_custom_neural_posterior_modes_are_removed(self):
        for mode in ("mlp", "dual"):
            with self.assertRaises(ValueError):
                self.engine(mode)

    def test_complete_arrivals_reduce_to_exact_join(self):
        engine = self.engine("ema", initial_delay_ms=0.01, initial_delay_sigma=0.1)
        result = engine.estimate(events(), 0, 10_000, 1_000_000)
        self.assertEqual(result["observed_join"], 32)
        self.assertAlmostEqual(result["posterior_join_mean"], 32, places=5)
        self.assertEqual(result["predictive_lower"], 32)
        self.assertEqual(result["predictive_upper"], 32)

    def test_completion_never_removes_confirmed_pairs(self):
        result = self.engine().estimate(events(), 0, 10_000, 10_000)
        self.assertGreaterEqual(result["posterior_join_mean"], result["observed_join"])
        self.assertGreaterEqual(result["predictive_lower"], result["observed_join"])
        self.assertGreaterEqual(result["predictive_upper"], result["predictive_lower"])

    def test_future_arrivals_are_rejected_not_silently_used(self):
        with self.assertRaises(ValueError):
            self.engine().estimate(events() + [Event("S", 9000, 20_000, 0)], 0, 10_000, 10_000)

    def test_window_is_half_open(self):
        data = [
            Event("S", 1000, 1000, 0),
            Event("R", 1000, 1000, 0),
            Event("S", 2000, 2000, 0),
            Event("R", 2000, 2000, 0),
        ]
        result = self.engine("ema").estimate(data, 1000, 2000, 2000)
        self.assertEqual(result["observed_join"], 1)

    def test_global_prior_only_changes_after_certified_feedback(self):
        engine = self.engine()
        before = engine.prior_loc.clone()
        engine.estimate(events(), 0, 10_000, 10_000)
        self.assertTrue(torch.equal(before, engine.prior_loc))
        with self.assertRaises(ValueError):
            engine.update_complete_window(events(), 0, 10_000, 10_000, 9000)
        engine.update_complete_window(events(), 0, 10_000, 10_000, 10_000)
        self.assertFalse(torch.equal(before, engine.prior_loc))
        with self.assertRaises(ValueError):
            engine.update_complete_window(events(), 0, 10_000, 10_000, 10_000)
        with self.assertRaises(ValueError):
            engine.estimate(events(), 0, 10_000, 10_000)

    def test_stream_limits_clock_and_source_watermark(self):
        stream = StreamingPDA(self.engine("ema"), max_events=2)
        stream.ingest(Event("S", 100, 200, 0))
        with self.assertRaises(ValueError):
            stream.ingest(Event("R", 100, 150, 0))
        stream.ingest(Event("R", 100, 300, 0))
        with self.assertRaises(OverflowError):
            stream.ingest(Event("R", 200, 400, 0))
        with self.assertRaises(ValueError):
            stream.evict_before(200)
        stream.certify(200)
        stream.evict_before(200)
        with self.assertRaises(ValueError):
            stream.ingest(Event("S", 100, 400, 0))
        with self.assertRaises(ValueError):
            stream.estimate(0, 200, 400)

    def test_checkpoint_resumes_posterior_rng_and_optimizer(self):
        engine = self.engine("vi")
        engine.estimate(events(), 0, 10_000, 10_000)
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "posterior.checkpoint"
            engine.save(path)
            restored = PDAJoinEngine.load(path)
        first = engine.estimate(events(), 0, 10_000, 10_000)
        second = restored.estimate(events(), 0, 10_000, 10_000)
        for field in (
            "posterior_join_mean",
            "posterior_loc",
            "posterior_log_scale",
            "predictive_lower",
            "predictive_upper",
            "mode",
            "elbo_after",
        ):
            self.assertEqual(first[field], second[field], field)


class UpstreamPECJContracts(unittest.TestCase):
    def test_saved_pecj_assets_are_called_without_new_layers(self):
        import hashlib
        from sage_tsdb.intelligence.upstream import repository

        adapter = PECJNeuralModels()
        for kind, module in adapter.models.items():
            raw = torch.ones(2, module.getDimension()[0]) * adapter.scales[kind]
            result = adapter.predict(kind, raw)
            with torch.no_grad():
                module(raw / adapter.scales[kind])
                expected, _ = module.getMuEstimation()
            self.assertTrue(torch.allclose(result["mean"], expected * adapter.scales[kind]))
            file = repository("PECJ") / adapter.provenance[kind]["file"]
            self.assertEqual(
                hashlib.sha256(file.read_bytes()).hexdigest(), adapter.provenance[kind]["sha256"]
            )

    def test_no_new_neural_layer_definitions_in_integration(self):
        import ast

        root = Path(__file__).parents[1] / "sage_tsdb/intelligence"
        prohibited = {
            "Linear",
            "TransformerEncoderLayer",
            "TransformerEncoder",
            "Conv1d",
            "LSTM",
            "GRU",
        }
        for path in root.glob("*.py"):
            for node in ast.walk(ast.parse(path.read_text())):
                if isinstance(node, ast.Call) and isinstance(node.func, ast.Attribute):
                    self.assertNotIn(node.func.attr, prohibited, str(path))


class NeuralContracts(unittest.TestCase):
    def learner(self):
        return SRTFDLearner(
            DiagnosisConfig(
                input_dim=4,
                num_classes=3,
                memory_per_class=6,
                learning_rate=0.001,
            )
        )

    def data(self):
        generator = torch.Generator().manual_seed(123)
        x = torch.randn(72, 4, generator=generator) * 0.12
        y = torch.arange(3).repeat_interleave(24)
        x[y == 0] += torch.tensor([-3.0, -3.0, 0.0, 0.0])
        x[y == 1] += torch.tensor([3.0, -3.0, 0.0, 0.0])
        x[y == 2] += torch.tensor([0.0, 3.0, 2.0, 2.0])
        return x, y

    def trained(self):
        learner = self.learner()
        x, y = self.data()
        learner.calibrate(x[y < 2])
        learner.observe(x[y < 2], y[y < 2], steps=80)
        return learner, x, y

    def test_real_neural_weights_learn_a_separable_fault(self):
        learner, x, y = self.trained()
        score = learner.evaluate(x[y < 2], y[y < 2])
        self.assertGreater(score["accuracy"], 0.9)
        self.assertEqual(learner.gradient_steps, 80)

    def test_new_fault_trains_without_erasing_old_replay(self):
        learner, x, y = self.trained()
        update = learner.observe(x[y == 2], y[y == 2], steps=100)
        self.assertFalse((update["prediction_before_update"]["class"] == 2).any())
        self.assertGreater(learner.evaluate(x, y)["accuracy"], 0.9)
        self.assertEqual(set(learner.memory), {0, 1, 2})
        self.assertTrue(all(len(points) <= 6 for points in learner.memory.values()))

    def test_parameters_and_forward_are_the_original_srtfd_network(self):
        from sage_tsdb.intelligence.upstream import srtfd_class

        learner, x, _ = self.trained()
        self.assertIs(learner.model.network.__class__, srtfd_class()[0])
        self.assertEqual(
            sum(p.numel() for p in learner.model.parameters()),
            sum(p.numel() for p in learner.model.network.parameters()),
        )
        learner.model.eval()
        with torch.no_grad():
            expected = learner.model.network(learner.model.normalized(x))
            expected = expected.masked_fill(~learner.model.seen_classes, -1e9).softmax(-1)
        self.assertTrue(torch.allclose(expected, learner.predict(x)["probabilities"], atol=1e-5))

    def test_new_class_batch_cannot_generate_wrong_old_class_pseudo_labels(self):
        learner, x, y = self.trained()
        labels = y[y == 2].clone()
        labels[::2] = -1
        update = learner.observe(x[y == 2], labels, steps=2, use_pseudo=True)
        self.assertEqual(update["pseudo"], 0)

    def test_unlabeled_data_cannot_create_new_class_or_trusted_memory(self):
        learner, x, y = self.trained()
        before = copy.deepcopy(learner.memory)
        learner.observe(x[y == 2], use_pseudo=False)
        self.assertFalse(bool(learner.model.seen_classes[2]))
        self.assertEqual(set(before), set(learner.memory))
        self.assertTrue(all(torch.equal(before[k], learner.memory[k]) for k in before))

    def test_evaluation_changes_neither_weights_memory_nor_normalization(self):
        learner, x, y = self.trained()
        before = copy.deepcopy(learner.model.state_dict())
        memory = copy.deepcopy(learner.memory)
        steps = learner.gradient_steps
        learner.evaluate(x, y)
        self.assertTrue(
            all(torch.equal(v, learner.model.state_dict()[k]) for k, v in before.items())
        )
        self.assertTrue(all(torch.equal(v, learner.memory[k]) for k, v in memory.items()))
        self.assertEqual(steps, learner.gradient_steps)
        with self.assertRaises(ValueError):
            learner.calibrate(x)

    def test_corrupt_features_and_fractional_labels_fail_before_training(self):
        learner, x, y = self.trained()
        with self.assertRaises(ValueError):
            learner.observe([[math.nan, 0, 0, 0]], [0])
        with self.assertRaises(ValueError):
            learner.observe(x[:1], [1.5])
        with self.assertRaises(ValueError):
            learner.predict(torch.zeros(2, 5))

    def test_checkpoint_resumes_neural_training_exactly(self):
        learner, x, y = self.trained()
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "model.checkpoint"
            learner.save(path)
            restored = SRTFDLearner.load(path)
        learner.observe(x[y == 2], y[y == 2], steps=5)
        restored.observe(x[y == 2], y[y == 2], steps=5)
        self.assertTrue(
            all(
                torch.equal(v, restored.model.state_dict()[k])
                for k, v in learner.model.state_dict().items()
            )
        )
        self.assertEqual(learner.gradient_steps, restored.gradient_steps)

    def test_script_export_matches_eager_probabilities(self):
        learner, x, _ = self.trained()
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "srtfd.ts"
            learner.export_torchscript(path)
            scripted = torch.jit.load(str(path)).eval()
            with torch.no_grad():
                exported = scripted(x).softmax(-1)
        self.assertTrue(torch.allclose(exported, learner.predict(x)["probabilities"], atol=1e-5))

    def test_pipeline_quality_gate_and_database_persistence(self):
        from sage_tsdb.core import TimeSeriesDB

        learner, x, y = self.trained()
        database = TimeSeriesDB()
        processor = IntelligentStreamProcessor(
            PDAJoinEngine(PDAConfig(keys=2, mode="ema")), learner, SageResultSink(database)
        )
        for event in sorted(events(), key=lambda e: e.arrival_us):
            processor.ingest(event)
        updates = learner.updates
        rejected = processor.execute(0, 10_000, 10_000, x, y, quality_ok=False, train=True)
        self.assertIsNone(rejected["diagnosis"])
        self.assertEqual(updates, learner.updates)
        result = processor.execute(0, 10_000, 10_000, x[:3])
        self.assertEqual(len(result["diagnosis"]["class"]), 3)
        self.assertEqual(database.size, 5)  # two Join snapshots and three neural results

    def test_pipeline_checkpoint_keeps_inflight_events_and_clocks(self):
        learner, _, _ = self.trained()
        processor = IntelligentStreamProcessor(
            PDAJoinEngine(PDAConfig(keys=2, mode="ema")), learner
        )
        for event in sorted(events(), key=lambda e: e.arrival_us):
            processor.ingest(event)
        with tempfile.TemporaryDirectory() as directory:
            processor.save(directory)
            restored = IntelligentStreamProcessor.load(directory)
        self.assertEqual(processor.stream.events, restored.stream.events)
        self.assertEqual(processor.stream.clock_us, restored.stream.clock_us)
        self.assertEqual(
            processor.execute(0, 10_000, 10_000)["join"]["posterior_join_mean"],
            restored.execute(0, 10_000, 10_000)["join"]["posterior_join_mean"],
        )


if __name__ == "__main__":
    unittest.main()
