#!/usr/bin/env python3
"""Run and validate opt-in EGTRAIN GUI playback profiling trials."""

import argparse
import json
import math
import os
import platform
import statistics
import subprocess
import tempfile
from pathlib import Path

PREFIX = "QEGTRAIN_PLAYBACK_PROFILE "
REPO_ROOT = Path(__file__).resolve().parents[2]
PATHS = {
    "worker/playback_step/compute",
    "worker/playback_step/snapshot_build_publish",
    "worker/playback_step/snapshot_build_publish/build_gui_snapshot",
    "worker/playback_step/snapshot_build_publish/mailbox_publish",
    "gui/snapshot_delivery",
    "gui/snapshot_delivery/timeline",
    "gui/snapshot_delivery/render_frame",
    "gui/snapshot_delivery/render_frame/signalling",
    "gui/snapshot_delivery/render_frame/train_position",
    "gui/snapshot_delivery/render_frame/platforms",
    "gui/snapshot_delivery/render_frame/passenger_icons",
    "gui/snapshot_delivery/render_frame/train_passenger_info",
    "gui/train_animation/value_changed",
    "render/viewport_paint",
    "render/viewport_paint/background",
}
PARENTS = {
    "worker/playback_step/compute": ("worker", ""),
    "worker/playback_step/snapshot_build_publish": ("worker", ""),
    "worker/playback_step/snapshot_build_publish/build_gui_snapshot":
        ("worker", "worker/playback_step/snapshot_build_publish"),
    "worker/playback_step/snapshot_build_publish/mailbox_publish":
        ("worker", "worker/playback_step/snapshot_build_publish"),
    "gui/snapshot_delivery": ("gui", ""),
    "gui/snapshot_delivery/timeline": ("gui", "gui/snapshot_delivery"),
    "gui/snapshot_delivery/render_frame": ("gui", "gui/snapshot_delivery"),
    "gui/snapshot_delivery/render_frame/signalling": ("gui", "gui/snapshot_delivery/render_frame"),
    "gui/snapshot_delivery/render_frame/train_position": ("gui", "gui/snapshot_delivery/render_frame"),
    "gui/snapshot_delivery/render_frame/platforms": ("gui", "gui/snapshot_delivery/render_frame"),
    "gui/snapshot_delivery/render_frame/passenger_icons": ("gui", "gui/snapshot_delivery/render_frame"),
    "gui/snapshot_delivery/render_frame/train_passenger_info": ("gui", "gui/snapshot_delivery/render_frame"),
    "gui/train_animation/value_changed": ("gui", ""),
    "render/viewport_paint": ("render", ""),
    "render/viewport_paint/background": ("render", "render/viewport_paint"),
}
RUN_KEYS = {
    "type", "schema", "trial", "view", "platform", "architecture", "qt_platform",
    "build_type", "case", "requested_scenario", "scenario", "default_scenario",
    "target_zoom", "actual_zoom", "duration_ms", "delay_ms", "passenger_gui", "tsm",
    "route_choice", "horizon", "clock", "scope", "mode",
}
AGGREGATE_KEYS = {
    "type", "path", "lane", "parent", "calls", "total_ns", "min_ns", "median_ns", "p95_ns", "max_ns",
}
COMPLETION_KEYS = {
    "type", "observed_ns", "start_timestep", "end_timestep", "timesteps", "deliveries",
    "rendered_updates", "paints", "clean_stop", "validation", "post_freeze_records",
}
FORBIDDEN_KEYS = {
    "environment", "env", "stdout", "stderr", "app_path", "scene_path", "executable",
    "binary", "binary_id", "binary_size", "hash", "checksum", "digest", "commit",
    "commit_id", "timestamp", "wall_clock", "command",
}
INTEGER_KEYS = {
    "schema", "trial", "duration_ms", "delay_ms", "horizon", "calls", "total_ns", "min_ns",
    "median_ns", "p95_ns", "max_ns", "observed_ns", "start_timestep", "end_timestep",
    "timesteps", "deliveries", "rendered_updates", "paints", "post_freeze_records",
}
BOOLEAN_KEYS = {"passenger_gui", "tsm", "route_choice", "clean_stop"}
FLOAT_KEYS = {"target_zoom", "actual_zoom"}
STRING_KEYS = (RUN_KEYS | AGGREGATE_KEYS | COMPLETION_KEYS) - INTEGER_KEYS - BOOLEAN_KEYS - FLOAT_KEYS
OPTIONAL_PATHS = {"gui/train_animation/value_changed"}
MANDATORY_PATHS = PATHS - OPTIONAL_PATHS


