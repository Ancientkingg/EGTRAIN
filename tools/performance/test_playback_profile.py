import json
import tempfile
import unittest
from pathlib import Path

import playback_profile as profile


def records(*, paints=1, structural=True):
    host = {"Darwin": "macOS", "Linux": "Linux", "Windows": "Windows"}[profile.platform.system()]
    run = {
        "type": "run", "schema": 1, "trial": 1, "view": "fit", "platform": host,
        "architecture": "arm64", "qt_platform": "offscreen", "build_type": "Debug",
        "case": "Copenhagen", "requested_scenario": "default", "scenario": "baseline",
        "default_scenario": "baseline", "target_zoom": 1.0, "actual_zoom": 1.0,
        "duration_ms": 50, "delay_ms": 0, "passenger_gui": True, "tsm": False,
        "route_choice": False, "horizon": 8000, "clock": "steady_clock_ns",
        "scope": "post_startup_playback", "mode": "structural" if structural else "recorded",
    }
    totals = {
        "worker/playback_step/compute": 30,
        "worker/playback_step/snapshot_build_publish": 30,
        "worker/playback_step/snapshot_build_publish/build_gui_snapshot": 10,
        "worker/playback_step/snapshot_build_publish/mailbox_publish": 10,
        "gui/snapshot_delivery": 80,
        "gui/snapshot_delivery/timeline": 10,
        "gui/snapshot_delivery/render_frame": 60,
        "gui/snapshot_delivery/render_frame/signalling": 10,
        "gui/snapshot_delivery/render_frame/train_position": 10,
        "gui/snapshot_delivery/render_frame/platforms": 10,
        "gui/snapshot_delivery/render_frame/passenger_icons": 10,
        "gui/snapshot_delivery/render_frame/train_passenger_info": 10,
        "render/viewport_paint": 30,
        "render/viewport_paint/background": 10,
    }
    aggregates = [{
        "type": "aggregate", "path": path, "lane": profile.PARENTS[path][0],
        "parent": profile.PARENTS[path][1], "calls": 10, "total_ns": total,
        "min_ns": 1, "median_ns": 2, "p95_ns": min(10, total), "max_ns": min(10, total),
    } for path, total in totals.items()]
    completion = {
        "type": "completion", "observed_ns": 50_000_000, "start_timestep": 1, "end_timestep": 2,
        "timesteps": 2, "deliveries": 1, "rendered_updates": 1, "paints": paints,
        "clean_stop": True, "validation": "complete", "post_freeze_records": 0,
    }
    return [run, *aggregates, completion]


