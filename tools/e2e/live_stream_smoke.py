#!/usr/bin/env python3
"""Smoke test of the live stream (--live, --live-every, --live-commands).

  live_stream_smoke.py QEGTRAIN LIVE_CLIENT [--second-run]

Runs the application with a stream and reads it with live_client:

  * a headless run that a client follows from start to end, with the same output as a run without a stream;
  * a client that leaves in the middle of a run and a run that nobody follows, both with that output too;
  * a client that joins after the run has ended (window run);
  * with --second-run, a second run in the same process (scripted window run);
  * the options that are refused, and the notice for the legacy -TSM and -RC options.

The scripted window run of --second-run is checked on macOS only, so CMake passes the option there.
"""
import argparse
import glob
import json
import os
import shutil
import socket
import subprocess
import sys
import tempfile
import time
from pathlib import Path
from typing import Dict, List, Optional, Tuple

ROOT = Path(__file__).resolve().parents[2]
SCENES = ROOT / "EGTRAIN" / "QEGTRAIN" / "Scenes"
HORIZON = 1500
EVERY = 25
START_TIMEOUT = 120
RUN_TIMEOUT = 180
# Files that differ between two runs of the same input: the timings of the run.
TIMING_FILES = {"Computing_Times.txt"}


class Failure(Exception):
    pass


def tail(path: Path, lines: int = 40) -> str:
    try:
        return "\n".join(path.read_text(encoding="utf-8", errors="replace").splitlines()[-lines:])
    except OSError:
        return ""


class App:
    """One run of the application with its own folders and logs."""

    def __init__(self, app: Path, root: Path, scene: str, arguments: List[str], window: bool = False,
                 extra_env: Optional[Dict[str, str]] = None):
        self.root = root
        self.output = root / "output"
        self.output.mkdir(parents=True)
        self.stdout_path = root / "stdout.txt"
        self.stderr_path = root / "stderr.txt"
        env = os.environ.copy()
        env.update({"QT_QPA_PLATFORM": "offscreen", "QEGTRAIN_OUTPUT_DIR": str(self.output),
                    "QEGTRAIN_E2E_SETTINGS_DIR": str(root / "settings")})
        env.update(extra_env or {})
        command = [str(app), "--scene", str(SCENES / scene), "-g", "1" if window else "0"]
        if "-TSM" not in arguments:
            command += ["-TSM", "0", "-RC", "0"]
        command += arguments
        self.stdout = open(self.stdout_path, "wb")
        self.stderr = open(self.stderr_path, "wb")
        self.process = subprocess.Popen(command, cwd=root, env=env, stdout=self.stdout, stderr=self.stderr)

    def endpoint_file(self) -> Path:
        """The live-endpoint.json of the run, once the application has written all of it."""
        deadline = time.time() + START_TIMEOUT
        while time.time() < deadline:
            for name in glob.glob(str(self.output / "Output" / "*" / "live-endpoint.json")):
                try:
                    if json.loads(Path(name).read_text(encoding="utf-8")).get("state"):
                        return Path(name)
                except (OSError, ValueError):
                    pass
            if self.process.poll() is not None:
                raise Failure("the application ended before it wrote the endpoint file (exit %s)\n%s"
                              % (self.process.returncode, tail(self.stderr_path)))
            time.sleep(0.01)
        raise Failure("no endpoint file after %d s" % START_TIMEOUT)

    def wait(self, timeout: int = RUN_TIMEOUT) -> int:
        try:
            code = self.process.wait(timeout=timeout)
        except subprocess.TimeoutExpired:
            raise Failure("the application did not end within %d s\n%s" % (timeout, tail(self.stdout_path)))
        finally:
            self.close()
        return code

    def stop(self) -> None:
        """Ends a process that is still running. Only the process that this object started."""
        if self.process.poll() is None:
            self.process.terminate()
            try:
                self.process.wait(timeout=10)
            except subprocess.TimeoutExpired:
                self.process.kill()
                self.process.wait(timeout=10)
        self.close()

    def close(self) -> None:
        self.stdout.close()
        self.stderr.close()

    def result_folders(self) -> List[Path]:
        return sorted(p for p in (self.output / "Output").glob("*") if p.is_dir())


