#!/usr/bin/env python3
"""Smoke test of the live stream (--live, --live-every, --live-commands).

  live_stream_smoke.py QEGTRAIN LIVE_CLIENT [--second-run]

Runs the application with a stream and reads it with live_client:

  * a headless run that a client follows from start to end, with the same output as a run without a stream;
  * a client that sends one set_delay_ms request: the server accepts it, the run reports it as applied, and the output is
    that of a run without a stream; a request that the server rejects changes nothing;
  * a client that leaves in the middle of a run and a run that nobody follows, both with that output too;
  * a headless run of a scene that repeats its services, so that the trains of a service are told apart by their occurrence;
  * a client that joins after the run has ended, and a run that is stopped (window runs);
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
# The run that receives a command is longer, so that the client has the hello and the run is still going on when it sends.
COMMAND_HORIZON = 3000
# EGTRAIN_TEST_TIMEOUT_SCALE (set by CTest in a build with -DEGTRAIN_TEST_TIMEOUT_SCALE) stretches every wait, for builds that run slowly.
SCALE = max(1, int(os.environ.get("EGTRAIN_TEST_TIMEOUT_SCALE", "1") or "1"))
START_TIMEOUT = 120 * SCALE
RUN_TIMEOUT = 180 * SCALE
CLIENT_TIMEOUT = 60 * SCALE
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
                 extra_env: Optional[Dict[str, str]] = None, answers: str = ""):
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
        for legacy in ("-TSM", "-RC"):
            if legacy not in arguments and "--interactive" not in arguments:
                command += [legacy, "0"]
        command += arguments
        (root / "answers.txt").write_text(answers, encoding="utf-8")
        self.stdin = open(root / "answers.txt", "rb")
        self.stdout = open(self.stdout_path, "wb")
        self.stderr = open(self.stderr_path, "wb")
        self.process = subprocess.Popen(command, cwd=root, env=env, stdin=self.stdin, stdout=self.stdout, stderr=self.stderr)

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
        self.stdin.close()
        self.stdout.close()
        self.stderr.close()

    def result_folders(self) -> List[Path]:
        return sorted(p for p in (self.output / "Output").glob("*") if p.is_dir())


def run_client(client: Path, endpoint: Path, arguments: List[str]) -> Tuple[int, List[dict], str]:
    """Runs live_client and returns its exit status, the summary of each run it saw and its standard error."""
    process = subprocess.run([str(client), str(endpoint), "--timeout", str(CLIENT_TIMEOUT)] + arguments, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
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
    """The result files and the standard output of the two runs are the same, and the stream has left no file behind."""
    left, right = expected.result_folders(), actual.result_folders()
    expect(len(left) == 1 and len(right) == 1 and left[0].name == right[0].name,
           "%s: the result folders differ: %s and %s" % (what, left, right))
    expect(not (right[0] / "live-endpoint.json").exists(), "%s: live-endpoint.json is left behind" % what)
    a, b = files_of(left[0]), files_of(right[0])
    expect(sorted(a) == sorted(b), "%s: the files differ: only in the run without a stream %s, only in the run with one %s"
           % (what, sorted(set(a) - set(b)), sorted(set(b) - set(a))))
    for name in a:
        expect(a[name] == b[name], "%s: %s differs from the run without a stream" % (what, name))
    expect(expected.stdout_path.read_bytes() == actual.stdout_path.read_bytes(),
           "%s: the standard output differs from the run without a stream" % what)
    expect("Live stream" not in expected.stderr_path.read_text(encoding="utf-8", errors="replace"),
           "%s: a run without --live mentions the stream" % what)


def check_scene(summary: dict, what: str, scene: str) -> None:
    """The hello frame names the start time of the scene and trains that are services of the scene, each once."""
    hours, minutes, seconds = (int(part) for part in json.loads((SCENES / scene / "scene.json").read_text(encoding="utf-8"))["base_time"].split(":"))
    expect(summary["base_time_s"] == hours * 3600 + minutes * 60 + seconds, "%s: base_time_s is not the start time of %s: %s" % (what, scene, summary))
    known = {service["id"] for service in json.loads((SCENES / scene / "services.json").read_text(encoding="utf-8"))["services"]}
    services = [tuple(service) for service in summary["services"]]
    expect(len(services) == summary["trains"] and len(set(services)) == len(services),
           "%s: the trains of the hello frame do not differ in service_id and occurrence: %s" % (what, summary["services"]))
    expect(all(service_id in known and isinstance(occurrence, int) for service_id, occurrence in services),
           "%s: a train is no service of %s: %s" % (what, scene, summary["services"]))


def check_run(summary: dict, what: str, scene: str, horizon: Optional[int], every: int, states: int) -> None:
    """A run that the client read to its end. A fast run can lose state frames, so only a minimum is asked for.

    Without a horizon the run is the whole scene and the hello frame says how long it is."""
    check_scene(summary, what, scene)
    if horizon is None:
        horizon = summary["horizon_steps"]
    expect(summary["status"] == "completed", "%s: the run did not end as completed: %s" % (what, summary))
    expect(summary["steps_finished"] == horizon, "%s: steps_finished is not %d: %s" % (what, horizon, summary))
    expect(summary["horizon_steps"] == horizon, "%s: wrong horizon: %s" % (what, summary))
    expect(summary["states"] >= states, "%s: fewer than %d state frames: %s" % (what, states, summary))
    expect(summary["first_step"] % every == 0 and summary["last_step"] % every == 0 and summary["last_step"] >= summary["first_step"],
           "%s: the steps are not those of --live-every %d: %s" % (what, every, summary))
    expect(summary["positions"] > 0 and summary["moving"] > 0, "%s: no train with a position and a speed: %s" % (what, summary))
    expect(summary["trains"] > 0 and summary["scene_name"], "%s: the hello frame is missing or empty: %s" % (what, summary))


def baseline(app: Path, temp: Path, horizon: int = HORIZON, name: str = "baseline") -> App:
    run = App(app, temp / name, "Paimpol", ["-h", str(horizon)])
    expect(run.wait() == 0, "the run without a stream failed\n%s" % tail(run.stdout_path))
    return run


def stream_run(app: Path, client: Path, temp: Path, reference: App) -> None:
    run = App(app, temp / "stream", "Paimpol", ["-h", str(HORIZON), "--live", "0", "--live-commands", "--live-every", str(EVERY)])
    try:
        code, summaries, errors = run_client(client, run.endpoint_file(), ["--runs", "1"])
        expect(code == 0 and len(summaries) == 1, "the client found a problem (exit %d):\n%s" % (code, errors))
        check_run(summaries[0], "followed run", "Paimpol", HORIZON, EVERY, 5)
        expect(run.wait() == 0, "the run with a stream failed\n%s" % tail(run.stdout_path))
        expect("Live stream on tcp://127.0.0.1:" in run.stderr_path.read_text(encoding="utf-8", errors="replace"),
               "the application did not say where the stream is")
        same_results(reference, run, "followed run")
    finally:
        run.stop()
    print("PASS a client follows a run from start to end, and the results are those of a run without a stream")


def command_run(app: Path, client: Path, temp: Path) -> None:
    """A client sends one set_delay_ms. The run applies it between two steps, which changes no result."""
    reference = baseline(app, temp, COMMAND_HORIZON, "baseline-command")
    run = App(app, temp / "command", "Paimpol", ["-h", str(COMMAND_HORIZON), "--live", "0", "--live-commands", "--live-every", str(EVERY)])
    try:
        code, summaries, errors = run_client(client, run.endpoint_file(), ["--runs", "1", "--set-delay-ms", "1"])
        expect(code == 0 and len(summaries) == 1, "the client found a problem (exit %d):\n%s" % (code, errors))
        summary = summaries[0]
        check_run(summary, "run with a command", "Paimpol", COMMAND_HORIZON, EVERY, 5)
        reply = summary["command"]
        expect(reply and reply["status"] == "accepted" and reply["run_id"] == summary["run_id"],
               "the server did not accept set_delay_ms for the run: %s" % summary)
        expect(summary["rejected"] == [], "the run turned the command down: %s" % summary)
        expect(len(summary["applied"]) == 1, "the run did not report the command as applied once: %s" % summary)
        applied = summary["applied"][0]
        expect(applied["command_id"] == reply["id"] and applied["cmd"] == "set_delay_ms" and 0 < applied["step"] < COMMAND_HORIZON,
               "the applied event does not belong to the request: %s and %s" % (reply, applied))
        expect(run.wait() == 0, "the run with a command failed\n%s" % tail(run.stdout_path))
        same_results(reference, run, "run with a command")
    finally:
        run.stop()
    print("PASS a client sends set_delay_ms, the run reports it as applied, and the results are those of a run without a stream")


def rejected_command_run(app: Path, client: Path, temp: Path) -> None:
    """A request with a delay above the limit is answered with a reason, and the run is not changed."""
    run = App(app, temp / "rejected", "Paimpol", ["-h", str(HORIZON), "--live", "0", "--live-commands", "--live-every", str(EVERY)])
    try:
        code, summaries, errors = run_client(client, run.endpoint_file(), ["--runs", "1", "--set-delay-ms", "5000"])
        expect(code == 0 and len(summaries) == 1, "the client found a problem (exit %d):\n%s" % (code, errors))
        summary = summaries[0]
        reply = summary["command"]
        expect(reply and reply["status"] == "rejected" and reply["reason"] == "bad_argument",
               "the server did not reject the delay above the limit with bad_argument: %s" % summary)
        expect(summary["applied"] == [] and summary["rejected"] == [], "the run reports a command that the server rejected: %s" % summary)
        expect(summary["status"] == "completed", "the run did not complete: %s" % summary)
        expect(run.wait() == 0, "the run failed\n%s" % tail(run.stdout_path))
    finally:
        run.stop()
    print("PASS a request that the server rejects is answered with a reason and does not reach the run")


def disconnect_run(app: Path, client: Path, temp: Path, reference: App) -> None:
    run = App(app, temp / "disconnect", "Paimpol", ["-h", str(HORIZON), "--live", "0", "--live-every", "10"])
    try:
        code, summaries, errors = run_client(client, run.endpoint_file(), ["--max-states", "3"])
        expect(code == 0 and len(summaries) == 1 and summaries[0]["states"] >= 3, "the client did not read 3 frames:\n%s" % errors)
        expect(summaries[0]["last_step"] < HORIZON // 2, "the client did not leave in the first half of the run: %s" % summaries[0])
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


def repeated_services_run(app: Path, client: Path, temp: Path) -> None:
    """The scene repeats each of its services, so trains of one service are told apart by their occurrence.

    The run is the whole scene, which takes seconds; a shorter run ends before the client has joined."""
    scene = "Assignment_Gvc_Gdg_Ut"
    run = App(app, temp / "repeated", scene, ["--live", "0", "--live-commands", "--live-every", "100"])
    try:
        code, summaries, errors = run_client(client, run.endpoint_file(), ["--runs", "1"])
        expect(code == 0 and len(summaries) == 1, "the client found a problem (exit %d):\n%s" % (code, errors))
        check_run(summaries[0], "repeated services", scene, None, 100, 3)
        expect(len({service_id for service_id, _ in summaries[0]["services"]}) < summaries[0]["trains"],
               "the trains of the repeated services do not share a service_id: %s" % summaries[0]["services"])
        expect(run.wait() == 0, "the run failed\n%s" % tail(run.stdout_path))
    finally:
        run.stop()
    print("PASS the trains of a repeated service differ in their occurrence")


def completed_run(app: Path, client: Path, temp: Path) -> None:
    """A window run with a stream: a client that comes after the run has ended gets the hello and no state."""
    run = App(app, temp / "completed", "Paimpol", ["-h", "300", "--live", "0", "--live-commands", "--live-every", "50"], window=True,
              extra_env={"QEGTRAIN_AUTOSTART": "1"})
    try:
        endpoint = run.endpoint_file()
        code, summaries, errors = run_client(client, endpoint, ["--runs", "1"])
        expect(code == 0 and len(summaries) == 1, "the client found a problem (exit %d):\n%s" % (code, errors))
        check_run(summaries[0], "window run", "Paimpol", 300, 50, 3)
        code, late, errors = run_client(client, endpoint, ["--runs", "0"])
        expect(code == 0 and len(late) == 1, "the late client found a problem (exit %d):\n%s" % (code, errors))
        expect(late[0]["run_id"] == summaries[0]["run_id"] and late[0]["states"] == 0 and not late[0]["joined_late"],
               "a client after the run did not get the hello of that run alone: %s and %s" % (late, summaries))
        expect(run.process.poll() is None, "the application ended with the client after the run")
    finally:
        run.stop()
    print("PASS a client that comes after a run has ended gets its hello and the application goes on")


def cancelled_run(app: Path, client: Path, temp: Path) -> None:
    """A window run that is stopped at its first displayed step publishes the steps that ran and the status cancelled."""
    run = App(app, temp / "cancelled", "Paimpol", ["-h", str(HORIZON), "--live", "0", "--live-commands"], window=True,
              extra_env={"QEGTRAIN_AUTOSTART": "1", "QEGTRAIN_E2E_CLOSE_DURING_RUN": "1"})
    try:
        code, summaries, errors = run_client(client, run.endpoint_file(), ["--runs", "1"])
        expect(code == 0 and len(summaries) == 1, "the client found a problem (exit %d):\n%s" % (code, errors))
        summary = summaries[0]
        expect(summary["status"] == "cancelled", "the stopped run did not end as cancelled: %s" % summary)
        expect(summary["steps_finished"] is not None and summary["steps_finished"] < HORIZON, "the stopped run ran all its steps: %s" % summary)
        expect(not summary["joined_late"], "the client did not see the hello frame of the stopped run: %s" % summary)
        expect(run.wait() == 0, "the application did not end after the stop\n%s" % tail(run.stdout_path))
    finally:
        run.stop()
    print("PASS a run that is stopped publishes steps_finished and run_finished with the status cancelled")


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
            check_run(summary, "run %d" % number, "Assignment_Gvc_Gdg_Ut", 600, 100, 3)
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
    """One line on standard error when -TSM or -RC is on, whether it comes from an option or from a question."""
    # --interactive asks for -TSM and then -RC, because the call gives -n and -g.
    cases = [("-TSM 1", ["-TSM", "1"], ""), ("-RC 1", ["-RC", "1"], ""), ("--interactive", ["--interactive", "-n", "2"], "1\n0\n")]
    for number, (what, arguments, answers) in enumerate(cases):
        run = App(app, temp / ("legacy-%d" % number), "Paimpol", ["-h", "1"] + arguments, answers=answers)
        expect(run.wait() == 0, "the run with %s failed\n%s" % (what, tail(run.stdout_path)))
        lines = [line for line in run.stderr_path.read_text(encoding="utf-8", errors="replace").splitlines() if "legacy options" in line]
        expect(len(lines) == 1 and "--live" in lines[0], "%s did not give one line pointing to --live: %s" % (what, lines))
    plain = App(app, temp / "no-legacy", "Paimpol", ["-h", "1"])
    expect(plain.wait() == 0, "the run without -TSM and -RC failed\n%s" % tail(plain.stdout_path))
    expect("legacy options" not in plain.stderr_path.read_text(encoding="utf-8", errors="replace"), "a run without -TSM and -RC gave the notice")
    print("PASS -TSM 1, -RC 1 and the answers of --interactive print one line on standard error that points to --live")


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
        command_run(app, client, temp)
        rejected_command_run(app, client, temp)
        disconnect_run(app, client, temp, reference)
        unwatched_run(app, temp, reference)
        repeated_services_run(app, client, temp)
        completed_run(app, client, temp)
        cancelled_run(app, client, temp)
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