def set_total(record, total):
    record.update({"total_ns": total, "min_ns": 0, "median_ns": 0,
                   "p95_ns": total // record["calls"], "max_ns": total // record["calls"]})


class PlaybackProfileTests(unittest.TestCase):
    def test_optional_animation_absence_is_valid(self):
        trial = records()
        profile.validate_trial(trial, trial=1, view="fit", duration_ms=50, delay_ms=0,
                               structural=True)
        aggregates = {record["path"]: record for record in trial if record["type"] == "aggregate"}
        summary = profile.summarize([{
            "view": "fit", "trial": 1, "self_ns": profile.self_totals(trial),
            "aggregates": aggregates, "observed_ns": 50_000_000,
        }], structural=True)
        self.assertEqual(summary["views"]["fit"]["trials"], [{
            "trial": 1,
            "unobserved_optional_paths": ["gui/train_animation/value_changed"],
        }])

    def test_absent_optional_path_remains_nonqualifying_trial(self):
        trials = []
        for trial_number in (1, 2, 3):
            trial = records()
            if trial_number < 3:
                trial.insert(-1, {
                    "type": "aggregate", "path": "gui/train_animation/value_changed",
                    "lane": "gui", "parent": "", "calls": 10,
                    "total_ns": 10_000_000, "min_ns": 1_000_000,
                    "median_ns": 1_000_000, "p95_ns": 1_000_000,
                    "max_ns": 1_000_000,
                })
            aggregates = {record["path"]: record for record in trial
                          if record["type"] == "aggregate"}
            trials.append({"view": "fit", "trial": trial_number,
                           "self_ns": profile.self_totals(trial),
                           "aggregates": aggregates, "observed_ns": 50_000_000})
        summary = profile.summarize(trials, structural=True)
        row = next(row for row in summary["views"]["fit"]["rankings"]
                   if row["path"] == "gui/train_animation/value_changed")
        self.assertEqual(row["decision_qualifying_trials"], 2)
        self.assertTrue(row["decision_satisfied"])
        self.assertEqual(len(row["trial_self_totals"]), 3)
        self.assertFalse(row["trial_self_totals"][2]["observed"])
        self.assertFalse(row["trial_self_totals"][2]["meets_decision_rule"])

    def test_each_missing_mandatory_path_is_rejected(self):
        for missing in profile.MANDATORY_PATHS:
            with self.subTest(path=missing):
                bad = [record for record in records() if record.get("path") != missing]
                with self.assertRaisesRegex(ValueError, "missing, unknown, or duplicate"):
                    profile.validate_trial(bad, trial=1, view="fit", duration_ms=50,
                                           delay_ms=0, structural=True)

    def test_observed_optional_animation_remains_strictly_validated(self):
        bad = records()
        bad.insert(-1, {
            "type": "aggregate", "path": "gui/train_animation/value_changed", "lane": "worker",
            "parent": "", "calls": 1, "total_ns": 1, "min_ns": 1, "median_ns": 1,
            "p95_ns": 1, "max_ns": 1,
        })
        with self.assertRaisesRegex(ValueError, "thread lane or parent mismatch"):
            profile.validate_trial(bad, trial=1, view="fit", duration_ms=50,
                                   delay_ms=0, structural=True)

    def test_percentile_and_direct_child_self_time(self):
        self.assertEqual(profile.percentile([1, 2, 3, 100], 0.95), 100)
        trial = records()
        self.assertEqual(profile.self_totals(trial)["gui/snapshot_delivery"], 10)

    def test_forbidden_metadata_and_absolute_paths_are_rejected(self):
        bad = records()
        bad[0]["stdout"] = "text"
        with self.assertRaisesRegex(ValueError, "forbidden"):
            profile.validate_trial(bad, trial=1, view="fit", duration_ms=50, delay_ms=0, structural=True)
        bad = records()
        bad[0]["case"] = "/private/input"
        with self.assertRaisesRegex(ValueError, "absolute"):
            profile.validate_trial(bad, trial=1, view="fit", duration_ms=50, delay_ms=0, structural=True)

    def test_output_must_be_new_outside_protected_paths(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp).resolve()
            app = root / "app" / "QEGTRAIN"
            scene = root / "scene"
            app.parent.mkdir()
            app.write_text("app")
            scene.mkdir()
            output = root / "result"
            self.assertEqual(profile.prepare_output_root(output, app, scene), output)
            with self.assertRaisesRegex(ValueError, "new"):
                profile.prepare_output_root(output, app, scene)
            with self.assertRaisesRegex(ValueError, "protected"):
                profile.prepare_output_root(scene / "result", app, scene)

    def test_symlink_output_ancestor_is_rejected(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp).resolve()
            real = root / "real"
            real.mkdir()
            link = root / "link"
            link.symlink_to(real, target_is_directory=True)
            app = root / "app"
            app.write_text("app")
            scene = root / "scene"
            scene.mkdir()
            with self.assertRaisesRegex(ValueError, "symlink"):
                profile.prepare_output_root(link / "result", app, scene)

    def test_insufficient_samples_are_rejected(self):
        with self.assertRaisesRegex(ValueError, "insufficient"):
            profile.validate_trial(records(paints=0), trial=1, view="fit", duration_ms=50,
                                   delay_ms=0, structural=True)

    def test_malformed_and_incomplete_trials_are_rejected(self):
        with self.assertRaises(ValueError):
            profile.validate_trial(records()[:-1], trial=1, view="fit", duration_ms=50,
                                   delay_ms=0, structural=True)
        bad = records()
        bad[1]["p95_ns"] = -1
        with self.assertRaises(ValueError):
            profile.validate_trial(bad, trial=1, view="fit", duration_ms=50,
                                   delay_ms=0, structural=True)

    def test_bool_is_not_an_integer_and_nonfinite_zoom_is_rejected(self):
        bad = records()
        bad[0]["duration_ms"] = True
        with self.assertRaisesRegex(ValueError, "nonnegative integer"):
            profile.validate_trial(bad, trial=1, view="fit", duration_ms=50,
                                   delay_ms=0, structural=True)
        bad = records()
        bad[0]["actual_zoom"] = float("nan")
        with self.assertRaisesRegex(ValueError, "finite"):
            profile.validate_trial(bad, trial=1, view="fit", duration_ms=50,
                                   delay_ms=0, structural=True)

    def test_invalid_nesting_is_rejected_not_clamped(self):
        bad = records()
        parent = next(record for record in bad if record.get("path") == "render/viewport_paint")
        parent.update({"total_ns": 5, "min_ns": 0, "median_ns": 0, "p95_ns": 5, "max_ns": 5})
        with self.assertRaisesRegex(ValueError, "direct children exceed"):
            profile.validate_trial(bad, trial=1, view="fit", duration_ms=50,
                                   delay_ms=0, structural=True)

    def assert_lane_overrun_rejected(self, lane, totals):
        bad = records()
        aggregates = {record["path"]: record for record in bad if record["type"] == "aggregate"}
        for path, total in totals.items():
            set_total(aggregates[path], total)
        with self.assertRaisesRegex(ValueError, f"{lane} self-accounted time exceeds"):
            profile.validate_trial(bad, trial=1, view="fit", duration_ms=50,
                                   delay_ms=0, structural=True)

    def test_render_lane_100ms_is_rejected_for_50ms_window(self):
        self.assert_lane_overrun_rejected("render", {
            "render/viewport_paint": 100_000_000,
            "render/viewport_paint/background": 0,
        })

    def test_worker_lane_self_time_overrun_is_rejected(self):
        self.assert_lane_overrun_rejected("worker", {
            "worker/playback_step/compute": 60_000_000,
            "worker/playback_step/snapshot_build_publish": 60_000_000,
            "worker/playback_step/snapshot_build_publish/build_gui_snapshot": 0,
            "worker/playback_step/snapshot_build_publish/mailbox_publish": 0,
        })

    def test_gui_lane_self_time_overrun_is_rejected(self):
        self.assert_lane_overrun_rejected("gui", {
            "gui/snapshot_delivery": 100_000_000,
            "gui/snapshot_delivery/timeline": 0,
            "gui/snapshot_delivery/render_frame": 0,
            "gui/snapshot_delivery/render_frame/signalling": 0,
            "gui/snapshot_delivery/render_frame/train_position": 0,
            "gui/snapshot_delivery/render_frame/platforms": 0,
            "gui/snapshot_delivery/render_frame/passenger_icons": 0,
            "gui/snapshot_delivery/render_frame/train_passenger_info": 0,
        })

    def test_summary_keeps_views_and_trial_self_totals_separate(self):
        trials = []
        for view, multiplier in (("fit", 1), ("dense", 2)):
            trial_records = records()
            aggregates = {record["path"]: record for record in trial_records
                          if record["type"] == "aggregate"}
            self_ns = {path: value * multiplier
                       for path, value in profile.self_totals(trial_records).items()}
            trials.append({"view": view, "trial": 1, "self_ns": self_ns,
                           "aggregates": aggregates, "observed_ns": 50_000_000})
        summary = profile.summarize(trials, structural=True)
        self.assertEqual(summary["mode"], "structural")
        self.assertEqual(summary["evidence_status"], "structural only; not recorded evidence")
        self.assertEqual(set(summary["views"]), {"fit", "dense"})
        self.assertTrue(summary["dense_to_fit"])
        fit_row = summary["views"]["fit"]["rankings"][0]
        self.assertIn("trial_self_totals", fit_row)
        self.assertIn("median_per_call_p95_ns", fit_row)
        self.assertEqual(summary["views"]["fit"]["trials"][0]["unobserved_optional_paths"],
                         ["gui/train_animation/value_changed"])

    def test_recorded_summary_has_recorded_evidence_status(self):
        summary = profile.summarize([], structural=False)
        self.assertEqual(summary["mode"], "recorded")
        self.assertEqual(summary["evidence_status"], "recorded evidence")


if __name__ == "__main__":
    unittest.main()
