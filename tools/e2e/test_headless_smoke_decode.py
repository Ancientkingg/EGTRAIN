#!/usr/bin/env python3
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from headless_smoke import case_command, route_errors, run_command


def main() -> None:
    command = case_command(3)
    expected = ["-n", "3", "-g", "0", "-TSM", "0", "-RC", "0"]
    if command[1:] != expected:
        raise SystemExit(f"headless case command does not disable the GUI and integrations: {command}")

    errors = route_errors("ok\nERROR4 in Route r1\nERROR5 in Route r1\n")
    if errors != ["ERROR4 in Route r1", "ERROR5 in Route r1"]:
        raise SystemExit(f"route error detection failed: {errors}")

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
