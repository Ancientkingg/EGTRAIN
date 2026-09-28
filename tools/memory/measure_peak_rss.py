#!/usr/bin/env python3
"""Measure peak RSS for the canonical native cases on macOS."""

import argparse
import datetime
import json
import os
import platform
import re
import signal
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SOURCE_DIR = ROOT / "EGTRAIN/QEGTRAIN"
SCENE_DIR = SOURCE_DIR / "Scenes"
TIME_TOOL = Path("/usr/bin/time")
TIMEOUT_SECONDS = 900
SCENARIO = "canonical native full headless run; GUI, train sharing, and route choice disabled"
CASES = ("Copenhagen", "Milano_Brescia")


class MeasurementError(RuntimeError):
    pass


def parse_macos_time_peak_rss(stderr: str) -> int:
    """Return macOS /usr/bin/time's maximum resident set size in bytes."""
    matches = re.findall(
        r"^\s*([0-9]+)\s+maximum resident set size\s*$", stderr, re.MULTILINE
    )
    if len(matches) != 1:
        raise ValueError(
            "expected exactly one 'maximum resident set size' line from /usr/bin/time -l"
        )
    value = int(matches[0])
    if value <= 0:
        raise ValueError("maximum resident set size must be greater than zero")
    return value


def case_command(app: Path, scene_dir: Path) -> list[str]:
    return [
        str(TIME_TOOL),
        "-l",
        str(app),
        "--scene",
        str(scene_dir),
        "-g",
        "0",
        "-TSM",
        "0",
        "-RC",
        "0",
    ]


def validate_output_dir(output_dir: Path, repo_root: Path = ROOT) -> Path:
    """Allow generated data only under build/ or outside the checkout."""
    requested = Path(os.path.abspath(output_dir.expanduser()))
    if requested.is_symlink():
        raise MeasurementError(f"output path must not be a symlink: {requested}")
    output = requested.resolve()
    root = repo_root.resolve()
    try:
        relative = output.relative_to(root)
    except ValueError:
        return output
    if not relative.parts or relative.parts[0] != "build":
        raise ValueError(
            f"output directory inside the checkout must be under {root / 'build'}"
        )
    return output


def validate_measurement_host() -> None:
    if sys.platform != "darwin":
        raise MeasurementError("peak-RSS collection is supported only on macOS")
    if not TIME_TOOL.is_file():
        raise MeasurementError(f"required measurement tool is missing: {TIME_TOOL}")


def cmake_build_type(app: Path, explicit: str | None) -> str:
    app = app.expanduser().resolve()
    for directory in app.parents:
        cache = directory / "CMakeCache.txt"
        if not cache.is_file():
            continue
        try:
            lines = cache.read_text(encoding="utf-8", errors="replace").splitlines()
        except OSError:
            continue
        for line in lines:
            if line.startswith("CMAKE_BUILD_TYPE:"):
                value = line.partition("=")[2].strip()
                if not value:
                    continue
                if explicit and explicit != value:
                    raise MeasurementError(
                        f"--build-type {explicit!r} conflicts with {cache}'s "
                        f"CMAKE_BUILD_TYPE {value!r}"
                    )
                return value
    if explicit:
        return explicit
    raise MeasurementError(
        "build type is unavailable from the application's CMake cache; "
        "pass --build-type for an external application"
    )


def reject_output_symlink(path: Path, output_dir: Path) -> None:
    """Reject an owned path that is or resolves outside the output directory."""
    if path.is_symlink():
        raise MeasurementError(f"collector output path must not be a symlink: {path}")
    try:
        path.resolve().relative_to(output_dir.resolve())
    except ValueError as exc:
        raise MeasurementError(
            f"collector output path resolves outside {output_dir}: {path}"
        ) from exc


def ensure_output_directory(path: Path, output_dir: Path) -> None:
    reject_output_symlink(path, output_dir)
    path.mkdir(exist_ok=True)
    reject_output_symlink(path, output_dir)
    if not path.is_dir():
        raise MeasurementError(f"collector output path is not a directory: {path}")


