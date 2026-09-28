#!/usr/bin/env python3
"""Offscreen native-builder and route-chart regression for route diagrams."""
from pathlib import Path
import csv
import os
import subprocess
import sys
import tempfile

root = Path(__file__).resolve().parents[2]
build = root / "build"
env = dict(os.environ, QT_QPA_PLATFORM="offscreen")
for executable, evidence in (
    ("test_operationsbuilder", "native forward/reverse route diagram coordinates passed"),
    ("test_routediagramcoordinates", "route coordinate projection passed"),
    ("test_diagramwindow", "all DiagramWindow tests passed"),
):
    program = build / executable
    if not program.is_file():
        sys.exit(f"missing {program}; build the focused targets first")
    result = subprocess.run([str(program)], cwd=root, env=env, text=True,
                            stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=90)
    output = result.stdout + result.stderr
    if result.returncode or evidence not in output:
        sys.exit(f"{executable} failed or omitted {evidence!r}:\n{output[-4000:]}")
    print(f"PASS {executable}: {evidence}")

app = build / "QEGTRAIN.app/Contents/MacOS/QEGTRAIN"
scene = root / "EGTRAIN/QEGTRAIN/Scenes/Paimpol"
if not app.is_file():
    sys.exit(f"missing {app}; build QEGTRAIN first")
with tempfile.TemporaryDirectory(prefix="route-diagram-") as directory:
    env.update(QEGTRAIN_AUTOSTART="1", QEGTRAIN_E2E_ROUTE_DIAGRAM=directory)
    result = subprocess.run([str(app), "--scene", str(scene), "-h", "8000", "-g", "1",
                             "-pax", "0", "-TSM", "0", "-RC", "0"],
                            cwd=root / "EGTRAIN/QEGTRAIN", env=env, text=True,
                            stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=180)
    if result.returncode or "E2E_ROUTE_DIAGRAM_OK" not in result.stdout:
        sys.exit(f"route diagram run failed:\n{(result.stdout + result.stderr)[-5000:]}")
    expected = ["Train", "Reference route", "Source route", "Event", "Station",
                "Journey order", "Call", "Elapsed time[s]", "Reference route X[km]"]
    plotted = {}
    for name in ("train_path_graph", "timetable_graph"):
        png = Path(directory, name + ".png")
        if not png.is_file() or png.stat().st_size == 0:
            sys.exit(f"missing displayed chart PNG: {png}")
        with Path(directory, name + "_all.csv").open(newline="", encoding="utf-8") as stream:
            all_rows = list(csv.DictReader(stream))
        with Path(directory, name + "_first.csv").open(newline="", encoding="utf-8") as stream:
            reader = csv.DictReader(stream)
            if reader.fieldnames != expected:
                sys.exit(f"{name} graph CSV schema mismatch: {reader.fieldnames}")
            first_rows = list(reader)
        if not all_rows or not first_rows or len(first_rows) >= len(all_rows):
            sys.exit(f"{name} CSV filtering did not narrow visible trains")
        first_train = first_rows[0]["Train"]
        if any(row["Train"] != first_train for row in first_rows):
            sys.exit(f"{name} CSV contains hidden trains")
        if not any(row["Reference route X[km]"] and row["Elapsed time[s]"] for row in first_rows):
            sys.exit(f"{name} CSV lacks mapped timed samples")
        if name == "timetable_graph" and not any(not row["Elapsed time[s]"] for row in first_rows):
            sys.exit("timetable graph did not retain unavailable event times as blank")
        plotted[name] = first_rows
        print(f"PASS {name}: {len(first_rows)} filtered rows; PNG and graph schema")
    path_positions = [float(row["Reference route X[km]"]) for row in plotted["train_path_graph"]
                      if row["Reference route X[km]"]]
    station_positions = [float(row["Reference route X[km]"]) for row in plotted["timetable_graph"]
                         if row["Reference route X[km]"] and row["Elapsed time[s]"]]
    if not station_positions or not any(
        min(abs(path - station) for path in path_positions) < 0.01
        for station in station_positions
    ):
        sys.exit("native trajectory and plotted station coordinates do not align")
    print("PASS native trajectory/station reference-coordinate alignment")
