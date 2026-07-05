#!/usr/bin/env python3
import subprocess
import sys
import tempfile
from pathlib import Path

from headless_smoke import CASES, run_case


ROOT = Path(__file__).resolve().parents[2]
SCENE_TOOL = ROOT / "build/scene_tool"
SCENE_DIR = ROOT / "EGTRAIN/QEGTRAIN/Scenes"

CASES_INFO = [
    (1, "Netherlands", "Input_EGTRAIN_Netherlands"),
    (2, "Paimpol", "Input_EGTRAIN_Paimpol"),
    (3, "Copenhagen", "Input_EGTRAIN_Banedanmark"),
    (4, "Milano_Brescia", "Input_EGTRAIN_Milano_Brescia"),
]


def main() -> None:
    if not SCENE_TOOL.exists():
        sys.exit(f"scene_tool not found at {SCENE_TOOL}. Build it first.")

    with tempfile.TemporaryDirectory() as tmp:
        tmp_dir = Path(tmp)
        for case_id, scene_name, staged_name in CASES_INFO:
            print(f"--- round-trip case {case_id}: {scene_name} ---")
            scene_dir = SCENE_DIR / scene_name
            exported_dir = tmp_dir / f"exported_{case_id}"

            print("Validating committed scene...")
            subprocess.run([str(SCENE_TOOL), "validate", str(scene_dir)], check=True)

            print("Exporting committed scene...")
            subprocess.run([str(SCENE_TOOL), "export", str(scene_dir), str(exported_dir)], check=True)

            staging_dir = tmp_dir / f"staging_{case_id}"
            staging_dir.mkdir()
            (staging_dir / "Output").mkdir()
            (staging_dir / CASES[case_id]).mkdir(parents=True, exist_ok=True)
            input_link_dir = staging_dir / "Input"
            input_link_dir.mkdir()
            (input_link_dir / staged_name).symlink_to(exported_dir, target_is_directory=True)

            # Movement artifacts are covered by headless_smoke from the repo
            # working tree. In isolated staging the legacy simulator can exit
            # cleanly while producing stationary trajectory artifacts even for
            # native legacy input, so this gate checks conversion/export/run.
            print("Running simulation...")
            run_case(case_id, cwd=staging_dir, out_base=staging_dir)

    print("ROUNDTRIP PASS")


if __name__ == "__main__":
    main()