def run_client(client: Path, endpoint: Path, arguments: List[str]) -> Tuple[int, List[dict], str]:
    """Runs live_client and returns its exit status, the summary of each run it saw and its standard error."""
    process = subprocess.run([str(client), str(endpoint)] + arguments, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                             text=True, encoding="utf-8", errors="replace", timeout=RUN_TIMEOUT)
    summaries = [json.loads(line) for line in process.stdout.splitlines() if line.strip()]
    return process.returncode, summaries, process.stderr


def expect(condition: bool, message: str) -> None:
    if not condition:
        raise Failure(message)


def files_of(folder: Path) -> Dict[str, bytes]:
    found = {}
    for path in sorted(folder.rglob("*")):
        if path.is_file() and path.name not in TIMING_FILES:
            found[path.relative_to(folder).as_posix()] = path.read_bytes()
    return found


def same_results(expected: App, actual: App, what: str) -> None:
    """The result files of the two runs are the same, and the stream has left no file behind."""
    left, right = expected.result_folders(), actual.result_folders()
    expect(len(left) == 1 and len(right) == 1 and left[0].name == right[0].name,
           "%s: the result folders differ: %s and %s" % (what, left, right))
    expect(not (right[0] / "live-endpoint.json").exists(), "%s: live-endpoint.json is left behind" % what)
    a, b = files_of(left[0]), files_of(right[0])
    expect(sorted(a) == sorted(b), "%s: the files differ: only in the run without a stream %s, only in the run with one %s"
           % (what, sorted(set(a) - set(b)), sorted(set(b) - set(a))))
    for name in a:
        expect(a[name] == b[name], "%s: %s differs from the run without a stream" % (what, name))
    expect("Live stream" not in expected.stderr_path.read_text(encoding="utf-8", errors="replace"),
           "%s: a run without --live mentions the stream" % what)


def check_run(summary: dict, what: str, horizon: int, every: int, states: int) -> None:
    """A run that the client read to its end. A fast run can lose state frames, so only a minimum is asked for."""
    expect(summary["status"] == "completed", "%s: the run did not end as completed: %s" % (what, summary))
    expect(summary["steps_finished"] == horizon, "%s: steps_finished is not %d: %s" % (what, horizon, summary))
    expect(summary["horizon_steps"] == horizon, "%s: wrong horizon: %s" % (what, summary))
    expect(summary["states"] >= states, "%s: fewer than %d state frames: %s" % (what, states, summary))
    expect(summary["first_step"] % every == 0 and summary["last_step"] % every == 0 and summary["last_step"] >= summary["first_step"],
           "%s: the steps are not those of --live-every %d: %s" % (what, every, summary))
    expect(summary["positions"] > 0 and summary["moving"] > 0, "%s: no train with a position and a speed: %s" % (what, summary))
    expect(summary["trains"] > 0 and summary["scene_name"], "%s: the hello frame is missing or empty: %s" % (what, summary))


def baseline(app: Path, temp: Path) -> App:
    run = App(app, temp / "baseline", "Paimpol", ["-h", str(HORIZON)])
    expect(run.wait() == 0, "the run without a stream failed\n%s" % tail(run.stdout_path))
    return run


