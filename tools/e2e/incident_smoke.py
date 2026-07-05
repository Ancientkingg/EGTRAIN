#!/usr/bin/env python3
"""End-to-end check that a train_breakdown incident reaches the simulation.

Exports the assignment scene twice: once unchanged and once with a
train_breakdown incident on a single service for a window that falls inside
the service's normal travel. Then it runs both and asserts that

  * the unchanged run has the target train moving during the window (control),
  * the incident run has the target train's position frozen during the window,
  * the incident run has the target train moving before and after the window.

This exercises the whole path: scene incidents.json -> SceneExporter Incidents.txt
-> Load_Incidents -> Incident_Holds_Train.
"""
import json
import math
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SCENE_TOOL = ROOT / "build/scene_tool"
APP = ROOT / "build/QEGTRAIN.app/Contents/MacOS/QEGTRAIN"
SCENE_DIR = ROOT / "EGTRAIN/QEGTRAIN/Scenes/Assignment_Gvc_Gdg_Ut"
CASE_ID = 5
STAGED_NAME = "Input_EGTRAIN_Assignment"
TRAJ_REL = "Output/Output_EGTRAIN_Assignment/TrainTrajectories/TrainServicePathDiagram.txt"
PROMPTS = "n\n0\n0\n0\n0\n"

# svc_e1-1 travels during timesteps [239, 2091] in the base scene, so this
# window is comfortably inside its normal run.
TARGET_SERVICE = "svc_e1"
TARGET_TRAIN = "svc_e1-1"
WINDOW_START = 800
WINDOW_END = 1400
_TOL = 1.0  # metres; positions within this are treated as unchanged


def export_and_run(scene_dir: Path, tmp: Path, tag: str) -> list[float | None]:
    exported = tmp / f"exported_{tag}"
    subprocess.run([str(SCENE_TOOL), "export", str(scene_dir), str(exported)], check=True,
                   stdout=subprocess.DEVNULL)
    staging = tmp / f"staging_{tag}"
    (staging / "Output").mkdir(parents=True)
    (staging / "Input").mkdir()
    os.symlink(exported, staging / "Input" / STAGED_NAME, target_is_directory=True)
    proc = subprocess.run([str(APP), "-n", str(CASE_ID)], cwd=staging, input=PROMPTS, text=True,
                          stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=300)
    if proc.returncode != 0:
        sys.exit(f"[{tag}] case {CASE_ID} exited with {proc.returncode}")
    traj = staging / TRAJ_REL
    if not traj.exists():
        sys.exit(f"[{tag}] no trajectory file written")
    return read_positions(traj, TARGET_TRAIN)


def read_positions(traj: Path, train: str) -> list[float | None]:
    """Return the per-timestep position list for one train, None where blank."""
    for line in traj.read_text(encoding="utf-8", errors="replace").splitlines():
        if "\t" not in line:
            continue
        cells = line.split("\t")
        if cells[0] != train:
            continue
        positions: list[float | None] = []
        for cell in cells[4:]:  # skip trainDescription, dispLineID, reversed, corridor
            if cell == "":
                positions.append(None)
                continue
            try:
                value = float(cell)
            except ValueError:
                positions.append(None)
                continue
            positions.append(value if math.isfinite(value) and value > -9990 and value != -1.0 else None)
        return positions
    sys.exit(f"train {train} not found in {traj}")


def span(positions: list[float | None], lo: int, hi: int) -> float:
    window = [p for p in positions[lo:hi] if p is not None]
    if len(window) < 2:
        return 0.0
    return max(window) - min(window)


def main() -> None:
    if not SCENE_TOOL.exists() or not APP.exists():
        sys.exit("scene_tool or QEGTRAIN app not built")

    with tempfile.TemporaryDirectory() as tmp:
        tmp_dir = Path(tmp)

        # Control: unchanged scene.
        base_positions = export_and_run(SCENE_DIR, tmp_dir, "base")
        base_window = span(base_positions, WINDOW_START, WINDOW_END)
        if base_window <= _TOL:
            sys.exit(f"control failed: {TARGET_TRAIN} does not move during [{WINDOW_START},{WINDOW_END}] "
                     f"in the unmodified scene (span {base_window:.2f} m)")
        print(f"PASS control: {TARGET_TRAIN} moves {base_window:.0f} m during the window in the base scene")

        # Incident: same scene plus a train_breakdown on the target service.
        incident_scene = tmp_dir / "scene_incident"
        shutil.copytree(SCENE_DIR, incident_scene)
        incidents = {"incidents": [{
            "id": "smoke_breakdown",
            "type": "train_breakdown",
            "target": TARGET_SERVICE,
            "start_seconds": WINDOW_START,
            "end_seconds": WINDOW_END,
        }]}
        (incident_scene / "incidents.json").write_text(json.dumps(incidents, indent=2), encoding="utf-8")

        inc_positions = export_and_run(incident_scene, tmp_dir, "incident")
        held = span(inc_positions, WINDOW_START, WINDOW_END)
        if held > _TOL:
            sys.exit(f"incident failed: {TARGET_TRAIN} moved {held:.2f} m during the breakdown window "
                     f"[{WINDOW_START},{WINDOW_END}] but should be held")
        print(f"PASS incident: {TARGET_TRAIN} held (span {held:.2f} m) during the breakdown window")

        before = span(inc_positions, 300, WINDOW_START)
        after = span(inc_positions, WINDOW_END, 2200)
        if before <= _TOL:
            sys.exit(f"incident failed: {TARGET_TRAIN} did not move before the window (span {before:.2f} m)")
        if after <= _TOL:
            sys.exit(f"incident failed: {TARGET_TRAIN} did not resume after the window (span {after:.2f} m)")
        print(f"PASS incident: {TARGET_TRAIN} moves {before:.0f} m before and {after:.0f} m after the window")


if __name__ == "__main__":
    main()
