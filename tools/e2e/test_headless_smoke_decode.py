#!/usr/bin/env python3
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from headless_smoke import run_command


def main() -> None:
    proc = run_command(
        [
            sys.executable,
            "-c",
            "import sys; sys.stdout.buffer.write(bytes([0xfc]))",
        ]
    )
    if proc.returncode != 0:
        raise SystemExit(f"expected clean subprocess exit, got {proc.returncode}")
    if "\ufffd" not in proc.stdout:
        raise SystemExit("expected replacement character for undecodable stdout")


if __name__ == "__main__":
    main()