def percentile(values: list[int], fraction: float) -> int:
    if not values:
        return 0
    ordered = sorted(values)
    return ordered[min(len(ordered) - 1, int((len(ordered) - 1) * fraction + 0.5))]


def paths_overlap(left: Path, right: Path) -> bool:
    return left == right or left in right.parents or right in left.parents


def prepare_output_root(requested: Path, app: Path, scene: Path) -> Path:
    output = Path(os.path.abspath(os.fspath(requested.expanduser())))
    for component in (output, *output.parents):
        if os.path.lexists(component) and component.is_symlink():
            raise ValueError("output path has a symlink ancestor")
    if os.path.lexists(output):
        raise ValueError("output path must be new")
    if not output.parent.is_dir():
        raise ValueError("output parent does not exist")
    resolved = output.resolve(strict=False)
    for forbidden in (REPO_ROOT.resolve(), app.resolve(), scene.resolve()):
        if paths_overlap(resolved, forbidden):
            raise ValueError("output path overlaps a protected location")
    resolved.mkdir()
    return resolved


def _absolute_string(value: str) -> bool:
    return value.startswith(("/", "\\")) or (len(value) > 2 and value[1:3] in (":/", ":\\"))


def reject_forbidden(record: dict) -> None:
    for key, value in record.items():
        lowered = key.lower()
        if lowered in FORBIDDEN_KEYS or any(
            token in lowered for token in ("timestamp", "checksum", "digest", "commit", "hash")
        ):
            raise ValueError(f"forbidden metadata key: {key}")
        if isinstance(value, str) and _absolute_string(value):
            raise ValueError(f"absolute persisted path in {key}")


