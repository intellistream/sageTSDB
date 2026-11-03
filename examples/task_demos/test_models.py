"""Causality, bounded memory, data quality and optional real-component checks."""

import copy
import os
import tempfile
import unittest
from collections import Counter
from pathlib import Path

from common import PecjCLI, decode_status, exact_pairs
from models import OnlineModes, ReplayDiagnosis, WarningDetector
from run_demos import heldout_scores, matched_features


class ModelContracts(unittest.TestCase):
    def test_new_label_cannot_change_the_prediction_already_scored(self):
        learner = ReplayDiagnosis()
        learner.learn((0, 0), 0)
        learner.learn((4, 0), 1)
        predicted, source = learner.observe((0, 4), revealed_label=2)
        self.assertNotEqual(predicted, 2)
        self.assertEqual(source, "label")
        self.assertEqual(learner.predict((0, 4))[0], 2)

    def test_unrecognized_cluster_cannot_self_label_as_new_fault(self):
        learner = ReplayDiagnosis()
        learner.learn((0, 0), 0)
        learner.learn((4, 0), 1)
        before = copy.deepcopy(learner.memory)
        self.assertEqual(learner.observe((0, 4))[1], "none")
        self.assertEqual(before, learner.memory)

    def test_replay_keeps_rare_class_within_memory_budget(self):
        balanced, recent = ReplayDiagnosis(), ReplayDiagnosis(balanced=False)
        for model in (balanced, recent):
            model.learn((4, 0), 1)
            for i in range(200):
                model.learn((i * 0.001, 0), 0)
                model.learn((0, 4 + i * 0.001), 2)
                self.assertLessEqual(len(model.memory), 36)
        counts = Counter(label for _, label in balanced.memory)
        self.assertIn(1, counts)
        self.assertTrue(all(n <= 12 for n in counts.values()))
        self.assertNotIn(1, {label for _, label in recent.memory})

    def test_heldout_evaluation_does_not_train(self):
        learner = ReplayDiagnosis()
        learner.learn((0, 0), 0)
        before = copy.deepcopy(learner.memory)
        heldout_scores({"test": learner}, seed=123, shift=0.3)
        self.assertEqual(before, learner.memory)

    def test_spikes_do_not_move_normal_mode_or_create_clusters(self):
        modes = OnlineModes((0, 0))
        for _ in range(30):
            modes.observe((9, 9))
            modes.observe((0, 0))
        self.assertEqual(modes.modes, {0: (0, 0)})

    def test_new_mode_is_created_after_scoring_and_reuses_old_id(self):
        modes = OnlineModes((0, 0), patience=3)
        for _ in range(2):
            self.assertIsNone(modes.observe((3, 3))[2])
        score, old_mode, created = modes.observe((3, 3))
        self.assertGreater(score, 1)
        self.assertEqual(old_mode, 0)
        self.assertEqual(created, 1)
        self.assertEqual(modes.observe((0, 0))[1], 0)
        self.assertEqual(modes.observe((3, 3))[1], 1)

    def test_mode_capacity_is_bounded(self):
        modes = OnlineModes((0, 0), patience=2, max_modes=2)
        for shift in (4, 8, 12):
            modes.observe((shift, shift))
            modes.observe((shift, shift))
            self.assertLessEqual(len(modes.modes), 2)
        self.assertEqual(set(modes.modes), {2, 3})

    def test_missing_window_breaks_warning_streak(self):
        detector = WarningDetector(warmup=2)
        detector.observe((0, 0))
        detector.observe((0, 0))
        self.assertFalse(detector.observe((9, 9))[1])
        self.assertEqual(detector.observe(None), (None, False))
        self.assertFalse(detector.observe((9, 9))[1])
        self.assertTrue(detector.observe((9, 9))[1])

    def test_alignment_rejects_many_to_many_sensor_features(self):
        with self.assertRaises(ValueError):
            matched_features([{"key": 1, "value": 10}] * 2,
                             [{"key": 1, "value": 2}])

    def test_status_is_parsed_despite_pecj_logging(self):
        status = decode_status('INFO {not json}\n{"success": true, "join_count": 4}\n')
        self.assertEqual(status["join_count"], 4)
        with self.assertRaises(RuntimeError):
            decode_status("INFO only: no result")


@unittest.skipUnless(os.environ.get("PECJ_COMPONENT"), "set PECJ_COMPONENT for real CLI checks")
class RealComponentContracts(unittest.TestCase):
    def test_half_open_window_and_many_to_many_join(self):
        cli = PecjCLI(os.environ["PECJ_COMPONENT"])
        s = [{"timestamp_us": t, "key": 7, "value": 10}
             for t in (999_999, 1_000_000, 1_000_005, 1_000_010)]
        r = [{"timestamp_us": t, "key": 7, "value": 20}
             for t in (1_000_001, 1_000_004, 1_000_010)]
        with tempfile.TemporaryDirectory() as temporary:
            status = cli.execute(Path(temporary), s, r, 1_000_000, 1_000_010)
        self.assertEqual(status["input_s_count"], 2)
        self.assertEqual(status["input_r_count"], 2)
        self.assertEqual(status["join_count"], exact_pairs(s[1:3], r[:2]))


if __name__ == "__main__":
    unittest.main()
