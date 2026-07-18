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
from __future__ import annotations

import json
import math
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

from headless_smoke import case_command

ROOT = Path(__file__).resolve().parents[2]
SCENE_TOOL = ROOT / "build/scene_tool"
APP = ROOT / "build/QEGTRAIN.app/Contents/MacOS/QEGTRAIN"
SCENE_DIR = ROOT / "EGTRAIN/QEGTRAIN/Scenes/Assignment_Gvc_Gdg_Ut"
CASE_ID = 5
STAGED_NAME = "Input_EGTRAIN_Assignment"
TRAJ_REL = "Output/Output_EGTRAIN_Assignment/TrainTrajectories/TrainServicePathDiagram.txt"

# [800, 1400] lies inside the scheduled trip for IC1723-1.
TARGET_SERVICE = "IC1723"
TARGET_TRAIN = "IC1723-1"
WINDOW_START = 800
WINDOW_END = 1400
_TOL = 1.0  # metres; positions within this are treated as unchanged

# signal_failure phase: blocks are 2 km each (TrackLines/B0/BlockCumPari.txt),
# so 12-B0 starts at 24 km. At the window start the train is near 14.5 km,
# and the base run crosses 24 km around t=1160.
FAILED_BLOCK = "12-B0"
BLOCK_ENTRY_M = 24000.0
SF_WINDOW_START = 900
SF_WINDOW_END = 1500


def export_and_run(scene_dir: Path, tmp: Path, tag: str) -> list[float | None]:
    exported = tmp / f"exported_{tag}"
    log = Path(tempfile.gettempdir()) / f"qegtrain-incident-{tag}.log"
    with log.open("wb") as output:
        export = subprocess.run([str(SCENE_TOOL), "export", str(scene_dir), str(exported)],
                                stdout=output, stderr=subprocess.STDOUT)
    if export.returncode != 0:
        sys.exit(f"[{tag}] scene export failed with {export.returncode}; see {log}")
    staging = tmp / f"staging_{tag}"
    (staging / "Output").mkdir(parents=True)
    (staging / "Input").mkdir()
    os.symlink(exported, staging / "Input" / STAGED_NAME, target_is_directory=True)
    with log.open("ab") as output:
        proc = subprocess.run(case_command(CASE_ID), cwd=staging, stdout=output,
                              stderr=subprocess.STDOUT, timeout=300)
    if proc.returncode != 0:
        sys.exit(f"[{tag}] case {CASE_ID} exited with {proc.returncode}; see {log}")
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

        # Control for the signal_failure phase: the base run must cross the
        # failed block entry inside the window.
        base_crossing = [p for p in base_positions[SF_WINDOW_START:SF_WINDOW_END] if p is not None]
        if not base_crossing or max(base_crossing) <= BLOCK_ENTRY_M + _TOL:
            sys.exit(f"control failed: {TARGET_TRAIN} does not pass the {FAILED_BLOCK} entry at "
                     f"{BLOCK_ENTRY_M:.0f} m during [{SF_WINDOW_START},{SF_WINDOW_END}] in the base scene")
        print(f"PASS control: {TARGET_TRAIN} passes {BLOCK_ENTRY_M:.0f} m during the failure window in the base scene")

        # Incident: same scene plus a signal_failure on a block ahead of the train.
        sf_scene = tmp_dir / "scene_signal_failure"
        shutil.copytree(SCENE_DIR, sf_scene)
        sf_incidents = {"incidents": [{
            "id": "smoke_signal_failure",
            "type": "signal_failure",
            "target": FAILED_BLOCK,
            "start_seconds": SF_WINDOW_START,
            "end_seconds": SF_WINDOW_END,
        }]}
        (sf_scene / "incidents.json").write_text(json.dumps(sf_incidents, indent=2), encoding="utf-8")

        sf_positions = export_and_run(sf_scene, tmp_dir, "signal_failure")
        sf_window = [p for p in sf_positions[SF_WINDOW_START:SF_WINDOW_END] if p is not None]
        if not sf_window:
            sys.exit(f"signal_failure failed: {TARGET_TRAIN} has no positions during the failure window")
        held_at = max(sf_window)
        if held_at > BLOCK_ENTRY_M + _TOL:
            sys.exit(f"signal_failure failed: {TARGET_TRAIN} reached {held_at:.0f} m during the window but "
                     f"should brake before the {FAILED_BLOCK} entry at {BLOCK_ENTRY_M:.0f} m")
        if span(sf_positions, 300, SF_WINDOW_START) <= _TOL:
            sys.exit(f"signal_failure failed: {TARGET_TRAIN} did not move before the window")
        sf_tail = [p for p in sf_positions[SF_WINDOW_END:] if p is not None]
        if not sf_tail or max(sf_tail) <= BLOCK_ENTRY_M + _TOL:
            sys.exit(f"signal_failure failed: {TARGET_TRAIN} did not pass the failed block after the window")
        print(f"PASS signal_failure: {TARGET_TRAIN} held at {held_at:.0f} m before the {FAILED_BLOCK} entry "
              f"and passed it after the window")


if __name__ == "__main__":
    main()
