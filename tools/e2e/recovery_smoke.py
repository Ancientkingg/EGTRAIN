#!/usr/bin/env python3
"""End a window with unsaved edits without closing it, and recover the edits at the next start.

usage: recovery_smoke.py /path/to/QEGTRAIN /path/to/scene_tool

Every run starts QEGTRAIN offscreen with its own folder for recovery copies and its own settings folder, so that no run meets the
copies of a user. A run of the script edit-then-crash makes three edits to a bundle, waits until the autosave has written the copy and
ends with _Exit(137): no destructor runs and no close event reaches the window. The next run finds the copy and answers the prompt.

  bundle      The next start offers the copy. Recover opens it as an unsaved scene without a file, Save As starts at the file that the
              copy came from, and the original keeps its bytes and its modification time. After Save As and a close, no copy is left and
              the next start has no prompt.
  new case    A case that was never saved is recovered the same way.
  changed     The original changed, or is newer than the copy, or is gone. The prompt says so, Decide Later keeps the copy, Recover
              opens it and Discard removes it.
  discard     Discard at close leaves no copy and no prompt at the next start.
  unwritable  A folder for the copies that cannot be made does not stop the editing or a run, and the window reports it once.
  crash note  An uncaught exception after an autosave, and SIGSEGV without a copy, each leave one crash note. The next start names
              the note in the recovery prompt, or in a notice when there is no copy, and marks it reported, so the start after
              that does not name it again.
"""
from __future__ import annotations

import os
import signal
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
RUN_DIR = ROOT / "EGTRAIN" / "QEGTRAIN"
SCENE = RUN_DIR / "Scenes" / "Assignment_Gvc_Gdg_Ut"
APP_ARGUMENTS = ["-h", "100", "-g", "1", "-pax", "0"]
RUN_SECONDS = 300
CRASH_CODE = 137
EDITED_NAME = "Recovery smoke case"
EDITED_DESCRIPTION = "Recovery smoke description"
EDITED_BASE_TIME = "09:15:30"
HOUR_NS = 3600 * 10**9


def fail(message: str, output: str = "") -> None:
    tail = output[-3000:]
    raise SystemExit(f"FAIL {message}" + (f"\n{tail}" if tail else ""))


def require(condition: bool, message: str, output: str = "") -> None:
    if not condition:
        fail(message, output)


class Smoke:
    def __init__(self, app: Path, scene_tool: Path, temp: Path) -> None:
        self.app = app
        self.scene_tool = scene_tool
        self.temp = temp
        self.count = 0
        (temp / "settings").mkdir()

    def folder(self, name: str) -> Path:
        path = self.temp / name
        path.mkdir()
        return path

    def bundle(self, folder: Path) -> Path:
        """A packed copy of the committed scene."""
        bundle = folder / "source.egscene"
        subprocess.run([str(self.scene_tool), "pack", str(SCENE), str(bundle)], check=True, capture_output=True, timeout=120)
        return bundle

    def validate(self, bundle: Path) -> None:
        result = subprocess.run([str(self.scene_tool), "validate", str(bundle)], capture_output=True, text=True, timeout=120)
        require(result.returncode == 0, f"scene_tool does not validate {bundle.name}", result.stdout + result.stderr)

    def run(self, script: str, scene: Path, recovery_root: Path, *, answer: str = "", save_as: Path | None = None,
            code: int = 0, seconds: int = 120) -> str:
        """Starts the window on a scene, runs the script in it and returns its output. The exit code has to be code."""
        self.count += 1
        env = os.environ.copy()
        for name in [name for name in env if name.startswith("QEGTRAIN_")]:
            del env[name]
        env.update({
            "QT_QPA_PLATFORM": "offscreen",
            "QEGTRAIN_E2E_RECOVERY": script,
            "QEGTRAIN_E2E_SETTINGS_DIR": str(self.temp / "settings"),
            "QEGTRAIN_OUTPUT_DIR": str(self.temp / "output"),
            "QEGTRAIN_RECOVERY_DIR": str(recovery_root),
            "QEGTRAIN_CRASH_DIR": str(recovery_root.parent / "crashes"),
            "QEGTRAIN_RECOVERY_INTERVAL_MS": "100",
        })
        if answer:
            env["QEGTRAIN_E2E_RECOVERY_ANSWER"] = answer
        if save_as:
            env["QEGTRAIN_E2E_RECOVERY_SAVE_AS"] = str(save_as)
        command = [str(self.app), "--scene", str(scene), *APP_ARGUMENTS]
        try:
            result = subprocess.run(command, cwd=RUN_DIR, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=seconds)
        except subprocess.TimeoutExpired as error:
            fail(f"{script} did not end within {seconds} s", (error.stdout or b"").decode("utf-8", errors="replace"))
        output = result.stdout.decode("utf-8", errors="replace")
        require(result.returncode == code, f"{script} ended with {result.returncode}, not {code}", output)
        if code == 0:
            require("E2E_RECOVERY_OK" in output, f"{script} did not report E2E_RECOVERY_OK", output)
        return output


