#!/usr/bin/env python3
import os
import select
import signal
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
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

    assert_launch_reaches_defaults(app, [], "no-argument launch")
    assert_launch_reaches_defaults(app, ["-n", "1", "-g", "1"], "partial-argument launch")


if __name__ == "__main__":
    main()