def validate_trial(records: list[dict], *, trial: int, view: str, duration_ms: int,
                   delay_ms: int, structural: bool, requested_scenario: str = "default",
                   expected_qt_platform: str | None = None) -> None:
    if not records or any(type(record) is not dict for record in records):
        raise ValueError("records must be JSON objects")
    for record in records:
        reject_forbidden(record)
        record_type = record.get("type")
        expected = {"run": RUN_KEYS, "aggregate": AGGREGATE_KEYS, "completion": COMPLETION_KEYS}.get(record_type)
        if expected is None or set(record) != expected:
            raise ValueError("unknown record type or keys")
        for key in INTEGER_KEYS & record.keys():
            if type(record[key]) is not int or record[key] < 0:
                raise ValueError(f"{key} must be a nonnegative integer")
        for key in BOOLEAN_KEYS & record.keys():
            if type(record[key]) is not bool:
                raise ValueError(f"{key} must be a boolean")
        for key in FLOAT_KEYS & record.keys():
            if type(record[key]) is not float or not math.isfinite(record[key]):
                raise ValueError(f"{key} must be a finite number")
        for key in STRING_KEYS & record.keys():
            if type(record[key]) is not str:
                raise ValueError(f"{key} must be a string")

    runs = [record for record in records if record["type"] == "run"]
    completions = [record for record in records if record["type"] == "completion"]
    aggregates = [record for record in records if record["type"] == "aggregate"]
    if len(runs) != 1 or len(completions) != 1:
        raise ValueError("trial needs exactly one run and one completion record")
    run, completion = runs[0], completions[0]
    target = 3.0 if view == "dense" else 1.0
    if view not in {"fit", "dense"} or run["schema"] != 1 or run["trial"] != trial or run["view"] != view:
        raise ValueError("trial identity mismatch")
    if run["case"] != "Copenhagen" or requested_scenario not in {"default", "baseline"}:
        raise ValueError("case or requested scenario mismatch")
    expected_scenario = run["default_scenario"] if requested_scenario == "default" else requested_scenario
    if (run["requested_scenario"] != requested_scenario or run["default_scenario"] != "baseline"
            or run["scenario"] != expected_scenario):
        raise ValueError("requested, selected, or default scenario mismatch")
    if run["duration_ms"] != duration_ms or run["delay_ms"] != delay_ms or run["horizon"] != 8000:
        raise ValueError("playback settings mismatch")
    if run["target_zoom"] != target or abs(run["actual_zoom"] - target) > 0.02:
        raise ValueError("view zoom mismatch")
    if run["clock"] != "steady_clock_ns" or run["scope"] != "post_startup_playback":
        raise ValueError("measurement boundary mismatch")
    if run["mode"] != ("structural" if structural else "recorded"):
        raise ValueError("evidence mode mismatch")
    host_platform = {"Darwin": "macOS", "Linux": "Linux", "Windows": "Windows"}.get(platform.system())
    if run["platform"] != host_platform or run["architecture"] not in {"arm64", "x86_64", "x86", "i386"}:
        raise ValueError("platform or architecture mismatch")
    if run["qt_platform"] not in {"cocoa", "offscreen", "xcb", "wayland", "windows", "minimal"}:
        raise ValueError("unknown Qt platform")
    if expected_qt_platform is not None and run["qt_platform"] != expected_qt_platform:
        raise ValueError("Qt platform does not match the request")
    if run["build_type"] not in {"Debug", "Release", "RelWithDebInfo", "MinSizeRel"}:
        raise ValueError("unknown build type")
    if not run["passenger_gui"] or run["tsm"] or run["route_choice"]:
        raise ValueError("recorded playback modes mismatch")
    if not structural:
        if platform.system() != "Darwin" or run["platform"] != "macOS" or run["qt_platform"] != "cocoa":
            raise ValueError("recorded evidence requires native macOS Cocoa")
        if run["build_type"] != "Release" or duration_ms != 30000 or delay_ms != 250:
            raise ValueError("recorded protocol settings mismatch")

    aggregate_paths = [record["path"] for record in aggregates]
    aggregates_by_path = {record["path"]: record for record in aggregates}
    path_set = set(aggregate_paths)
    if (len(aggregate_paths) != len(path_set) or not path_set <= PATHS
            or not MANDATORY_PATHS <= path_set):
        raise ValueError("missing, unknown, or duplicate aggregate path")
    for aggregate in aggregates:
        if (aggregate["lane"], aggregate["parent"]) != PARENTS[aggregate["path"]]:
            raise ValueError("aggregate thread lane or parent mismatch")
        if not (aggregate["min_ns"] <= aggregate["median_ns"] <= aggregate["p95_ns"] <= aggregate["max_ns"]):
            raise ValueError("invalid aggregate percentiles")
        if (aggregate["calls"] < 1
                or not aggregate["calls"] * aggregate["min_ns"] <= aggregate["total_ns"]
                <= aggregate["calls"] * aggregate["max_ns"]):
            raise ValueError("invalid aggregate totals")

    minimum = 1 if structural else 100
    if completion["start_timestep"] >= completion["end_timestep"] or completion["timesteps"] < minimum:
        raise ValueError("timestep range or sample count did not advance")
    if any(completion[key] < minimum for key in ("deliveries", "rendered_updates", "paints")):
        raise ValueError("insufficient recorded samples")
    lower_ns = int(duration_ms * 1_000_000 * 0.95)
    upper_ns = duration_ms * 1_000_000 + max(1_000_000_000, duration_ms * 50_000)
    if not lower_ns <= completion["observed_ns"] <= upper_ns:
        raise ValueError("observed window is inconsistent with requested duration")
    if completion["clean_stop"] is not True or completion["validation"] != "complete":
        raise ValueError("trial did not stop cleanly")
    if completion["post_freeze_records"] != 0:
        raise ValueError("records were added after freeze")
    totals = self_totals(records)
    for lane in {aggregate["lane"] for aggregate in aggregates}:
        accounted = sum(total for path, total in totals.items() if aggregates_by_path[path]["lane"] == lane)
        if accounted > completion["observed_ns"]:
            raise ValueError(f"{lane} self-accounted time exceeds observed window")