def fields(output: str, marker: str) -> dict[str, str]:
    """The key=value fields, separated by |, of the line that starts with marker."""
    for line in output.splitlines():
        if line.startswith(marker):
            body = line[len(marker):].lstrip("= ")
            return dict(part.split("=", 1) for part in body.split("|"))
    fail(f"the output has no {marker}", output)
    return {}


def instance_folders(root: Path) -> list[Path]:
    return sorted(path for path in root.iterdir() if path.is_dir()) if root.is_dir() else []


def copies(root: Path) -> list[Path]:
    """The instance folders that hold a copy."""
    return [path for path in instance_folders(root) if (path / "recovery.json").is_file()]


def snapshot(path: Path) -> tuple[bytes, int] | None:
    """The bytes and the modification time of a file, compared directly."""
    return (path.read_bytes(), path.stat().st_mtime_ns) if path.is_file() else None


def check_recovered(output: str, saved: Path, validate=None) -> None:
    state = fields(output, "E2E_RECOVERY_STATE")
    require(state["prompted"] == "1" and state["opened"] == "1", "the prompt did not open the recovered copy", output)
    require(state["name"] == EDITED_NAME and state["description"] == EDITED_DESCRIPTION and state["base_time"] == EDITED_BASE_TIME,
            "the recovered scene lacks the edits", output)
    require("E2E_RECOVERY_SAVED" in output, "Save As of the recovered scene did not report a saved scene", output)
    require(saved.is_file(), "Save As wrote no file", output)
    if validate:
        validate(saved)


def scenario_bundle(smoke: Smoke) -> None:
    folder = smoke.folder("bundle")
    root = folder / "recovery"
    bundle = smoke.bundle(folder)
    before = snapshot(bundle)
    output = smoke.run("edit-then-crash", bundle, root, code=CRASH_CODE)
    require("E2E_RECOVERY_WRITTEN" in output, "the autosave did not report its copy before the crash", output)
    require(len(copies(root)) == 1, f"the crash left {len(copies(root))} copies, not one")
    require(snapshot(bundle) == before, "the autosave changed the original")

    saved = folder / "saved.egscene"
    output = smoke.run("start-again", SCENE, root, answer="recover", save_as=saved)
    prompt = fields(output, "E2E_RECOVERY_PROMPT")
    require(prompt["rows"] == "1" and prompt["source"] == str(bundle) and prompt["note"] == "" and prompt["default"] == "recover",
            f"the prompt does not offer the copy of {bundle.name}: {prompt}", output)
    check_recovered(output, saved, smoke.validate)
    start = fields(output, "E2E_RECOVERY_SAVE_AS_START")
    require(Path(start["dir"]).resolve() == bundle.parent.resolve() and start["name"] == bundle.name,
            f"Save As does not start at the original: {start}", output)
    require(snapshot(bundle) == before, "recovering and Save As changed the original", output)
    require(not instance_folders(root), "a clean close left a recovery folder")

    output = smoke.run("start-again", SCENE, root)
    require(fields(output, "E2E_RECOVERY_STATE")["prompted"] == "0", "the next start offered a copy after a clean close", output)
    print("PASS a bundle is recovered after a crash and Save As leaves the original as it was")


