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
import zipfile
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
LAUNCH_TIMEOUT_SECONDS = 60 * max(1, int(os.environ.get("EGTRAIN_TEST_TIMEOUT_SCALE", "1") or "1"))
# A file name with an e acute and two CJK characters; the CJK characters are outside the Western Windows code page.
NON_ASCII_NAME = "Paimpol \u00e9\u6771\u4eac.egscene"
HEADLESS_OPTIONS = ["-g", "0", "-h", "1", "-pax", "0", "-TSM", "0", "-RC", "0"]
REJECTION_LINE = "ERROR: Cannot open"
# The test hook QEGTRAIN_E2E_OPEN_REQUEST prints the status line of the window after the open event.
STATUS_PREFIX = "E2E_OPEN_REQUEST_STATUS="


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
def run_app(
    app: Path, args: list[str], temp: Path, label: str, chooser: bool = False, extra_env: Optional[dict[str, str]] = None
) -> tuple[int, str]:
    env = os.environ.copy()
    env.update(extra_env or {})
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
    if rejection is not None and not any(
        line.startswith(REJECTION_LINE) and rejection in line for line in output.splitlines()
    ):
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
    # The scene is built into a run of its own, which replaces the run of the controller once it is complete: after the last failure
    # path, with the copy of the settings, which the reset of the run returns to the default.
    if "make_unique<SimulationRun>()" not in prepare:
        raise SystemExit("scene preparation does not stage the run")
    for builder in ("buildInfrastructureAndSignallingFromScene(*staged,", "buildOperationsFromScene(*staged,"):
        if builder not in prepare:
            raise SystemExit(f"scene preparation does not call {builder} on the staged run")
    if "mutableRun().adopt(std::move(*staged));" not in prepare:
        raise SystemExit("scene preparation does not put the staged run in place of the run of the controller")
    if "staged->settings = initial_variables;" not in prepare or prepare.index("staged->settings = initial_variables;") < prepare.index("buildOperationsFromScene("):
        raise SystemExit("scene preparation does not copy the settings into the run after the scene is built")
    if prepare.index("mutableRun().adopt(") < prepare.rindex("resetState();") or prepare.index("mutableRun().adopt(") < prepare.index("staged->settings = initial_variables;"):
        raise SystemExit("scene preparation replaces the run of the controller before the run is complete")

    begin_preparation = dispatch[
        dispatch.index("void DispatchController::beginScenePreparation()"):
        dispatch.index("void DispatchController::resetState()")
    ]
    if "prepareNativeOperationsState(mutableRun());" not in begin_preparation:
        raise SystemExit("lightweight scene preparation no longer prepares operation state")
    for reset in ("resetNativeOperationsState(", "resetNativeInfrastructureState("):
        if reset in begin_preparation:
            raise SystemExit(f"lightweight scene preparation incorrectly calls {reset}")

    full_reset = dispatch[
        dispatch.index("void DispatchController::resetState()"):
        dispatch.index("std::shared_ptr<const GuiSimulationSnapshot>")
    ]
    for reset in ("resetNativeInfrastructureState(mutableRun());", "resetNativeOperationsState(mutableRun());"):
        if reset not in full_reset:
            raise SystemExit(f"full dispatch reset no longer calls {reset}")
    # The lists of the run are cleared by SimulationRun::reset(), which the operation state calls.
    for name in ("BlocksOccupied", "BlocksConnected", "ETCS_MA"):
        for label, body in (("lightweight scene preparation", begin_preparation), ("full dispatch reset", full_reset)):
            if f"{name}.clear();" in body:
                raise SystemExit(f"{label} clears {name} itself instead of resetting the run")

    operations = (ROOT / "EGTRAIN/QEGTRAIN/simulation/builders/OperationsBuilder.cpp").read_text(encoding="utf-8")
    lightweight = operations[
        operations.index("void prepareNativeOperationsState(SimulationRun& run)"):
        operations.index("void resetNativeOperationsState(SimulationRun& run)")
    ]
    full_operations_reset = operations[
        operations.index("void resetNativeOperationsState(SimulationRun& run)"):
        operations.index("std::vector<SceneDiagnostic> buildOperationsFromScene(")
    ]
    if "run.reset();" not in lightweight:
        raise SystemExit("operation preparation does not reset the run")
    # The trains are state of the run too: its reset releases their storage, so neither function names them.
    moved = (
        "simulationIncidents", "VCmsgTimestep", "VCmsgTrain", "VCmsgText", "AllStationPlatforms", "AllStationPlatformIndex",
        "AllDailyPassengers", "AllDailyPassengerIndex", "numAllStationPlatforms", "numAllDailyPassengers",
        "regional_train", "numRegions", "nativeClearRegionalTrains",
    )
    for label, body in (("operation preparation", lightweight), ("full operation reset", full_operations_reset)):
        for name in moved:
            if name in body:
                raise SystemExit(f"{label} handles {name} itself instead of resetting the run")
    if "prepareNativeOperationsState(run);" not in full_operations_reset:
        raise SystemExit("full operation reset does not combine the preparation with the reset of the run")

    run = (ROOT / "EGTRAIN/QEGTRAIN/simulation/SimulationRun.cpp").read_text(encoding="utf-8")
    run_reset = run[run.index("void SimulationRun::reset()"):run.index("namespace {")]
    if "swap(trains);" not in run_reset or "numTrains = 0;" not in run_reset:
        raise SystemExit("the reset of the run does not release the train storage")
    if "settings = InitialParameters(0);" not in run_reset:
        raise SystemExit("the reset of the run does not return the settings to the default")

    # The infrastructure is not reset with the operations: the scene builder replaces it, and the window still shows the
    # previous one until then. Only the full reset of the infrastructure clears it, through the run.
    if "infra" in run[run.index("void SimulationRun::reset()"):run.index("void SimulationRun::resetInfrastructure()")]:
        raise SystemExit("the reset of the run clears the infrastructure, which the scene builder replaces")
    infra_reset = run[run.index("void RunInfrastructure::reset()"):run.index("void SimulationRun::reset()")]
    if "swap(sections);" not in infra_reset or "numRoutes = 0;" not in infra_reset:
        raise SystemExit("the reset of the infrastructure does not release the section storage and the routes")
    builder = (ROOT / "EGTRAIN/QEGTRAIN/simulation/builders/InfrastructureBuilder.cpp").read_text(encoding="utf-8")
    native_reset = builder[builder.index("void nativeResetRuntime(SimulationRun& run)"):builder.index("bool nativeHasErrors(")]
    if "run.resetInfrastructure();" not in native_reset:
        raise SystemExit("the native runtime reset does not reset the infrastructure of the run")
    for name in ("train_route", "signalling_block_sections", "blockSets", "connections", "StationArray", "list_of_TDS"):
        if name in native_reset:
            raise SystemExit(f"the native runtime reset handles {name} itself instead of resetting the run")

    # The state of a run is reached through the run: no header of simulation/ declares a global for it, and the run defines none.
    for header in sorted((ROOT / "EGTRAIN/QEGTRAIN/simulation").glob("*.h")):
        for line in header.read_text(encoding="utf-8").splitlines():
            if line.startswith("extern ") and line != "extern InitialParameters initial_variables;":
                raise SystemExit(f"{header.name} declares a global: {line}")
    if "= theRun." in run:
        raise SystemExit("SimulationRun.cpp defines a global name for a member of the run")


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
    calls = re.findall(r'getCmdOption\(\s*argv\s*,\s*argv\s*\+\s*argc\s*,\s*"([^"]+)"\s*\)', source)
    read = set(calls)
    # Every call except the definition must have the form above, or its option would be missed.
    if len(re.findall(r"(?<!char\* )getCmdOption\(", source)) != len(calls):
        raise SystemExit('app/main.cpp: a getCmdOption call is not written as getCmdOption(argv, argv + argc, "-x")')
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