def self_totals(trial_records: list[dict]) -> dict[str, int]:
    aggregates = {record["path"]: record for record in trial_records if record["type"] == "aggregate"}
    totals = {}
    for path, record in aggregates.items():
        children = sum(child["total_ns"] for child in aggregates.values() if child["parent"] == path)
        if children > record["total_ns"]:
            raise ValueError(f"invalid nesting: direct children exceed inclusive total for {path}")
        totals[path] = record["total_ns"] - children
    return totals


def summarize(trials: list[dict], *, structural: bool) -> dict:
    views = {}
    for view in ("fit", "dense"):
        view_trials = [trial for trial in trials if trial["view"] == view]
        if not view_trials:
            continue
        paths = sorted({path for trial in view_trials for path in trial["self_ns"]})
        rows = []
        for path in paths:
            values = []
            medians = []
            p95s = []
            dominant_trials = 0
            for trial in view_trials:
                if path not in trial["self_ns"]:
                    values.append({"trial": trial["trial"], "self_total_ns": 0,
                                   "lane_share": 0.0, "window_share": 0.0,
                                   "lane_dominant": False, "meets_decision_rule": False,
                                   "observed": False})
                    medians.append(0)
                    p95s.append(0)
                    continue
                aggregate = trial["aggregates"][path]
                lane = aggregate["lane"]
                lane_total = sum(total for candidate, total in trial["self_ns"].items()
                                 if trial["aggregates"][candidate]["lane"] == lane)
                lane_max = max(total for candidate, total in trial["self_ns"].items()
                               if trial["aggregates"][candidate]["lane"] == lane)
                self_ns = trial["self_ns"][path]
                lane_share = self_ns / lane_total if lane_total else 0.0
                window_share = self_ns / trial["observed_ns"]
                dominant = self_ns == lane_max
                qualifies = dominant and lane_share >= 0.20 and window_share >= 0.05
                dominant_trials += int(qualifies)
                values.append({"trial": trial["trial"], "self_total_ns": self_ns,
                               "lane_share": lane_share, "window_share": window_share,
                               "lane_dominant": dominant, "meets_decision_rule": qualifies,
                               "observed": True})
                medians.append(aggregate["median_ns"])
                p95s.append(aggregate["p95_ns"])
            aggregate = next(trial["aggregates"][path] for trial in view_trials
                             if path in trial["aggregates"])
            rows.append({"lane": aggregate["lane"], "path": path, "parent": aggregate["parent"],
                         "trial_self_totals": values,
                         "median_trial_self_total_ns": int(statistics.median(
                             value["self_total_ns"] for value in values)),
                         "median_per_call_median_ns": int(statistics.median(medians)),
                         "median_per_call_p95_ns": int(statistics.median(p95s)),
                         "decision_qualifying_trials": dominant_trials,
                         "decision_satisfied": dominant_trials >= 2})
        rows.sort(key=lambda row: (row["lane"], -row["median_trial_self_total_ns"], row["path"]))
        views[view] = {
            "trials": [{
                "trial": trial["trial"],
                "unobserved_optional_paths": sorted(OPTIONAL_PATHS - trial["aggregates"].keys()),
            } for trial in view_trials],
            "rankings": rows,
        }

    comparisons = []
    if {"fit", "dense"} <= set(views):
        fit = {row["path"]: row for row in views["fit"]["rankings"]}
        dense = {row["path"]: row for row in views["dense"]["rankings"]}
        for path in sorted(fit.keys() & dense.keys()):
            fit_total = fit[path]["median_trial_self_total_ns"]
            comparisons.append({"path": path,
                "dense_to_fit_median_self_ratio": dense[path]["median_trial_self_total_ns"] / fit_total
                    if fit_total else None,
                "dense_to_fit_per_call_median_ratio": dense[path]["median_per_call_median_ns"]
                    / fit[path]["median_per_call_median_ns"] if fit[path]["median_per_call_median_ns"] else None,
                "dense_to_fit_per_call_p95_ratio": dense[path]["median_per_call_p95_ns"]
                    / fit[path]["median_per_call_p95_ns"] if fit[path]["median_per_call_p95_ns"] else None})
    mode = "structural" if structural else "recorded"
    evidence_status = "structural only; not recorded evidence" if structural else "recorded evidence"
    return {"schema": 2, "mode": mode, "evidence_status": evidence_status,
            "views": views, "dense_to_fit": comparisons}


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--app", required=True, type=Path)
    parser.add_argument("--scene", required=True, type=Path)
    parser.add_argument("--scenario", default="default", choices=("default", "baseline"))
    parser.add_argument("--views", nargs="+", default=["fit", "dense"], choices=("fit", "dense"))
    parser.add_argument("--duration", type=float, default=30.0)
    parser.add_argument("--step-delay", type=int, default=250)
    parser.add_argument("--trials", type=int, default=3)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--structural-test", action="store_true")
    args = parser.parse_args()
    if args.duration <= 0 or not math.isfinite(args.duration) or args.step_delay < 0 or args.step_delay > 500 or args.trials < 1:
        parser.error("duration, step delay, and trials are out of range")
    if not args.structural_test and (args.views != ["fit", "dense"] or args.trials != 3
                                    or args.duration != 30.0 or args.step_delay != 250):
        parser.error("recorded evidence requires exactly Fit+dense, three trials, 30 seconds, and 250 ms delay")
    return args