def scenario_new_case(smoke: Smoke) -> None:
    folder = smoke.folder("new_case")
    root = folder / "recovery"
    bundle = smoke.bundle(folder)
    before = snapshot(bundle)
    smoke.run("new-then-crash", bundle, root, code=CRASH_CODE)
    require(len(copies(root)) == 1, "the crash of a new case left no copy")

    saved = folder / "saved.egscene"
    output = smoke.run("start-again", SCENE, root, answer="recover", save_as=saved)
    prompt = fields(output, "E2E_RECOVERY_PROMPT")
    require(prompt["rows"] == "1" and prompt["source"] == "Not saved yet" and prompt["default"] == "recover",
            f"the prompt does not offer a new case: {prompt}", output)
    # A new case has no services and no rolling stock, so it is a saved scene that scene_tool would not accept as complete.
    check_recovered(output, saved)
    require(snapshot(bundle) == before, "the file that was open when the new case was made changed", output)
    require(not instance_folders(root), "a clean close left a recovery folder")
    print("PASS a case that was never saved is recovered after a crash")


def scenario_original_changes(smoke: Smoke) -> None:
    for kind, note in (("changed", "changed after"), ("newer", "newer than this copy"), ("missing", "missing")):
        folder = smoke.folder(kind)
        root = folder / "recovery"
        bundle = smoke.bundle(folder)
        smoke.run("edit-then-crash", bundle, root, code=CRASH_CODE)
        stat = bundle.stat()
        if kind == "changed":
            os.utime(bundle, ns=(stat.st_atime_ns, stat.st_mtime_ns - HOUR_NS))
        elif kind == "newer":
            os.utime(bundle, ns=(stat.st_atime_ns, stat.st_mtime_ns + HOUR_NS))
        else:
            bundle.unlink()
        before = snapshot(bundle)

        output = smoke.run("start-again", SCENE, root, answer="later")
        prompt = fields(output, "E2E_RECOVERY_PROMPT")
        require(note in prompt["note"], f"the prompt does not say that the original is {kind}: {prompt}", output)
        require(prompt["default"] == ("recover" if kind == "missing" else "later"),
                f"the prompt makes the wrong button the default for a {kind} original: {prompt}", output)
        require(fields(output, "E2E_RECOVERY_STATE")["opened"] == "0", "Decide Later opened the copy", output)
        require(len(copies(root)) == 1, f"Decide Later removed the copy of a {kind} original")

        if kind == "newer":
            output = smoke.run("start-again", SCENE, root, answer="discard")
            require(fields(output, "E2E_RECOVERY_STATE")["opened"] == "0", "Discard opened the copy", output)
            require(not instance_folders(root), "Discard left the copy")
        else:
            saved = folder / "saved.egscene"
            output = smoke.run("start-again", SCENE, root, answer="recover", save_as=saved)
            check_recovered(output, saved, smoke.validate)
            require(not instance_folders(root), "a clean close left a recovery folder")
        require(snapshot(bundle) == before, f"the {kind} original was changed by the recovery", output)
    print("PASS a changed, a newer and a missing original are named and the copy stays until the user decides")


def scenario_discard(smoke: Smoke) -> None:
    folder = smoke.folder("discard")
    root = folder / "recovery"
    smoke.run("close-after-discard", SCENE, root)
    require(not instance_folders(root), "Discard at close left a recovery folder")
    output = smoke.run("start-again", SCENE, root)
    require(fields(output, "E2E_RECOVERY_STATE")["prompted"] == "0", "the next start offered a copy after Discard", output)
    print("PASS Discard at close leaves no copy and no prompt")