# A scene of another version or layout ends the start with exit code 1 and the reason on standard error, in a launch
# without a window and in one with a window. The window launch hands --scene to the window without loading the scene.
# QEGTRAIN_AUTOSTART suppresses the dialogs of the window; a preload would stop at a message box that nobody closes.
def assert_version_rejections_name_the_reason(app: Path) -> None:
    draft = RUN_DIR / "tests/fixtures/scene-versions/draft-layout-Paimpol"
    with tempfile.TemporaryDirectory(prefix="qegtrain-version-rejection-") as root_name:
        root = Path(root_name)
        newer = root / "newer"
        newer.mkdir()
        (newer / "scene.json").write_text(
            json.dumps({"name": "newer", "schema_version": 2, "saved_with_app_version": "9.9"}), encoding="utf-8"
        )
        newer_bundle = root / "newer.egscene"
        with zipfile.ZipFile(newer_bundle, "w") as archive:
            archive.writestr("scene.json", json.dumps({"format": "egscene", "bundle_version": 2, "schema_version": 1}))
            archive.writestr("future-layout.json", "future")
        cases = (
            ("a draft scene", ["--scene", str(draft)], ["scene.compatibility.draft", "2026-08-08", "Load Legacy Case"]),
            ("a newer scene", ["--scene", str(newer)], ["scene.compatibility.newer", "schema 2 (saved with EGTRAIN 9.9)"]),
            ("a newer bundle", [str(newer_bundle)], ["scene.compatibility.newer", "schema 1, bundle layout 2"]),
        )
        for index, (name, args, details) in enumerate(cases):
            label = f"headless launch with {name}"
            returncode, output = run_app(app, args + HEADLESS_OPTIONS, root / f"run-headless-{index}", label)
            if returncode != 1:
                raise SystemExit(f"{label} exited with {returncode}, expected 1\n{tail(output)}")
            missing = [detail for detail in details if detail not in output]
            if missing or "End of Simulation" in output:
                raise SystemExit(f"{label} did not report {missing!r} or ran the scene\n{tail(output)}")
        print("headless launches with a draft, a newer and a newer bundle scene exit with 1 and name the reason")

        window_cases = (
            ("a draft scene", ["--scene", str(draft)], ["scene.compatibility.draft", "Load Legacy Case"]),
            ("a newer scene", ["--scene", str(newer)], ["scene.compatibility.newer", "schema 2 (saved with EGTRAIN 9.9)"]),
        )
        for index, (name, args, details) in enumerate(window_cases):
            label = f"window launch with {name}"
            returncode, output = run_app(app, args, root / f"run-window-{index}", label, extra_env={"QEGTRAIN_AUTOSTART": "1"})
            if returncode != 1:
                raise SystemExit(f"{label} exited with {returncode}, expected 1\n{tail(output)}")
            missing = [detail for detail in details if detail not in output]
            if missing:
                raise SystemExit(f"{label} did not report {missing!r}\n{tail(output)}")
        print("window launches with --scene and a draft or a newer scene exit with 1 and name the reason")
        # A file name that does not open is reported, and the start goes on as without a file name.
        assert_no_argument_chooser_continuation(
            app, (str(newer_bundle),), "window launch with a newer bundle", "scene.compatibility.newer"
        )
        print("a window launch with a newer bundle named the reason and started as without a file name")


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


