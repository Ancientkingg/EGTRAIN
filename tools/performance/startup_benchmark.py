#!/usr/bin/env python3
"""Run opt-in EGTRAIN startup timing trials and summarize the JSON records."""

import argparse
import json
import os
import platform
import signal
import statistics
import subprocess
import tempfile
from collections import defaultdict
from datetime import datetime, timezone
from pathlib import Path

PREFIX = "QEGTRAIN_TIMING "
REPO_ROOT = Path(__file__).resolve().parents[2]
SUMMARY_KEY_FIELDS = ("scene", "scene_path", "temperature", "phase", "invocation", "source")


def expected_record_sequence(warm_trials: int) -> list[tuple[int, str, str, str]]:
    records = [
        (0, "canonical_load", "startup_preload", "main"),
        (0, "application_initialization", "main_to_mainwindow_constructed", "main"),
    ]
    for iteration in range(warm_trials + 1):
        records.extend([
            (iteration, "canonical_load", "scene_open", "MainWindow::openSceneDirectory"),
            (iteration, "first_preview_paint", "preview", "NetworkView::viewportEvent"),
            (iteration, "native_runtime_construction", "simulation.prepareScene", "MainWindow::runScene"),
            (iteration, "first_runtime_paint", "runtime", "NetworkView::viewportEvent"),
        ])
    return records


def validate_records(records: list[dict], warm_trials: int) -> None:
    if any(not isinstance(record, dict) for record in records):
        raise ValueError("timing records must be JSON objects")
    expected = expected_record_sequence(warm_trials)
    actual = [
        (record.get("iteration"), record.get("phase"), record.get("invocation"), record.get("source"))
        for record in records
    ]
    if actual != expected:
        raise ValueError(f"timing record order mismatch: {actual!r}")
    if [record.get("sequence") for record in records] != list(range(len(records))):
        raise ValueError("timing sequence is not globally contiguous")
    if any(record.get("event") != "complete" for record in records):
        raise ValueError("timing records must all be complete events")
    if any(record.get("identity_ok") is not True for record in records):
        raise ValueError("timing identity equality failed")
    if any(
        isinstance(record.get("elapsed_ms"), bool)
        or not isinstance(record.get("elapsed_ms"), (int, float))
        or record["elapsed_ms"] < 0
        for record in records
    ):
        raise ValueError("timing elapsed_ms must be a non-negative number")
    if any(
        record.get("temperature") != ("fresh_process" if record["iteration"] == 0 else "warm_in_process")
        for record in records
    ):
        raise ValueError("timing temperature labels do not match iterations")

    iterations = {record["iteration"] for record in records}
    if iterations != set(range(warm_trials + 1)):
        raise ValueError("timing iterations are not contiguous")
    paints = [record for record in records if record["phase"] in ("first_preview_paint", "first_runtime_paint")]
    for iteration in iterations:
        iteration_paints = [record for record in paints if record["iteration"] == iteration]
        if [(record["invocation"], record.get("generation")) for record in iteration_paints] != [
            ("preview", iteration), ("runtime", iteration)
        ]:
            raise ValueError(f"iteration {iteration} does not have one preview/runtime generation")
    if any(record.get("paint_scope") != "completed_qt_viewport_paint_handling" for record in paints):
        raise ValueError("first-paint scope is incomplete")
    if sum(record["phase"] == "native_runtime_construction" for record in records) != warm_trials + 1:
        raise ValueError("native preparation count is incorrect")
    canonical_counts = [
        sum(record["phase"] == "canonical_load" and record["iteration"] == iteration for record in records)
        for iteration in range(warm_trials + 1)
    ]
    if canonical_counts != [2] + [1] * warm_trials:
        raise ValueError(f"canonical load counts are incorrect: {canonical_counts!r}")
    application = [record for record in records if record["phase"] == "application_initialization"]
    if len(application) != 1 or application[0].get("canonical_preload_nested") is not True:
        raise ValueError("application initialization must identify its nested canonical preload")


def paths_overlap(left: Path, right: Path) -> bool:
    return left == right or left in right.parents or right in left.parents


def prepare_output_root(requested: Path, app: Path, scenes: list[Path]) -> Path:
    output = Path(os.path.abspath(os.fspath(requested.expanduser())))
    for component in (output, *output.parents):
        if os.path.lexists(component) and component.is_symlink():
            raise SystemExit(f"output path has a symlink ancestor: {component}")
    if os.path.lexists(output):
        raise SystemExit(f"output path already exists: {output}")
    if not output.parent.is_dir():
        raise SystemExit(f"output parent does not exist: {output.parent}")

    resolved = output.resolve(strict=False)
    repository = REPO_ROOT.resolve()
    if resolved == repository or repository in resolved.parents:
        raise SystemExit(f"output path must be outside the product repository: {resolved}")
    if resolved == app or resolved in app.parents or app in resolved.parents:
        raise SystemExit(f"output path overlaps the application: {resolved}")
    for scene in scenes:
        if paths_overlap(resolved, scene):
            raise SystemExit(f"output path overlaps scene input {scene}: {resolved}")
    resolved.mkdir()
    (resolved / "logs").mkdir()
    return resolved


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--app", required=True, type=Path)
    parser.add_argument("--scene", required=True, action="append", type=Path)
    parser.add_argument("--fresh-process-trials", type=int, default=5)
    parser.add_argument("--warm-trials", type=int, default=2)
    parser.add_argument("--timeout", type=float, default=120.0)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    if args.fresh_process_trials < 1 or args.warm_trials < 0 or args.timeout <= 0:
        parser.error("trial counts and timeout must be positive (warm trials may be zero)")
    return args


