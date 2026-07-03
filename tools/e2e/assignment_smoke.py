#!/usr/bin/env python3
import subprocess
import tempfile
import sys
from pathlib import Path
from headless_smoke import run_case, check_movement, check_station_arrivals

ROOT = Path(__file__).resolve().parents[2]
SCENE_TOOL = ROOT / "build/scene_tool"
SCENE_DIR = ROOT / "EGTRAIN/QEGTRAIN/Scenes/Assignment_Gvc_Gdg_Ut"
CASE_ID = 5
STAGED_NAME = "Input_EGTRAIN_Assignment"


def main() -> None:
    if not SCENE_TOOL.exists():
        sys.exit(f"scene_tool not found at {SCENE_TOOL}. Build first.")
    if not SCENE_DIR.exists():
        sys.exit(f"assignment scene not found at {SCENE_DIR}")

    with tempfile.TemporaryDirectory() as tmp:
        tmp_dir = Path(tmp)
        exported_dir = tmp_dir / "exported"

        print("Validating assignment scene...")
        subprocess.run([str(SCENE_TOOL), "validate", str(SCENE_DIR)], check=True)

        print("Exporting assignment scene...")
        subprocess.run([str(SCENE_TOOL), "export", str(SCENE_DIR), str(exported_dir)], check=True)

        staging_dir = tmp_dir / "staging"
        staging_dir.mkdir()
        (staging_dir / "Output").mkdir()
        input_dir = staging_dir / "Input"
        input_dir.mkdir()
        (input_dir / STAGED_NAME).symlink_to(exported_dir, target_is_directory=True)

        print("Running assignment case...")
        run_case(CASE_ID, cwd=staging_dir, out_base=staging_dir)
        check_movement(CASE_ID, out_base=staging_dir)
        check_station_arrivals(CASE_ID, out_base=staging_dir)


if __name__ == "__main__":
    main()
