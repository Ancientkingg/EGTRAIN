#!/usr/bin/env python3
import subprocess
import sys
from pathlib import Path


def main() -> None:
    root = Path(__file__).resolve().parents[2]
    scene_tool = Path(sys.argv[1])
    proc = subprocess.run(
        [scene_tool, "validate", root / "EGTRAIN/QEGTRAIN/Scenes/Copenhagen"],
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
    )
    if proc.returncode != 0:
        raise SystemExit(proc.stdout)

    dwell = [line for line in proc.stdout.splitlines() if "[scene.dwell.exceeds_window]" in line]
    if len(dwell) != 1:
        raise SystemExit(f"expected one grouped dwell diagnostic, got {len(dwell)}")
    if not dwell[0].split("x ", 1)[0].isdigit():
        raise SystemExit(f"expected grouped diagnostic count: {dwell[0]}")
    for detail in ("services.json", "Dwell time exceeds departure - arrival window"):
        if detail not in dwell[0]:
            raise SystemExit(f"grouped diagnostic lost {detail}: {dwell[0]}")
    for context in (
        "(service A-Hillerod-Hundige) at services[A-Hillerod-Hundige].stops[1].dwell_seconds",
        "(service A-Hillerod-Hundige-2) at services[A-Hillerod-Hundige-2].stops[1].dwell_seconds",
    ):
        if context not in proc.stdout:
            raise SystemExit(f"grouped diagnostic lost context: {context}")


if __name__ == "__main__":
    main()
