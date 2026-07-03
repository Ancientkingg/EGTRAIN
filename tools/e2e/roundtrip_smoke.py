#!/usr/bin/env python3
import subprocess
import tempfile
import sys
from pathlib import Path
from headless_smoke import run_case, check_movement, check_station_arrivals

ROOT = Path(__file__).resolve().parents[2]
SCENE_TOOL = ROOT / "build/scene_tool"
INPUT_DIR = ROOT / "EGTRAIN/QEGTRAIN/Input"

CASES_INFO = [
    (3, "Input_EGTRAIN_Copenhagen", "Input_EGTRAIN_Banedanmark"),
    (4, "Input_EGTRAIN_Milano_Brescia", "Input_EGTRAIN_Milano_Brescia"),
]

def main() -> None:
    if not SCENE_TOOL.exists():
        sys.exit(f"scene_tool not found at {SCENE_TOOL}. Build first.")

    with tempfile.TemporaryDirectory() as tmp:
        tmp_dir = Path(tmp)
        for case_id, src_name, staged_name in CASES_INFO:
            print(f"--- Running round-trip case {case_id} ---")
            legacy_dir = INPUT_DIR / src_name
            scene_dir = tmp_dir / f"scene_{case_id}"
            exported_dir = tmp_dir / f"exported_{case_id}"
            
            print(f"Importing {legacy_dir.name}...")
            subprocess.run([str(SCENE_TOOL), "import", str(legacy_dir), str(scene_dir)], check=True)
            
            print(f"Exporting...")
            subprocess.run([str(SCENE_TOOL), "export", str(scene_dir), str(exported_dir)], check=True)
            
            staging_dir = tmp_dir / f"staging_{case_id}"
            staging_dir.mkdir()
            (staging_dir / "Output").mkdir()
            input_link_dir = staging_dir / "Input"
            input_link_dir.mkdir()
            (input_link_dir / staged_name).symlink_to(exported_dir, target_is_directory=True)
            
            print(f"Running simulation...")
            run_case(case_id, cwd=staging_dir, out_base=staging_dir)
            check_movement(case_id, out_base=staging_dir)
            check_station_arrivals(case_id, out_base=staging_dir)

if __name__ == "__main__":
    main()
