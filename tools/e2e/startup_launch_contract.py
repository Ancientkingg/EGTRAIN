#!/usr/bin/env python3
import json
import os
import select
import signal
import subprocess
import sys
import tempfile
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools/performance"))
from startup_benchmark import validate_records

RUN_DIR = ROOT / "EGTRAIN/QEGTRAIN"
PROMPT_MARKERS = (
    "No arguments inserted",
    "Please enter",
    "Please insert",
    "Do you want to change it",
)
READY_MARKERS = (
    "Graphical user interface (GUI):",
    "Passenger GUI:",
)
TIMING_PREFIX = "QEGTRAIN_TIMING "


def assert_startup_timing_contract(app: Path) -> None:
    scene = ROOT / "EGTRAIN/QEGTRAIN/Scenes/Paimpol"
    env = os.environ.copy()
    env["QT_QPA_PLATFORM"] = "offscreen"
    env["QEGTRAIN_STARTUP_TIMING"] = "1"
    env["QEGTRAIN_STARTUP_WARM_TRIALS"] = "1"
    with tempfile.TemporaryDirectory(prefix="qegtrain-startup-contract-") as output_dir:
        env["QEGTRAIN_OUTPUT_DIR"] = output_dir
        try:
            proc = subprocess.run(
                [str(app), "--scene", str(scene)],
                cwd=RUN_DIR,
                env=env,
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
                text=True,
                timeout=12,
            )
        except subprocess.TimeoutExpired as exc:
            raise SystemExit(f"startup timing contract timed out\n{(exc.stdout or '')[-4000:]}") from exc
    if proc.returncode != 0:
        raise SystemExit(f"startup timing contract exited with {proc.returncode}\n{proc.stdout[-4000:]}")
    if "E2E_STARTUP_CHOOSER_" in proc.stdout:
        raise SystemExit(f"explicit timing launch opened the chooser\n{proc.stdout[-4000:]}")

    records = [
        json.loads(line[len(TIMING_PREFIX):])
        for line in proc.stdout.splitlines()
        if line.startswith(TIMING_PREFIX)
    ]
    try:
        validate_records(records, warm_trials=1)
    except ValueError as exc:
        raise SystemExit(f"startup timing contract failed: {exc}\n{proc.stdout[-4000:]}") from exc


def assert_no_argument_chooser_continuation(app: Path) -> None:
    env = os.environ.copy()
    env.setdefault("QT_QPA_PLATFORM", "offscreen")
    env["QEGTRAIN_E2E_STARTUP_CHOOSER"] = "1"
    try:
        proc = subprocess.run(
            [str(app)],
            cwd=RUN_DIR,
            env=env,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            timeout=12,
        )
    except subprocess.TimeoutExpired as exc:
        raise SystemExit(
            f"no-argument chooser continuation timed out\n{(exc.stdout or '')[-4000:]}"
        ) from exc
    if proc.returncode != 0:
        raise SystemExit(
            f"no-argument chooser continuation exited with {proc.returncode}\n{proc.stdout[-4000:]}"
        )

    values = {}
    for line in proc.stdout.splitlines():
        if line.startswith("E2E_STARTUP_CHOOSER_") and "=" in line:
            key, value = line.split("=", 1)
            values[key] = value
    expected = {
        "E2E_STARTUP_CHOOSER_PROMPT": (
            "Netherlands is already loaded. Choose another case study to open, or continue with it:"
        ),
        "E2E_STARTUP_CHOOSER_ACTION": "Continue with Netherlands",
        "E2E_STARTUP_CHOOSER_MODEL": "Netherlands",
        "E2E_STARTUP_CHOOSER_CASE_LABEL": "Netherlands",
        "E2E_STARTUP_CHOOSER_READINESS": "Ready to run",
        "E2E_STARTUP_CHOOSER_UNCHANGED": "yes",
    }
    mismatches = [
        f"{key}={values.get(key)!r}, expected {value!r}"
        for key, value in expected.items()
        if values.get(key) != value
    ]
    status = values.get("E2E_STARTUP_CHOOSER_STATUS", "")
    if status != "Ready - Netherlands":
        mismatches.append(
            f"E2E_STARTUP_CHOOSER_STATUS={status!r}, expected 'Ready - Netherlands'"
        )
    if mismatches:
        raise SystemExit(
            "no-argument chooser continuation state mismatch: "
            + "; ".join(mismatches)
            + f"\n{proc.stdout[-4000:]}"
        )