def scenario_unwritable(smoke: Smoke) -> None:
    folder = smoke.folder("unwritable")
    blocker = folder / "blocker"
    blocker.write_text("a file where the folder for the copies should be\n")
    smoke.run("fail-to-autosave", SCENE, blocker / "recovery", seconds=RUN_SECONDS)
    require(blocker.is_file(), "the blocker was replaced")
    print("PASS a folder for the copies that cannot be made does not stop the editing or a run")


def crash_note(folder: Path, cause: str, output: str) -> Path:
    """The one unreported crash note in the folder, which has to name the cause, and the path it gets once it is reported."""
    notes = sorted((folder / "crashes").glob("crash-*.txt"))
    require(len(notes) == 1, f"the crash left {len(notes)} crash notes, not one", output)
    text = notes[0].read_text()
    require(text.startswith("EGTRAIN ") and cause in text, f"the crash note does not name {cause}:\n{text}", output)
    return notes[0].with_name("reported-" + notes[0].name)


def require_reported(folder: Path, reported: Path, output: str) -> None:
    require(reported.is_file() and not list((folder / "crashes").glob("crash-*.txt")), "the start did not mark the note reported", output)


def scenario_crash_note(smoke: Smoke) -> None:
    folder = smoke.folder("crash_note")
    root = folder / "recovery"
    bundle = smoke.bundle(folder)
    output = smoke.run("edit-then-throw", bundle, root, code=-signal.SIGABRT)
    require("E2E_RECOVERY_WRITTEN" in output, "the autosave did not report its copy before the exception", output)
    reported = crash_note(folder, "Uncaught exception: injected crash", output)

    output = smoke.run("start-again", SCENE, root)
    require(f"E2E_RECOVERY_CRASH_NOTE {reported}" in output.splitlines(), "the recovery prompt does not name the crash note", output)
    require(len(copies(root)) == 1, "the copy was not kept after Decide Later", output)
    require_reported(folder, reported, output)

    output = smoke.run("start-again", SCENE, root)
    require("E2E_RECOVERY_CRASH_NOTE none" in output.splitlines(), "the next prompt named the reported crash note again", output)

    folder = smoke.folder("crash_notice")
    root = folder / "recovery"
    output = smoke.run("open-then-segv", SCENE, root, code=-signal.SIGSEGV)
    reported = crash_note(folder, "Signal: SIGSEGV", output)

    output = smoke.run("start-again", SCENE, root)
    require(f"E2E_RECOVERY_CRASH_NOTICE {reported}" in output.splitlines(), "the start has no notice that names the crash note", output)
    require("E2E_RECOVERY_CRASH_NOTE" not in output, "a start without a copy offered one", output)
    require_reported(folder, reported, output)

    output = smoke.run("start-again", SCENE, root)
    require("E2E_RECOVERY_CRASH_NOTE" not in output and "E2E_RECOVERY_CRASH_NOTICE" not in output,
            "a start after the crash was reported told of it again", output)
    print("PASS a crash leaves one note, and the next start names it in the recovery prompt or a notice, once")


def main() -> None:
    if len(sys.argv) != 3:
        raise SystemExit("usage: recovery_smoke.py /path/to/QEGTRAIN /path/to/scene_tool")
    app = Path(sys.argv[1]).resolve()
    scene_tool = Path(sys.argv[2]).resolve()
    for tool in (app, scene_tool):
        if not tool.exists():
            raise SystemExit(f"not found: {tool}")
    with tempfile.TemporaryDirectory(prefix="qegtrain-recovery-smoke-") as temp:
        smoke = Smoke(app, scene_tool, Path(temp))
        for scenario in (scenario_bundle, scenario_new_case, scenario_original_changes, scenario_discard, scenario_unwritable,
                         scenario_crash_note):
            scenario(smoke)
    print(f"PASS recovery smoke ({smoke.count} runs)")


if __name__ == "__main__":
    main()
