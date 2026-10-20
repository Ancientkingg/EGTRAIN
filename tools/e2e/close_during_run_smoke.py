#!/usr/bin/env python3
"""Close the window while a simulation runs.

usage: close_during_run_smoke.py /path/to/QEGTRAIN

Starts QEGTRAIN offscreen on the Assignment scene with an autostarted run. At
the first displayed step the application asks its own window to close. The
request has to return with the run still stopping and the window still open,
which the application reports as "E2E_CLOSE_DURING_RUN deferred=1". The
application then has to exit by itself with code 0 once the run has stopped.
"""
import os
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
RUN_DIR = ROOT / "EGTRAIN" / "QEGTRAIN"
SCENE = RUN_DIR / "Scenes" / "Assignment_Gvc_Gdg_Ut"
EXIT_SECONDS = 180
MARKER = "E2E_CLOSE_DURING_RUN deferred=1"
BAD_PATTERNS = ("AddressSanitizer", "UndefinedBehaviorSanitizer", "ThreadSanitizer", "runtime error")


def main() -> None:
    if len(sys.argv) != 2:
        raise SystemExit("usage: close_during_run_smoke.py /path/to/QEGTRAIN")
    app = Path(sys.argv[1]).resolve()
    if not app.exists():
        raise SystemExit(f"QEGTRAIN app not found: {app}")

    env = os.environ.copy()
    env["QEGTRAIN_AUTOSTART"] = "1"
    env["QEGTRAIN_E2E_CLOSE_DURING_RUN"] = "1"
    env.setdefault("QT_QPA_PLATFORM", "offscreen")
    with tempfile.TemporaryDirectory() as temp_dir:
        env["QEGTRAIN_OUTPUT_DIR"] = str(Path(temp_dir) / "output")
        command = [str(app), "--scene", str(SCENE), "-g", "1", "-pax", "0", "-TSM", "0", "-RC", "0"]
        try:
            proc = subprocess.run(command, cwd=RUN_DIR, env=env, stdout=subprocess.PIPE,
                                  stderr=subprocess.STDOUT, timeout=EXIT_SECONDS)
        except subprocess.TimeoutExpired as exc:
            tail = (exc.stdout or b"").decode("utf-8", errors="replace")[-4000:]
            raise SystemExit(f"QEGTRAIN did not exit within {EXIT_SECONDS}s of the close request\n{tail}") from exc

    text = proc.stdout.decode("utf-8", errors="replace")
    tail = text[-4000:]
    for pattern in BAD_PATTERNS:
        if pattern in text:
            raise SystemExit(f"the log contains '{pattern}'\n{tail}")
    if MARKER not in text:
        raise SystemExit(f"the close request did not wait for the run to stop: no '{MARKER}'\n{tail}")
    if proc.returncode != 0:
        raise SystemExit(f"QEGTRAIN exited with {proc.returncode} after the deferred close\n{tail}")
    print("PASS the window closed after the running simulation had stopped")


if __name__ == "__main__":
    main()