def assert_deferred_success_path_resets() -> None:
    dispatch = (ROOT / "EGTRAIN/QEGTRAIN/app/DispatchController.cpp").read_text(encoding="utf-8")
    prepare = dispatch[
        dispatch.index("std::vector<SceneDiagnostic> DispatchController::prepareScene("):
        dispatch.index("void DispatchController::beginScenePreparation()")
    ]
    if prepare.index("beginScenePreparation();") > prepare.index("validateRunnableScene("):
        raise SystemExit("scene preparation does not clear lightweight state before validation")
    if prepare.count("resetState();") != 4:
        raise SystemExit("scene preparation failure paths do not all perform a full reset")

    begin_preparation = dispatch[
        dispatch.index("void DispatchController::beginScenePreparation()"):
        dispatch.index("void DispatchController::resetState()")
    ]
    if "prepareNativeOperationsState();" not in begin_preparation:
        raise SystemExit("lightweight scene preparation no longer prepares operation state")
    for reset in ("resetNativeOperationsState();", "resetNativeInfrastructureState();"):
        if reset in begin_preparation:
            raise SystemExit(f"lightweight scene preparation incorrectly calls {reset}")

    full_reset = dispatch[
        dispatch.index("void DispatchController::resetState()"):
        dispatch.index("std::shared_ptr<const GuiSimulationSnapshot>")
    ]
    for reset in ("resetNativeInfrastructureState();", "resetNativeOperationsState();"):
        if reset not in full_reset:
            raise SystemExit(f"full dispatch reset no longer calls {reset}")

    rolling = (ROOT / "EGTRAIN/QEGTRAIN/simulation/RollingStock.cpp").read_text(encoding="utf-8")
    lightweight = rolling[
        rolling.index("void prepareNativeOperationsState()"):
        rolling.index("void resetNativeOperationsState()")
    ]
    full_operations_reset = rolling[
        rolling.index("void resetNativeOperationsState()"):
        rolling.index("std::vector<SceneDiagnostic> buildOperationsFromScene(")
    ]
    if "Max_N_Reg" in lightweight or "~Regional()" in lightweight:
        raise SystemExit("lightweight operation preparation reconstructs regional_train slots")
    if "prepareNativeOperationsState();" not in full_operations_reset or "Max_N_Reg" not in full_operations_reset:
        raise SystemExit("full operation reset does not combine lightweight clearing with train reconstruction")


def assert_launch_reaches_defaults(app: Path, args: list[str], label: str) -> None:
    master, slave = pty.openpty()
    output = b""
    env = os.environ.copy()
    env.setdefault("QT_QPA_PLATFORM", "offscreen")
    proc = subprocess.Popen(
        [str(app), *args],
        cwd=RUN_DIR,
        env=env,
        stdin=slave,
        stdout=slave,
        stderr=slave,
        preexec_fn=os.setsid,
    )
    os.close(slave)

    try:
        deadline = time.monotonic() + 8
        while time.monotonic() < deadline:
            readable, _, _ = select.select([master], [], [], 0.1)
            if master not in readable:
                continue
            try:
                chunk = os.read(master, 4096)
            except OSError:
                break
            if not chunk:
                break
            output += chunk
            text = output.decode("utf-8", errors="replace").replace("\r", "")
            for marker in PROMPT_MARKERS:
                if marker in text:
                    raise SystemExit(
                        f"{label} prompted for console input via {marker!r}\n{text[:1200]}"
                    )
            if any(marker in text for marker in READY_MARKERS):
                return

        text = output.decode("utf-8", errors="replace").replace("\r", "")
        raise SystemExit(f"{label} did not reach GUI defaults before timeout\n{text[:1200]}")
    finally:
        try:
            os.killpg(proc.pid, signal.SIGTERM)
        except ProcessLookupError:
            pass
        try:
            proc.wait(timeout=2)
        except subprocess.TimeoutExpired:
            os.killpg(proc.pid, signal.SIGKILL)
        os.close(master)


def main() -> None:
    if os.name == "nt":
        print("startup launch PTY test skipped on Windows")
        return

    global pty
    import pty

    if len(sys.argv) != 2:
        raise SystemExit("usage: startup_launch_contract.py PATH_TO_QEGTRAIN")

    app = Path(sys.argv[1]).resolve()
    if not app.exists():
        raise SystemExit(f"QEGTRAIN executable not found: {app}")

    assert_deferred_success_path_resets()
    source = (ROOT / "EGTRAIN/QEGTRAIN/app/MainWindow.cpp").read_text(encoding="utf-8")
    for name in ("paintStationPlatform", "arcDrawing", "paintConnection", "paintSignal"):
        start = source.index(f"void MainWindow::{name}(")
        if "fitInView(" in source[start:source.index("\n}\n", start)]:
            raise SystemExit(f"{name} refits the growing startup scene instead of deferring one final fit")
    assert_startup_timing_contract(app)
    assert_no_argument_chooser_continuation(app)
    assert_launch_reaches_defaults(app, [], "no-argument launch")
    assert_launch_reaches_defaults(app, ["-n", "1", "-g", "1"], "partial-argument launch")


if __name__ == "__main__":
    main()
