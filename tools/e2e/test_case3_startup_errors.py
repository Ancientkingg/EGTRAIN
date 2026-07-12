#!/usr/bin/env python3
import subprocess
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
ERRORS = (
    "ERROR:Cannot Read file with characteristics of Network Areas",
    "Error: Impossible to open file",
    "ERROR:Cannot Read file with Passenger Daily Activity Schedule",
    "ERROR:Cannot Read file with Updated Passenger Route Choices",
)


def main() -> None:
    app = Path(sys.argv[1]).resolve()
    proc = subprocess.run(
        [str(app), "-n", "3", "-h", "1", "-g", "0", "-TSM", "0", "-RC", "0"],
        cwd=ROOT / "EGTRAIN/QEGTRAIN",
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        timeout=60,
    )
    if proc.returncode != 0:
        raise SystemExit(f"case 3 startup exited with {proc.returncode}")
    found = [message for message in ERRORS if message in proc.stdout]
    if found:
        raise SystemExit("case 3 startup reported avoidable errors: " + ", ".join(found))


if __name__ == "__main__":
    main()