def stream_run(app: Path, client: Path, temp: Path, reference: App) -> None:
    run = App(app, temp / "stream", "Paimpol", ["-h", str(HORIZON), "--live", "0", "--live-commands", "--live-every", str(EVERY)])
    try:
        code, summaries, errors = run_client(client, run.endpoint_file(), ["--runs", "1"])
        expect(code == 0 and len(summaries) == 1, "the client found a problem (exit %d):\n%s" % (code, errors))
        check_run(summaries[0], "followed run", HORIZON, EVERY, 5)
        expect(run.wait() == 0, "the run with a stream failed\n%s" % tail(run.stdout_path))
        expect("Live stream on tcp://127.0.0.1:" in run.stderr_path.read_text(encoding="utf-8", errors="replace"),
               "the application did not say where the stream is")
        same_results(reference, run, "followed run")
    finally:
        run.stop()
    print("PASS a client follows a run from start to end, and the results are those of a run without a stream")


def disconnect_run(app: Path, client: Path, temp: Path, reference: App) -> None:
    run = App(app, temp / "disconnect", "Paimpol", ["-h", str(HORIZON), "--live", "0", "--live-every", "10"])
    try:
        code, summaries, errors = run_client(client, run.endpoint_file(), ["--max-states", "3"])
        expect(code == 0 and len(summaries) == 1 and summaries[0]["states"] >= 3, "the client did not read 3 frames:\n%s" % errors)
        expect(run.wait() == 0, "the run ended badly after the client had left\n%s" % tail(run.stdout_path))
        same_results(reference, run, "run after a client left")
    finally:
        run.stop()
    print("PASS a client that leaves in the middle of a run does not change the run")


def unwatched_run(app: Path, temp: Path, reference: App) -> None:
    run = App(app, temp / "unwatched", "Paimpol", ["-h", str(HORIZON), "--live", "0", "--live-commands"])
    try:
        expect(run.wait() == 0, "the run nobody follows failed\n%s" % tail(run.stdout_path))
        same_results(reference, run, "run nobody follows")
    finally:
        run.stop()
    print("PASS a run that nobody follows ends, with the results of a run without a stream")


def completed_run(app: Path, client: Path, temp: Path) -> None:
    """A window run with a stream: a client that comes after the run has ended gets the hello and no state."""
    run = App(app, temp / "completed", "Paimpol", ["-h", "300", "--live", "0", "--live-commands", "--live-every", "50"], window=True,
              extra_env={"QEGTRAIN_AUTOSTART": "1"})
    try:
        endpoint = run.endpoint_file()
        code, summaries, errors = run_client(client, endpoint, ["--runs", "1"])
        expect(code == 0 and len(summaries) == 1, "the client found a problem (exit %d):\n%s" % (code, errors))
        check_run(summaries[0], "window run", 300, 50, 3)
        code, late, errors = run_client(client, endpoint, ["--runs", "0"])
        expect(code == 0 and len(late) == 1, "the late client found a problem (exit %d):\n%s" % (code, errors))
        expect(late[0]["run_id"] == summaries[0]["run_id"] and late[0]["states"] == 0 and not late[0]["joined_late"],
               "a client after the run did not get the hello of that run alone: %s and %s" % (late, summaries))
        expect(run.process.poll() is None, "the application ended with the client after the run")
    finally:
        run.stop()
    print("PASS a client that comes after a run has ended gets its hello and the application goes on")


def second_run(app: Path, client: Path, temp: Path) -> None:
    """A scripted window run that runs the case a second time in the same process."""
    scene = SCENES / "Assignment_Gvc_Gdg_Ut"
    run = App(app, temp / "second", "Assignment_Gvc_Gdg_Ut", ["-h", "600", "-pax", "0", "--live", "0", "--live-commands", "--live-every", "100"],
              window=True, extra_env={"QEGTRAIN_AUTOSTART": "1", "QEGTRAIN_E2E_OPERATIONAL_COMPLETION": str(scene)})
    try:
        code, summaries, errors = run_client(client, run.endpoint_file(), ["--runs", "2"])
        expect(code == 0 and len(summaries) == 2, "the client did not read two runs (exit %d):\n%s" % (code, errors))
        expect(summaries[0]["run_id"] != summaries[1]["run_id"], "the two runs have the same run id: %s" % summaries)
        for number, summary in enumerate(summaries, 1):
            check_run(summary, "run %d" % number, 600, 100, 3)
            expect(not summary["joined_late"], "run %d: the client did not see the hello frame: %s" % (number, summary))
        expect(run.wait() == 0, "the scripted window run failed\n%s" % tail(run.stdout_path))
    finally:
        run.stop()
    print("PASS a second run in the same process has a new run id, a new hello and its own frames")