def reject_symlinks_below(directory: Path, output_dir: Path) -> None:
    for path in directory.rglob("*"):
        reject_output_symlink(path, output_dir)


def prepare_output_directory(output_dir: Path) -> Path:
    output_dir.mkdir(parents=True, exist_ok=True)
    if not output_dir.is_dir():
        raise MeasurementError(f"output path is not a directory: {output_dir}")
    for directory in (output_dir / "logs", output_dir / "runtime"):
        ensure_output_directory(directory, output_dir)
    result_path = output_dir / "peak-rss.json"
    reject_output_symlink(result_path, output_dir)
    reject_output_symlink(result_path.with_suffix(".json.tmp"), output_dir)
    result_path.unlink(missing_ok=True)
    return result_path


def prepare_case_output(case_name: str, output_dir: Path) -> tuple[Path, Path, Path, Path]:
    logs = output_dir / "logs"
    runtime_root = output_dir / "runtime"
    runtime_base = runtime_root / case_name
    runtime_output_root = runtime_base / "Output"
    runtime_output = runtime_output_root / case_name
    for directory in (logs, runtime_root, runtime_base, runtime_output_root, runtime_output):
        ensure_output_directory(directory, output_dir)
    reject_symlinks_below(runtime_base, output_dir)
    stdout_log = logs / f"{case_name}.stdout.log"
    stderr_log = logs / f"{case_name}.stderr.log"
    reject_output_symlink(stdout_log, output_dir)
    reject_output_symlink(stderr_log, output_dir)
    return runtime_base, runtime_output, stdout_log, stderr_log


def open_output_log(path: Path, output_dir: Path):
    reject_output_symlink(path, output_dir)
    flags = os.O_WRONLY | os.O_CREAT | os.O_TRUNC
    flags |= getattr(os, "O_NOFOLLOW", 0)
    descriptor = os.open(path, flags, 0o666)
    return os.fdopen(descriptor, "wb")


def run_process_group(
    command: list[str],
    *,
    cwd: Path,
    env: dict[str, str],
    stdout,
    stderr,
    timeout: float,
) -> int:
    """Run a command in a new process group and reap it fully on timeout."""
    process = subprocess.Popen(
        command,
        cwd=cwd,
        env=env,
        stdout=stdout,
        stderr=stderr,
        start_new_session=True,
    )
    try:
        return process.wait(timeout=timeout)
    except subprocess.TimeoutExpired:
        try:
            os.killpg(process.pid, signal.SIGKILL)
        except ProcessLookupError:
            pass
        process.wait()
        raise


def make_run_record(
    *,
    case_name: str,
    run_order: int,
    command: list[str],
    working_directory: Path,
    runtime_output: Path,
    stdout_log: Path,
    stderr_log: Path,
    output_dir: Path,
    peak_rss_bytes: int,
    elapsed_seconds: float,
) -> dict:
    return {
        "run_order": run_order,
        "case": case_name,
        "scenario": SCENARIO,
        "command": command,
        "working_directory": str(working_directory),
        "runtime_output": str(runtime_output),
        "peak_rss_bytes": peak_rss_bytes,
        "peak_rss_mib": round(peak_rss_bytes / (1024 * 1024), 3),
        "elapsed_seconds": round(elapsed_seconds, 6),
        "exit_code": 0,
        "stdout_log": str(stdout_log.relative_to(output_dir)),
        "stderr_log": str(stderr_log.relative_to(output_dir)),
    }


def write_json_atomic(path: Path, record: dict) -> None:
    temporary = path.with_suffix(path.suffix + ".tmp")
    reject_output_symlink(path, path.parent)
    reject_output_symlink(temporary, path.parent)
    temporary.unlink(missing_ok=True)
    flags = os.O_WRONLY | os.O_CREAT | os.O_EXCL
    flags |= getattr(os, "O_NOFOLLOW", 0)
    descriptor = os.open(temporary, flags, 0o666)
    with os.fdopen(descriptor, "w", encoding="utf-8") as temporary_file:
        temporary_file.write(json.dumps(record, indent=2) + "\n")
    os.replace(temporary, path)