def main() -> None:
    args = parse_args()
    app, scene = args.app.resolve(), args.scene.resolve()
    if not app.is_file() or not scene.exists():
        raise SystemExit("application or scene does not exist")
    if any(os.environ.get(name) == "1" for name in ("QEGTRAIN_STARTUP_TIMING", "QEGTRAIN_STARTUP_NATIVE_DETAIL")):
        raise SystemExit("startup timing must be disabled")
    output = prepare_output_root(args.output, app, scene)
    duration_ms = round(args.duration * 1000)
    all_records: list[dict] = []
    self_by_trial = []
    for view in args.views:
        for trial in range(1, args.trials + 1):
            env = os.environ.copy()
            env.update({
                "QEGTRAIN_PLAYBACK_PROFILE": "1",
                "QEGTRAIN_PLAYBACK_PROFILE_VIEW": view,
                "QEGTRAIN_PLAYBACK_PROFILE_TRIAL": str(trial),
                "QEGTRAIN_PLAYBACK_PROFILE_DURATION_MS": str(duration_ms),
                "QEGTRAIN_PLAYBACK_PROFILE_DELAY_MS": str(args.step_delay),
                "QEGTRAIN_PLAYBACK_PROFILE_SCENARIO": args.scenario,
                "QEGTRAIN_PLAYBACK_PROFILE_STRUCTURAL": "1" if args.structural_test else "0",
            })
            if not args.structural_test:
                env["QT_QPA_PLATFORM"] = "cocoa"
            with tempfile.TemporaryDirectory(prefix="qegtrain-playback-runtime-") as runtime_output:
                env["QEGTRAIN_OUTPUT_DIR"] = runtime_output
                command = [str(app), "--scene", str(scene), "-h", "8000", "-g", "1",
                           "-pax", "1", "-TSM", "0", "-RC", "0"]
                completed = subprocess.run(command, cwd=app.parent, env=env, text=True,
                                           stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                           timeout=max(120, int(args.duration) + 90), check=False)
            parsed = []
            for line in completed.stdout.splitlines():
                if line.startswith(PREFIX):
                    parsed.append(json.loads(line[len(PREFIX):]))
            if completed.returncode or any(record.get("type") == "error" for record in parsed):
                raise SystemExit(f"profile process failed for {view} trial {trial}")
            validate_trial(parsed, trial=trial, view=view, duration_ms=duration_ms,
                           delay_ms=args.step_delay, structural=args.structural_test,
                           requested_scenario=args.scenario,
                           expected_qt_platform=env.get("QT_QPA_PLATFORM"))
            all_records.extend(parsed)
            aggregates = {record["path"]: record for record in parsed if record["type"] == "aggregate"}
            completion = next(record for record in parsed if record["type"] == "completion")
            self_by_trial.append({"view": view, "trial": trial, "self_ns": self_totals(parsed),
                                  "aggregates": aggregates, "observed_ns": completion["observed_ns"]})

    with (output / "records.jsonl").open("w", encoding="utf-8") as handle:
        for record in all_records:
            handle.write(json.dumps(record, separators=(",", ":"), sort_keys=True) + "\n")
    summary = summarize(self_by_trial, structural=args.structural_test)
    (output / "summary.json").write_text(json.dumps(summary, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    lines = [
        "EGTRAIN playback profile",
        f"Mode: {summary['mode']}",
        f"Evidence status: {summary['evidence_status']}",
        f"Platform/build: {platform.system()} / records validated per trial",
        f"Settings: scenario={args.scenario}, views={','.join(args.views)}, duration={args.duration:g}s, "
        f"delay={args.step_delay}ms, trials={args.trials}, passenger GUI on, TSM/route choice off, horizon=8000",
        "Reproduce: tools/performance/playback_profile.py --app <release-app> --scene <copenhagen-scene> "
        "--scenario default --views fit dense --duration 30 --step-delay 250 --trials 3 --output <new-output-dir>",
        "Decision rule: a path qualifies when it is its lane's largest self-time path in at least 2/3 "
        "trials, with at least 20% of lane self-time and 5% of the measured window in those trials.",
    ]
    for view, view_summary in summary["views"].items():
        lines.append(f"\n{view} optional paths:")
        for trial in view_summary["trials"]:
            unobserved = ", ".join(trial["unobserved_optional_paths"]) or "none"
            lines.append(f"trial {trial['trial']} unobserved optional paths: {unobserved}")
        lines.append(f"\n{view} rankings:")
        for row in view_summary["rankings"]:
            trial_totals = ", ".join(
                f"t{value['trial']}={value['self_total_ns']}ns/"
                f"{value['lane_share']:.1%} lane/{value['window_share']:.1%} window/"
                f"dominant={'yes' if value['lane_dominant'] else 'no'}"
                for value in row["trial_self_totals"])
            lines.append(f"{row['path']}: self [{trial_totals}], median self "
                         f"{row['median_trial_self_total_ns']}ns, median per-call "
                         f"{row['median_per_call_median_ns']}ns, median p95 {row['median_per_call_p95_ns']}ns, "
                         f"decision {row['decision_qualifying_trials']}/{len(row['trial_self_totals'])} "
                         f"({'yes' if row['decision_satisfied'] else 'no'})")
    if summary["dense_to_fit"]:
        lines.append("\nDense-to-Fit comparisons:")
        for comparison in summary["dense_to_fit"]:
            ratio = comparison["dense_to_fit_median_self_ratio"]
            lines.append(f"{comparison['path']}: median self ratio "
                         f"{ratio:.3f}" if ratio is not None else f"{comparison['path']}: median self ratio n/a")
    report = "\n".join(lines) + "\n"
    (output / "report.txt").write_text(report, encoding="utf-8")
    print(f"wrote {len(all_records)} validated records")


if __name__ == "__main__":
    main()
