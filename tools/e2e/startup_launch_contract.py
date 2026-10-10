#!/usr/bin/env python3
import json
import os
import re
import select
import shutil
import signal
import subprocess
import sys
import tempfile
import time
from pathlib import Path
from typing import Optional

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools/performance"))
from startup_benchmark import validate_records

RUN_DIR = ROOT / "EGTRAIN/QEGTRAIN"
SCENE = RUN_DIR / "Scenes/Paimpol"
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
# These are functional launch checks, not startup performance thresholds.
# Allow instrumented Debug builds to finish both fresh and warm scene preparation.
LAUNCH_TIMEOUT_SECONDS = 60
# A file name with characters outside the Western Windows code page (e acute, then two CJK characters).
NON_ASCII_NAME = "Paimpol \u00e9\u6771\u4eac.egscene"
HEADLESS_OPTIONS = ["-g", "0", "-h", "1", "-pax", "0", "-TSM", "0", "-RC", "0"]
REJECTION_LINE = "ERROR: Cannot open"


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
                encoding="utf-8",
                errors="replace",
                timeout=LAUNCH_TIMEOUT_SECONDS,
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


def ascii_text(text: str) -> str:
    return text.encode("ascii", "backslashreplace").decode("ascii")


def tail(text: str) -> str:
    return ascii_text(text[-4000:])


# Runs QEGTRAIN to its end with the output and the settings in folders of its own below temp.
# The result is the exit code and the standard output and error together.
def run_app(app: Path, args: list[str], temp: Path, label: str, chooser: bool = False) -> tuple[int, str]:
    env = os.environ.copy()
    env["QT_QPA_PLATFORM"] = "offscreen"
    env["QEGTRAIN_OUTPUT_DIR"] = str(temp / "out")
    env["QEGTRAIN_E2E_SETTINGS_DIR"] = str(temp / "settings")
    env.pop("QEGTRAIN_E2E_STARTUP_CHOOSER", None)
    if chooser:
        env["QEGTRAIN_E2E_STARTUP_CHOOSER"] = "1"
    if sys.platform.startswith("linux") and not all(arg.isascii() for arg in args):
        # Qt reads the arguments with the locale there.
        env["LC_ALL"] = "C.UTF-8"
    try:
        proc = subprocess.run(
            [str(app), *args],
            cwd=RUN_DIR,
            env=env,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            encoding="utf-8",
            errors="replace",
            timeout=LAUNCH_TIMEOUT_SECONDS,
        )
    except subprocess.TimeoutExpired as exc:
        output = exc.stdout or ""
        if isinstance(output, bytes):
            output = output.decode("utf-8", errors="replace")
        raise SystemExit(f"{label} timed out\n{tail(output)}") from exc
    return proc.returncode, proc.stdout