# A window launch, with the arguments args and, when request is given, a QFileOpenEvent for that
# file at the moment "launch", "startup", "chooser" or "running" (the test hook
# QEGTRAIN_E2E_OPEN_REQUEST). The window opens the files in wanted and adds them to the recent
# cases. It does not open the chooser, except for the moment "chooser", where the event comes from
# inside the open chooser and the chooser must give way, and for "startup" off macOS, where nothing
# waits for the event before the chooser opens. A file in refused does not open: the status line
# says so and the file stays out of the recent cases. The window stays open, so the check stops it.
def assert_window_opens_files(app: Path, args: list[str], request: Optional[Path], moment: str, wanted: list[Path], refused: list[Path], temp: Path, label: str) -> None:
    env = os.environ.copy()
    env["QT_QPA_PLATFORM"] = "offscreen"
    env["QEGTRAIN_OUTPUT_DIR"] = str(temp / "out")
    env["QEGTRAIN_E2E_SETTINGS_DIR"] = str(temp / "settings")
    # That variable makes the chooser close itself and end the run, which the chooser moments must not.
    chooser_may_open = moment in ("startup", "chooser")
    if not chooser_may_open:
        env["QEGTRAIN_E2E_STARTUP_CHOOSER"] = "1"
    for name in ("QEGTRAIN_E2E_OPEN_REQUEST", "QEGTRAIN_E2E_OPEN_REQUEST_AT"):
        env.pop(name, None)
    if request is not None:
        env["QEGTRAIN_E2E_OPEN_REQUEST"] = str(request)
        env["QEGTRAIN_E2E_OPEN_REQUEST_AT"] = moment
    if sys.platform.startswith("linux"):
        # Qt reads the environment with the locale there.
        env["LC_ALL"] = "C.UTF-8"
    ini = temp / "settings/EGTRAIN/EGTRAIN.ini"
    temp.mkdir()
    log_path = temp / "window.log"

    def log_text() -> str:
        return log_path.read_text(encoding="utf-8", errors="replace")

    problem = ""
    with log_path.open("wb") as log:
        proc = subprocess.Popen(
            [str(app), *args],
            cwd=RUN_DIR,
            env=env,
            stdout=log,
            stderr=subprocess.STDOUT,
            start_new_session=True,
        )
        try:
            if wanted and not wait_until(lambda: all(ini_form(path) in read_ini(ini) for path in wanted), 60, proc):
                problem = f"{label} did not add every file to the recent cases within 60 s"
            elif request is not None and not wait_until(lambda: STATUS_PREFIX in log_text(), 60, proc):
                problem = f"{label} did not report the status line after the open event within 60 s"
            if not problem:
                time.sleep(2)
            if not problem and proc.poll() is not None:
                problem = f"{label} ended with {proc.poll()} instead of staying open"
            if not problem and any(ini_form(path) in read_ini(ini) for path in refused):
                problem = f"{label} added a file that does not open to the recent cases"
        finally:
            terminate_process_group(proc)
    text = log_text()
    if not problem and request is not None and "E2E_OPEN_REQUEST_SENT" not in text:
        problem = f"{label} did not send the open event"
    status = next((line for line in text.splitlines() if line.startswith(STATUS_PREFIX)), "")
    if not problem and request is not None and ("could not be opened" in status) != bool(refused):
        problem = f"{label} shows the status line {status[len(STATUS_PREFIX):]!r} after the open event"
    if not problem and "E2E_STARTUP_CHOOSER_" in text:
        problem = f"{label} opened the chooser"
    sent = next((line for line in text.splitlines() if line.startswith("E2E_OPEN_REQUEST_SENT")), "")
    if not problem and request is not None and "E2E_OPEN_REQUEST_CHOOSER_AFTER=no" not in text:
        problem = f"{label} left the chooser open after the open event"
    if not problem and moment == "chooser" and not sent.endswith("chooser=yes"):
        problem = f"{label} sent the open event while no chooser was open ({sent!r})"
    # macOS waits a moment before it opens the chooser, so an event that comes then is seen first.
    if not problem and moment == "startup" and sys.platform == "darwin" and not sent.endswith("chooser=no"):
        problem = f"{label} opened the chooser before the open event ({sent!r})"
    if problem:
        raise SystemExit(f"{problem}\n{tail(text)}")