def measure_case(
    *,
    case_name: str,
    run_order: int,
    app: Path,
    output_dir: Path,
) -> dict:
    scene = (SCENE_DIR / case_name).resolve()
    command = case_command(app, scene)
    runtime_base, runtime_output, stdout_log, stderr_log = prepare_case_output(
        case_name, output_dir
    )
    logs = output_dir / "logs"

    environment = os.environ.copy()
    environment["QEGTRAIN_OUTPUT_DIR"] = str(runtime_base)
    environment["QT_QPA_PLATFORM"] = "offscreen"
    started = time.monotonic()
    try:
        with open_output_log(stdout_log, output_dir) as stdout_file, open_output_log(
            stderr_log, output_dir
        ) as stderr_file:
            returncode = run_process_group(
                command,
                cwd=SOURCE_DIR,
                env=environment,
                stdout=stdout_file,
                stderr=stderr_file,
                timeout=TIMEOUT_SECONDS,
            )
    except subprocess.TimeoutExpired as exc:
        raise MeasurementError(
            f"{case_name} timed out after {TIMEOUT_SECONDS}s; logs retained in {logs}"
        ) from exc
    elapsed = time.monotonic() - started
    if returncode != 0:
        raise MeasurementError(
            f"{case_name} exited with {returncode}; logs retained in {logs}"
        )
    try:
        stderr = stderr_log.read_text(encoding="utf-8", errors="replace")
        peak_rss = parse_macos_time_peak_rss(stderr)
    except ValueError as exc:
        raise MeasurementError(f"cannot parse {case_name} peak RSS: {exc}") from exc
    return make_run_record(
        case_name=case_name,
        run_order=run_order,
        command=command,
        working_directory=SOURCE_DIR,
        runtime_output=runtime_output,
        stdout_log=stdout_log,
        stderr_log=stderr_log,
        output_dir=output_dir,
        peak_rss_bytes=peak_rss,
        elapsed_seconds=elapsed,
    )


def collect(app: Path, output_dir: Path, explicit_build_type: str | None) -> Path:
    validate_measurement_host()
    app = app.expanduser().resolve()
    if not app.is_file() or not os.access(app, os.X_OK):
        raise MeasurementError(f"application is not an executable file: {app}")
    for case_name in CASES:
        if not (SCENE_DIR / case_name).is_dir():
            raise MeasurementError(f"canonical scene is missing: {SCENE_DIR / case_name}")
    build_type = cmake_build_type(app, explicit_build_type)

    output_dir = validate_output_dir(output_dir)
    result_path = prepare_output_directory(output_dir)

    runs = [
        measure_case(
            case_name=case_name,
            run_order=index,
            app=app,
            output_dir=output_dir,
        )
        for index, case_name in enumerate(CASES, 1)
    ]
    record = {
        "schema_version": 1,
        "recorded_at_utc": datetime.datetime.now(datetime.timezone.utc)
        .isoformat()
        .replace("+00:00", "Z"),
        "platform": {
            "system": platform.system(),
            "release": platform.release(),
            "machine": platform.machine(),
            "macos_version": platform.mac_ver()[0],
        },
        "measurement_tool": {
            "path": str(TIME_TOOL),
            "arguments": ["-l"],
            "metric": "maximum resident set size",
            "unit": "bytes",
        },
        "build": {
            "app": str(app),
            "configuration": build_type,
        },
        "output_location": str(output_dir),
        "runs": runs,
    }
    write_json_atomic(result_path, record)
    return result_path


def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Measure canonical peak RSS with macOS /usr/bin/time -l"
    )
    parser.add_argument("--app", type=Path, required=True, help="QEGTRAIN executable")
    parser.add_argument(
        "--output-dir", type=Path, required=True, help="generated measurement directory"
    )
    parser.add_argument(
        "--build-type",
        help="fallback build configuration when no containing CMake cache provides one",
    )
    return parser.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    args = parse_args(argv)
    try:
        result = collect(args.app, args.output_dir, args.build_type)
    except (MeasurementError, ValueError) as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        return 1
    print(result)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