def run_process(
    command: list[str], env: dict[str, str], cwd: Path, timeout: float
) -> tuple[int, str, str | None]:
    proc = subprocess.Popen(
        command,
        cwd=cwd,
        env=env,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        errors="replace",
        start_new_session=True,
    )
    try:
        output, _ = proc.communicate(timeout=timeout)
    except subprocess.TimeoutExpired:
        try:
            os.killpg(proc.pid, signal.SIGTERM)
        except ProcessLookupError:
            pass
        try:
            output, _ = proc.communicate(timeout=3)
        except subprocess.TimeoutExpired:
            try:
                os.killpg(proc.pid, signal.SIGKILL)
            except ProcessLookupError:
                pass
            output, _ = proc.communicate()
        return -1, output, f"timed out after {timeout:g}s"
    return proc.returncode, output, None


def main() -> None:
    args = parse_args()
    app = args.app.resolve()
    scenes = [scene.resolve() for scene in args.scene]
    if not app.is_file():
        raise SystemExit(f"application not found: {app}")
    for scene in scenes:
        if not scene.exists():
            raise SystemExit(f"scene not found: {scene}")

    output = prepare_output_root(args.output, app, scenes)
    logs = output / "logs"
    metadata = {
        "timestamp_utc": datetime.now(timezone.utc).isoformat(),
        "os": platform.system(),
        "os_version": platform.mac_ver()[0] or platform.release(),
        "architecture": platform.machine(),
        "executable": str(app),
        "executable_size_bytes": app.stat().st_size,
        "command": [str(app), "--scene", "<scene>"],
        "scenes": [str(scene) for scene in scenes],
        "fresh_process_trials": args.fresh_process_trials,
        "warm_trials_per_process": args.warm_trials,
        "timeout_seconds": args.timeout,
        "temperature_labels": {
            "fresh_process": "iteration 0; OS/APFS/dyld caches may already be warm",
            "warm_in_process": "later same-process reload iterations",
        },
    }
    if platform.processor():
        metadata["cpu"] = platform.processor()
    (output / "metadata.json").write_text(json.dumps(metadata, indent=2) + "\n", encoding="utf-8")

    records: list[dict] = []
    trials_path = output / "trials.jsonl"
    with trials_path.open("w", encoding="utf-8") as trials_file:
        for scene_index, scene in enumerate(scenes):
            scene_label = scene.stem if scene.is_file() else scene.name
            for trial in range(args.fresh_process_trials):
                env = os.environ.copy()
                env["QEGTRAIN_STARTUP_TIMING"] = "1"
                env["QEGTRAIN_STARTUP_WARM_TRIALS"] = str(args.warm_trials)
                with tempfile.TemporaryDirectory(prefix="qegtrain-startup-output-") as process_output:
                    env["QEGTRAIN_OUTPUT_DIR"] = process_output
                    command = [str(app), "--scene", str(scene)]
                    returncode, process_log, failure = run_process(
                        command, env, app.parent, args.timeout
                    )
                log_path = logs / f"{scene_index:02d}-{scene_label}-{trial:02d}.log"
                log_path.write_text(process_log, encoding="utf-8")
                parsed = []
                for line in process_log.splitlines():
                    if not line.startswith(PREFIX):
                        continue
                    try:
                        record = json.loads(line[len(PREFIX):])
                    except json.JSONDecodeError as exc:
                        raise SystemExit(f"invalid timing record in {log_path}: {exc}") from exc
                    parsed.append(record)
                if returncode != 0:
                    detail = failure or f"exited with {returncode}"
                    raise SystemExit(f"{scene_label} trial {trial} {detail}; full output: {log_path}")
                try:
                    validate_records(parsed, args.warm_trials)
                except ValueError as exc:
                    raise SystemExit(f"invalid timing records for {scene_label} trial {trial}: {exc}; see {log_path}") from exc
                for record in parsed:
                    record.update(scene=scene_label, scene_path=str(scene), fresh_process_trial=trial)
                    records.append(record)
                    trials_file.write(json.dumps(record, separators=(",", ":")) + "\n")
                trials_file.flush()

    grouped: dict[tuple[str, str, str, str, str, str], list[float]] = defaultdict(list)
    for record in records:
        key = tuple(record.get(field, "") for field in SUMMARY_KEY_FIELDS)
        grouped[key].append(record["elapsed_ms"])
    summaries = []
    for (scene, scene_path, temperature, phase, invocation, source), values in sorted(grouped.items()):
        summaries.append({
            "scene": scene,
            "scene_path": scene_path,
            "temperature": temperature,
            "phase": phase,
            "invocation": invocation,
            "source": source,
            "n": len(values),
            "min_ms": min(values),
            "median_ms": statistics.median(values),
            "max_ms": max(values),
        })
    (output / "summary.json").write_text(
        json.dumps({
            "schema_version": 1,
            "key_fields": list(SUMMARY_KEY_FIELDS),
            "groups": summaries,
        }, indent=2) + "\n",
        encoding="utf-8",
    )
    print(f"wrote {len(records)} timing records to {output}")


if __name__ == "__main__":
    main()