def assert_no_argument_chooser_continuation(
    app: Path,
    args: tuple[str, ...] = (),
    label: str = "no-argument chooser continuation",
    rejection: Optional[str] = None,
) -> None:
    with tempfile.TemporaryDirectory(prefix="qegtrain-chooser-continuation-") as temp:
        returncode, output = run_app(app, list(args), Path(temp), label, chooser=True)
    if returncode != 0:
        raise SystemExit(f"{label} exited with {returncode}\n{tail(output)}")

    values = {}
    for line in output.splitlines():
        if line.startswith("E2E_STARTUP_CHOOSER_") and "=" in line:
            key, value = line.split("=", 1)
            values[key] = value
    expected = {
        "E2E_STARTUP_CHOOSER_PROMPT": (
            "Current case: Netherlands. Choose another or continue."
        ),
        "E2E_STARTUP_CHOOSER_ACTION": "Continue",
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
    if rejection is not None and (REJECTION_LINE not in output or rejection not in output):
        mismatches.append(f"the output has no {REJECTION_LINE!r} line with {rejection!r}")
    if mismatches:
        raise SystemExit(
            f"{label} state mismatch: "
            + "; ".join(mismatches)
            + f"\n{tail(output)}"
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
    if "regional_train" in lightweight or "nativeClearRegionalTrains" in lightweight:
        raise SystemExit("lightweight operation preparation releases the regional_train storage")
    if "prepareNativeOperationsState();" not in full_operations_reset or "regional_train" not in full_operations_reset:
        raise SystemExit("full operation reset does not combine lightweight clearing with releasing the trains")


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


def assert_option_list_matches_reads() -> None:
    source = (RUN_DIR / "app/main.cpp").read_text(encoding="utf-8")
    read = set(re.findall(r'getCmdOption\(\s*argv\s*,\s*argv\s*\+\s*argc\s*,\s*"([^"]+)"\s*\)', source))
    declared = re.search(r"optionsWithValue\s*=\s*\{([^}]*)\}", source)
    if not read or not declared:
        raise SystemExit("app/main.cpp: the options read with getCmdOption or the list optionsWithValue were not found")
    listed = set(re.findall(r'"([^"]+)"', declared.group(1)))
    if listed != read:
        raise SystemExit(
            "app/main.cpp: optionsWithValue differs from the options read with getCmdOption: "
            f"missing {sorted(read - listed)}, extra {sorted(listed - read)}"
        )


def make_bundles(scene_tool: Path, root: Path) -> tuple[Path, Path, Path]:
    # scene_tool is a console program whose narrow argv cannot carry the non-ASCII name on Windows,
    # so it writes a plain name and Python copies the file.
    packed = root / "packed.egscene"
    proc = subprocess.run(
        [str(scene_tool), "pack", str(SCENE), str(packed)],
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        encoding="utf-8",
        errors="replace",
        timeout=LAUNCH_TIMEOUT_SECONDS,
    )
    if proc.returncode != 0 or not packed.is_file():
        raise SystemExit(f"scene_tool could not pack {SCENE.name}\n{tail(proc.stdout)}")
    spaced = root / "folder with spaces" / "Paimpol.egscene"
    spaced.parent.mkdir()
    shutil.copyfile(packed, spaced)
    second = spaced.parent / "Second.egscene"
    shutil.copyfile(packed, second)
    unicode_bundle = root / NON_ASCII_NAME
    shutil.copyfile(packed, unicode_bundle)
    return spaced, second, unicode_bundle


def assert_headless_bundle_runs(app: Path, args: list[str], temp: Path, label: str) -> None:
    returncode, output = run_app(app, args + HEADLESS_OPTIONS, temp, label)
    if returncode != 0:
        raise SystemExit(f"{label} exited with {returncode}\n{tail(output)}")
    if "End of Simulation" not in output:
        raise SystemExit(f"{label} printed no 'End of Simulation'\n{tail(output)}")
    if REJECTION_LINE in output:
        raise SystemExit(f"{label} reported a rejection\n{tail(output)}")
    # The bundle holds Paimpol; the default case is Netherlands.
    if not (temp / "out/Output/Paimpol").is_dir() or (temp / "out/Output/Netherlands").exists():
        raise SystemExit(f"{label} did not run the scene of the bundle\n{tail(output)}")


def assert_headless_rejection(app: Path, args: list[str], temp: Path, label: str, reason: str) -> None:
    returncode, output = run_app(app, args + HEADLESS_OPTIONS, temp, label)
    if returncode != 1:
        raise SystemExit(f"{label} exited with {returncode}, expected 1\n{tail(output)}")
    if REJECTION_LINE not in output or reason not in output:
        raise SystemExit(f"{label} did not report {reason!r}\n{tail(output)}")


def assert_unicode_argument_arrives_intact(app: Path, root: Path) -> None:
    # An existing file that is no bundle: the loader rejects it, so the exit code is 1, but the
    # file name was found.
    label = "launch with an existing non-ASCII file"
    existing = root / "existing" / NON_ASCII_NAME
    existing.parent.mkdir()
    existing.write_bytes(b"not a bundle")
    returncode, output = run_app(app, [str(existing), *HEADLESS_OPTIONS], root / "run-existing", label)
    if returncode != 1:
        raise SystemExit(f"{label} exited with {returncode}, expected 1\n{tail(output)}")
    if "the file does not exist" in output or REJECTION_LINE in output:
        raise SystemExit(f"{label}: the file name was not found or was rejected\n{tail(output)}")

    label = "launch with a missing non-ASCII file"
    missing = root / "missing" / NON_ASCII_NAME
    returncode, output = run_app(app, [str(missing), *HEADLESS_OPTIONS], root / "run-missing", label)
    if returncode != 1:
        raise SystemExit(f"{label} exited with {returncode}, expected 1\n{tail(output)}")
    if "the file does not exist" not in output or NON_ASCII_NAME not in output:
        raise SystemExit(f"{label}: the file name was not reported unchanged\n{tail(output)}")
    print(f"non-ASCII file name {ascii(NON_ASCII_NAME)}: found when it exists, reported unchanged when missing")


def terminate_process_group(proc: subprocess.Popen) -> None:
    try:
        os.killpg(proc.pid, signal.SIGTERM)
    except ProcessLookupError:
        return
    try:
        proc.wait(timeout=5)
    except subprocess.TimeoutExpired:
        os.killpg(proc.pid, signal.SIGKILL)
        proc.wait(timeout=5)


# The window opens the bundle, adds it to the recent cases and does not open the chooser. The
# window stays open, so the check stops it.
def assert_window_opens_bundle_argument(app: Path, bundle: Path, temp: Path) -> None:
    label = "window launch with a bundle argument"
    env = os.environ.copy()
    env["QT_QPA_PLATFORM"] = "offscreen"
    env["QEGTRAIN_OUTPUT_DIR"] = str(temp / "out")
    env["QEGTRAIN_E2E_SETTINGS_DIR"] = str(temp / "settings")
    env["QEGTRAIN_E2E_STARTUP_CHOOSER"] = "1"
    ini = temp / "settings/EGTRAIN/EGTRAIN.ini"
    temp.mkdir()
    log_path = temp / "window.log"
    problem = ""
    with log_path.open("wb") as log:
        proc = subprocess.Popen(
            [str(app), str(bundle)],
            cwd=RUN_DIR,
            env=env,
            stdout=log,
            stderr=subprocess.STDOUT,
            start_new_session=True,
        )
        try:
            deadline = time.monotonic() + 60
            while proc.poll() is None:
                if ini.is_file() and str(bundle) in ini.read_text(encoding="utf-8", errors="replace"):
                    break
                if time.monotonic() >= deadline:
                    problem = f"{label} did not add the bundle to the recent cases within 60 s"
                    break
                time.sleep(0.25)
            if not problem:
                time.sleep(2)
            if not problem and proc.poll() is not None:
                problem = f"{label} ended with {proc.poll()} instead of staying open"
        finally:
            terminate_process_group(proc)
    text = log_path.read_text(encoding="utf-8", errors="replace")
    if not problem and "E2E_STARTUP_CHOOSER_" in text:
        problem = f"{label} opened the chooser"
    if problem:
        raise SystemExit(f"{problem}\n{tail(text)}")


def assert_open_argument_contract(app: Path, scene_tool: Path) -> None:
    with tempfile.TemporaryDirectory(prefix="qegtrain-open-argument-") as root_name:
        root = Path(root_name)
        spaced, second, unicode_bundle = make_bundles(scene_tool, root)
        notes = root / "notes.txt"
        notes.write_bytes(b"not a scene")
        malformed = root / "malformed.egscene"
        malformed.write_bytes(b"not a bundle")
        folder = root / "folder.egscene"
        folder.mkdir()

        assert_headless_bundle_runs(app, [str(spaced)], root / "run-spaces", "headless launch with a bundle in a folder with spaces")
        print("headless launch with a bundle in a folder with spaces ran the scene of the bundle")
        if os.name == "nt":
            print("headless launch with a non-ASCII bundle name skipped on Windows: the scene layer builds file paths from narrow strings")
        else:
            assert_headless_bundle_runs(app, [str(unicode_bundle)], root / "run-unicode", "headless launch with a non-ASCII bundle name")
            print(f"headless launch with the bundle name {ascii(NON_ASCII_NAME)} ran the scene of the bundle")
        assert_unicode_argument_arrives_intact(app, root)

        assert_headless_rejection(app, [str(notes)], root / "run-text", "headless launch with a text file", "the file must use the .egscene extension")
        assert_headless_rejection(app, [str(spaced), str(second)], root / "run-two", "headless launch with two bundles", "open exactly one .egscene file")
        assert_headless_rejection(app, [str(folder)], root / "run-folder", "headless launch with a folder", "an .egscene file is needed, not a directory")
        # The text file would be rejected if it were read as the file name.
        assert_headless_bundle_runs(app, ["--scene", str(spaced), str(notes)], root / "run-scene-wins", "headless launch with --scene and a file name")
        print("headless launches with a file that is rejected exit with 1; --scene wins over a file name")

        for name, args, reason in (
            ("a text file", [str(notes)], "the file must use the .egscene extension"),
            ("a malformed bundle", [str(malformed)], ""),
            ("two bundles", [str(spaced), str(second)], "open exactly one .egscene file"),
        ):
            assert_no_argument_chooser_continuation(app, tuple(args), f"window launch with {name}", reason)
        print("window launches with a file that does not open start as without an argument")

        if os.name == "nt":
            print("window launch with a bundle argument skipped on Windows: the settings file escapes backslashes")
        else:
            assert_window_opens_bundle_argument(app, spaced, root / "run-window")
            print("window launch with a bundle argument added it to the recent cases and kept the chooser closed")


def main() -> None:
    if len(sys.argv) != 3:
        raise SystemExit("usage: startup_launch_contract.py PATH_TO_QEGTRAIN PATH_TO_SCENE_TOOL")

    app = Path(sys.argv[1]).resolve()
    if not app.exists():
        raise SystemExit(f"QEGTRAIN executable not found: {app}")
    scene_tool = Path(sys.argv[2]).resolve()
    if not scene_tool.exists():
        raise SystemExit(f"scene_tool executable not found: {scene_tool}")

    assert_deferred_success_path_resets()
    source = (ROOT / "EGTRAIN/QEGTRAIN/app/MainWindow.cpp").read_text(encoding="utf-8")
    for name in ("paintStationPlatform", "arcDrawing", "paintConnection", "paintSignal"):
        start = source.index(f"void MainWindow::{name}(")
        if "fitInView(" in source[start:source.index("\n}\n", start)]:
            raise SystemExit(f"{name} refits the growing startup scene instead of deferring one final fit")
    assert_option_list_matches_reads()
    assert_startup_timing_contract(app)
    assert_no_argument_chooser_continuation(app)
    assert_open_argument_contract(app, scene_tool)
    if os.name == "nt":
        print("startup launch PTY checks skipped on Windows")
        return
    global pty
    import pty

    assert_launch_reaches_defaults(app, [], "no-argument launch")
    assert_launch_reaches_defaults(app, ["-n", "1", "-g", "1"], "partial-argument launch")


if __name__ == "__main__":
    main()
