#!/usr/bin/env python3
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from headless_smoke import check_scene_structure, case_command, route_errors, run_command, scene_output_dir


def main() -> None:
    for case_id in range(1, 5):
        check_scene_structure(case_id)

    command = case_command(3)
    expected = ["--scene", str(Path(__file__).resolve().parents[2] / "EGTRAIN/QEGTRAIN/Scenes/Copenhagen"),
                "-g", "0", "-TSM", "0", "-RC", "0"]
    if command[1:] != expected:
        raise SystemExit(f"headless case command does not disable the GUI and integrations: {command}")

    assignment_output = scene_output_dir(5, Path("/tmp/qegtrain-smoke"))
    if assignment_output.name != "Assignment Gvc-Gdg-Ut":
        raise SystemExit(f"headless output path ignores the canonical scene name: {assignment_output}")

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