def refused_options(app: Path, temp: Path) -> None:
    cases = [
        (["--live", "x"], 1, "--live requires a port"),
        (["--live", "70000"], 1, "--live requires a port"),
        (["--live"], 1, "--live requires a port"),
        (["--live", "0", "--live-every", "0"], 1, "--live-every requires"),
        (["--live-every", "5"], 1, "need --live"),
        (["--live-commands"], 1, "need --live"),
    ]
    for number, (arguments, status, message) in enumerate(cases):
        run = App(app, temp / ("refused-%d" % number), "Paimpol", ["-h", "10"] + arguments)
        code = run.wait()
        text = run.stderr_path.read_text(encoding="utf-8", errors="replace")
        expect(code == status and message in text, "%s: exit %d, expected %d with %r:\n%s" % (arguments, code, status, message, text))
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as busy:
        busy.bind(("127.0.0.1", 0))
        busy.listen(1)
        run = App(app, temp / "port-in-use", "Paimpol", ["-h", "10", "--live", str(busy.getsockname()[1])])
        code = run.wait()
        text = run.stderr_path.read_text(encoding="utf-8", errors="replace")
        expect(code == 2 and "Cannot start the live stream" in text, "a port in use: exit %d:\n%s" % (code, text))
    print("PASS options that are not valid, or a port in use, stop the start before a run")


def legacy_notice(app: Path, temp: Path) -> None:
    run = App(app, temp / "legacy", "Paimpol", ["-h", "1", "-TSM", "1"])
    expect(run.wait() == 0, "the run with -TSM 1 failed\n%s" % tail(run.stdout_path))
    lines = [line for line in run.stderr_path.read_text(encoding="utf-8", errors="replace").splitlines() if "legacy options" in line]
    expect(len(lines) == 1 and "--live" in lines[0], "-TSM 1 did not give one line pointing to --live: %s" % lines)
    plain = App(app, temp / "no-legacy", "Paimpol", ["-h", "1"])
    expect(plain.wait() == 0, "the run without -TSM failed\n%s" % tail(plain.stdout_path))
    expect("legacy options" not in plain.stderr_path.read_text(encoding="utf-8", errors="replace"), "a run without -TSM and -RC gave the notice")
    print("PASS -TSM 1 prints one line on standard error that points to --live")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("app", type=Path)
    parser.add_argument("client", type=Path)
    parser.add_argument("--second-run", action="store_true", help="also run the case with a second run in one process")
    arguments = parser.parse_args()
    app, client = arguments.app.resolve(), arguments.client.resolve()
    for path in (app, client, SCENES / "Paimpol"):
        if not path.exists():
            raise SystemExit("not found: %s" % path)

    temp = Path(tempfile.mkdtemp(prefix="qegtrain-live-"))
    try:
        reference = baseline(app, temp)
        stream_run(app, client, temp, reference)
        disconnect_run(app, client, temp, reference)
        unwatched_run(app, temp, reference)
        completed_run(app, client, temp)
        if arguments.second_run:
            second_run(app, client, temp)
        refused_options(app, temp)
        legacy_notice(app, temp)
    except Failure as failure:
        print("FAIL %s" % failure, file=sys.stderr)
        print("logs: %s" % temp, file=sys.stderr)
        raise SystemExit(1)
    shutil.rmtree(temp, ignore_errors=True)


if __name__ == "__main__":
    main()