# How the settings file spells a path: Qt writes a character outside ASCII as \x and its code.
def ini_form(path: Path) -> str:
    return "".join(c if ord(c) < 128 else "\\x%x" % ord(c) for c in str(path))


def wait_until(condition, timeout: float, proc: subprocess.Popen) -> bool:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline and proc.poll() is None:
        if condition():
            return True
        time.sleep(0.25)
    return condition()


def read_ini(ini: Path) -> str:
    return ini.read_text(encoding="utf-8", errors="replace") if ini.is_file() else ""


# A launch that only names a file hands it to the window that runs and ends with 0, and the window
# opens it like File > Open Case Study. The window is a test run with the relay switched on. It was
# started with a file, or without one so that the case chooser is open; the chooser then gives way.
# The second launch has no test variable, as when the system opens a file. A scripted launch that
# has a test variable and no switch opens the file in a window of its own. A file that does not load
# is handed over last, because the window then shows a message box that holds back further files.
# The socket of the relay lies in a folder of this check, so that other runs do not meet it.
def assert_file_launch_reaches_running_window(app: Path, first: Optional[Path], handed: list[Path], malformed: Optional[Path], scripted: Optional[Path], temp: Path) -> None:
    label = "file launch while a window runs" if first else "file launch while the chooser is open"
    temp.mkdir()
    sockets = Path(tempfile.mkdtemp(prefix="egtrelay-", dir="/tmp"))
    base = os.environ.copy()
    base["QT_QPA_PLATFORM"] = "offscreen"
    base["TMPDIR"] = str(sockets)
    # The home folder names the relay of the user, so every launch has the same one, and it is a
    # throwaway folder: the second launch has no settings folder of its own.
    home = temp / "home"
    home.mkdir()
    base["HOME"] = base["CFFIXED_USER_HOME"] = str(home)
    if sys.platform.startswith("linux"):
        base["LC_ALL"] = "C.UTF-8"
    window_env = {**base, "QEGTRAIN_OUTPUT_DIR": str(temp / "out"), "QEGTRAIN_E2E_SETTINGS_DIR": str(temp / "settings"), "QEGTRAIN_E2E_OPEN_RELAY": "1"}
    window_env.pop("QEGTRAIN_E2E_STARTUP_CHOOSER", None)
    # Should the relay fail, the second launch is a window with the settings of a throwaway folder.
    plain_env = {k: v for k, v in base.items() if not k.startswith("QEGTRAIN_E2E_")}
    plain_env.update({"QEGTRAIN_OUTPUT_DIR": str(temp / "plain-out"), "XDG_CONFIG_HOME": str(temp / "plain-config"), "QEGTRAIN_DISABLE_UPDATES": "1"})
    scripted_env = {k: v for k, v in base.items() if k != "QEGTRAIN_E2E_OPEN_RELAY"}
    scripted_env.update({"QEGTRAIN_OUTPUT_DIR": str(temp / "scripted-out"), "QEGTRAIN_E2E_SETTINGS_DIR": str(temp / "scripted-settings")})
    ini = temp / "settings/EGTRAIN/EGTRAIN.ini"
    scripted_ini = temp / "scripted-settings/EGTRAIN/EGTRAIN.ini"
    window_log = temp / "window.log"
    scripted_log = temp / "scripted.log"
    problem = ""
    procs: list[subprocess.Popen] = []
    try:
        with window_log.open("wb") as log:
            window = subprocess.Popen([str(app)] + ([str(first)] if first else []), cwd=RUN_DIR, env=window_env, stdout=log, stderr=subprocess.STDOUT, start_new_session=True)
        procs.append(window)
        if first and not wait_until(lambda: ini_form(first) in read_ini(ini), 60, window):
            problem = f"{label}: the window did not open the first file within 60 s"
        elif not wait_until(lambda: any(sockets.glob("egtrain-open-*")), 60, window):
            problem = f"{label}: the window did not start to listen within 60 s"
        else:
            # The listener has just been created; give it a moment to be ready.
            time.sleep(0.5)
        for bundle, opens in [(b, True) for b in handed] + ([(malformed, False)] if malformed else []):
            if problem:
                break
            try:
                second = subprocess.run([str(app), str(bundle)], cwd=RUN_DIR, env=plain_env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                    text=True, encoding="utf-8", errors="replace", timeout=30)
            except subprocess.TimeoutExpired:
                problem = f"{label}: the second launch with {ascii(bundle.name)} did not end within 30 s"
                break
            if second.returncode != 0:
                problem = f"{label}: the second launch with {ascii(bundle.name)} ended with {second.returncode}, expected 0\n{tail(second.stdout)}"
            elif opens and not wait_until(lambda: ini_form(bundle) in read_ini(ini), 30, window):
                problem = f"{label}: the window did not open {ascii(bundle.name)} within 30 s"
            elif not opens:
                time.sleep(2)
                if ini_form(bundle) in read_ini(ini):
                    problem = f"{label}: the window added {ascii(bundle.name)}, which does not load, to the recent cases"
        if not problem and window.poll() is not None:
            problem = f"{label}: the window ended with {window.poll()} instead of staying open"
        if not problem and scripted:
            with scripted_log.open("wb") as log:
                other = subprocess.Popen([str(app), str(scripted)], cwd=RUN_DIR, env=scripted_env, stdout=log, stderr=subprocess.STDOUT, start_new_session=True)
            procs.append(other)
            if not wait_until(lambda: ini_form(scripted) in read_ini(scripted_ini), 60, other):
                problem = f"{label}: a scripted launch did not open its file in a window of its own within 60 s"
            elif ini_form(scripted) in read_ini(ini):
                problem = f"{label}: a scripted launch handed its file to the running window"
    finally:
        for proc in procs:
            terminate_process_group(proc)
        shutil.rmtree(sockets, ignore_errors=True)
    if problem:
        raise SystemExit(f"{problem}\n{tail(window_log.read_text(encoding='utf-8', errors='replace'))}")


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
            ("a malformed bundle", [str(malformed)], "not a ZIP archive"),
            ("two bundles", [str(spaced), str(second)], "open exactly one .egscene file"),
        ):
            assert_no_argument_chooser_continuation(app, tuple(args), f"window launch with {name}", reason)
        print("window launches with a file that does not open start as without an argument")

        if os.name == "nt":
            print("window launch with a bundle argument and file launches while a window runs skipped on Windows: the settings file escapes backslashes")
        else:
            assert_window_opens_files(app, [str(spaced)], None, "", [spaced], [], root / "run-window", "window launch with a bundle argument")
            print("window launch with a bundle argument added it to the recent cases and kept the chooser closed")
            # An open event is what the system sends when it opens a file with the program, on macOS.
            assert_window_opens_files(app, [], spaced, "launch", [spaced], [], root / "run-event-launch", "window launch with an open event")
            assert_window_opens_files(app, [], unicode_bundle, "launch", [unicode_bundle], [], root / "run-event-unicode", "window launch with an open event for a non-ASCII file name")
            assert_window_opens_files(app, [str(spaced)], second, "running", [spaced, second], [], root / "run-event-running", "running window with an open event")
            assert_window_opens_files(app, [], malformed, "launch", [], [malformed], root / "run-event-malformed", "window launch with an open event for a malformed file")
            print("open events at launch and in a running window opened the files, with spaces and non-ASCII characters, kept the chooser closed and kept a malformed file out of the recent cases")
            # The system hands over the file that started the program after the startup callback has run.
            assert_window_opens_files(app, [], second, "startup", [second], [], root / "run-event-startup",
                "window launch with an open event after the start")
            assert_window_opens_files(app, [], spaced, "chooser", [spaced], [], root / "run-event-chooser",
                "window launch with an open event in the chooser")
            print("an open event just after the start and one from inside the open chooser opened their files, and the chooser gave way")
            scripted = spaced.parent / "Scripted.egscene"
            shutil.copyfile(second, scripted)
            assert_file_launch_reaches_running_window(app, spaced, [second, unicode_bundle], malformed, scripted, root / "run-relay")
            assert_file_launch_reaches_running_window(app, None, [second], None, None, root / "run-relay-chooser")
            print("file launches while a window runs were taken by that window, which closed its chooser and kept a malformed file out of the recent cases; a scripted launch kept its own window")


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
    source = (ROOT / "EGTRAIN/QEGTRAIN/graphics/NetworkPresenter.cpp").read_text(encoding="utf-8")
    for name in ("paintStationPlatform", "arcDrawing", "paintConnection", "paintSignal"):
        start = source.index(f"void NetworkPresenter::{name}(")
        if "fitInView(" in source[start:source.index("\n}\n", start)]:
            raise SystemExit(f"{name} refits the growing startup scene instead of deferring one final fit")
    assert_option_list_matches_reads()
    assert_startup_timing_contract(app)
    assert_no_argument_chooser_continuation(app)
    assert_open_argument_contract(app, scene_tool)
    assert_version_rejections_name_the_reason(app)
    if os.name == "nt":
        print("startup launch PTY checks skipped on Windows")
        return
    global pty
    import pty

    assert_launch_reaches_defaults(app, [], "no-argument launch")
    assert_launch_reaches_defaults(app, ["-n", "1", "-g", "1"], "partial-argument launch")


if __name__ == "__main__":
    main()
