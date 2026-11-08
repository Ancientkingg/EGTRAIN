#!/usr/bin/env python3
"""A window on a screen with a settings folder of its own keeps its crash notes there and leaves those of the user alone.

usage: run_data_folder_smoke.py /path/to/QEGTRAIN

The window starts on the vnc platform, which needs no display, so the run has a screen as far as the application can
tell. XDG_DATA_HOME and XDG_CONFIG_HOME stand in for the folders of the user, and each run finds a crash note in the
data folder of the user and another in its settings folder. At the start the window marks the note of its own folder
reported, renaming crash-*.txt to reported-crash-*.txt, and then waits in the crash notice, which nobody answers. The
script ends the window once a note is marked, or after a timeout.

  user run      Without QEGTRAIN_E2E_SETTINGS_DIR the window reports the note of the user, so the stand-in folder is the
                one the window reads.
  settings run  With QEGTRAIN_E2E_SETTINGS_DIR the window reports the note in the Crashes folder of its settings folder
                and leaves the note of the user unreported. Recovery copies use the same folder.

Linux only: CTest registers it where Qt has the vnc platform.
"""
from __future__ import annotations

import os
import subprocess
import sys
import tempfile
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
RUN_DIR = ROOT / "EGTRAIN" / "QEGTRAIN"
SCENE = RUN_DIR / "Scenes" / "Paimpol"
APP_ARGUMENTS = ["--scene", str(SCENE), "-g", "1", "-pax", "0"]
TIMEOUT_SCALE = max(1, int(os.environ.get("EGTRAIN_TEST_TIMEOUT_SCALE", "1") or "1"))
START_SECONDS = 60 * TIMEOUT_SCALE
USER_NOTE = "crash-user.txt"
OWN_NOTE = "crash-own.txt"


def fail(message: str, output: str = "") -> None:
    tail = output[-3000:]
    raise SystemExit(f"FAIL {message}" + (f"\n{tail}" if tail else ""))


def notes(folder: Path) -> list[str]:
    return sorted(path.name for path in folder.iterdir()) if folder.is_dir() else []


def write_note(folder: Path, name: str) -> None:
    folder.mkdir(parents=True, exist_ok=True)
    for old in folder.iterdir():
        old.unlink()
    (folder / name).write_text("A crash note left by an earlier run.\n", encoding="utf-8")


def run_window(app: Path, temp: Path, label: str, settings: Path | None, watched: list[Path]) -> str:
    """Starts the window and ends it once one of the watched folders holds a reported note. Returns its output."""
    env = {key: value for key, value in os.environ.items() if not key.startswith("QEGTRAIN_")}
    env["QT_QPA_PLATFORM"] = "vnc:port=0"
    env["XDG_DATA_HOME"] = str(temp / "user-data")
    env["XDG_CONFIG_HOME"] = str(temp / "user-config")
    env["QEGTRAIN_OUTPUT_DIR"] = str(temp / "output")
    if settings is not None:
        env["QEGTRAIN_E2E_SETTINGS_DIR"] = str(settings)
    log = temp / f"{label}.log"
    with log.open("wb") as out:
        window = subprocess.Popen([str(app), *APP_ARGUMENTS], cwd=RUN_DIR, env=env, stdout=out, stderr=subprocess.STDOUT)
        try:
            deadline = time.monotonic() + START_SECONDS
            while time.monotonic() < deadline and window.poll() is None:
                if any(name.startswith("reported-") for folder in watched for name in notes(folder)):
                    break
                time.sleep(0.2)
            # The rename of a second note would come right after the first.
            time.sleep(0.5)
        finally:
            if window.poll() is None:
                window.kill()
            window.wait()
    return log.read_text(encoding="utf-8", errors="replace")


def main() -> int:
    if len(sys.argv) != 2:
        print(__doc__)
        return 2
    app = Path(sys.argv[1]).resolve()
    with tempfile.TemporaryDirectory(prefix="qegtrain-run-data-folder-") as name:
        temp = Path(name)
        user = temp / "user-data" / "EGTRAIN" / "EGTRAIN" / "Crashes"
        settings = temp / "settings"
        own = settings / "Crashes"

        write_note(user, USER_NOTE)
        output = run_window(app, temp, "user-run", None, [user])
        if notes(user) != ["reported-" + USER_NOTE]:
            fail(f"user run: the crash notes of the user are {notes(user)}, not the reported note", output)
        print("PASS user run: the window reports the crash note of the user")

        write_note(user, USER_NOTE)
        write_note(own, OWN_NOTE)
        output = run_window(app, temp, "settings-run", settings, [user, own])
        if notes(own) != ["reported-" + OWN_NOTE]:
            fail(f"settings run: the crash notes of the settings folder are {notes(own)}, not the reported note", output)
        if notes(user) != [USER_NOTE]:
            fail(f"settings run: the crash notes of the user are {notes(user)}; the run took a note of the user", output)
        print("PASS settings run: the window reports the note of its settings folder and leaves that of the user")
    return 0


if __name__ == "__main__":
    sys.exit(main())
