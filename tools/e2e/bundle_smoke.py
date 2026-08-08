#!/usr/bin/env python3
import os
import subprocess
import sys
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
SCENES = ROOT / "EGTRAIN/QEGTRAIN/Scenes"


def run(command: list[str], **kwargs) -> None:
    result = subprocess.run(command, capture_output=True, text=True, timeout=300, **kwargs)
    if result.returncode != 0:
        sys.stderr.write(result.stdout)
        sys.stderr.write(result.stderr)
        result.check_returncode()


def main() -> None:
    if len(sys.argv) != 3:
        raise SystemExit("usage: bundle_smoke.py <scene_tool> <QEGTRAIN>")

    scene_tool = Path(sys.argv[1]).resolve()
    app = Path(sys.argv[2]).resolve()
    with tempfile.TemporaryDirectory() as tmp:
        tmp_dir = Path(tmp)
        bundles: dict[str, Path] = {}
        for scene_dir in sorted(path for path in SCENES.iterdir() if path.is_dir()):
            bundle = tmp_dir / f"{scene_dir.name}.egscene"
            run([str(scene_tool), "pack", str(scene_dir), str(bundle)])
            run([str(scene_tool), "validate", str(bundle)])
            bundles[scene_dir.name] = bundle
            print(f"PASS packed and validated {bundle.name}")

        env = os.environ.copy()
        env["QEGTRAIN_OUTPUT_DIR"] = str(tmp_dir / "run")
        run(
            [
                str(app),
                "--scene",
                str(bundles["Assignment_Gvc_Gdg_Ut"]),
                "-h",
                "600",
                "-g",
                "0",
                "-pax",
                "0",
                "-TSM",
                "0",
                "-RC",
                "0",
            ],
            cwd=tmp_dir,
            env=env,
        )
        print("PASS ran Assignment_Gvc_Gdg_Ut.egscene headlessly")


if __name__ == "__main__":
    main()
