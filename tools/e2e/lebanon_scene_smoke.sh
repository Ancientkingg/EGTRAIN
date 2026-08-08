#!/usr/bin/env bash
# Check the committed canonical Lebanon scene: it validates and exports without
# legacy passthrough files, then runs directly from the scene directory.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
TOOL="$ROOT/build/scene_tool"
SCENE="$ROOT/EGTRAIN/QEGTRAIN/Scenes/Lebanon"

if [[ ! -x "$TOOL" ]]; then
	echo "scene_tool not found or not executable: $TOOL" >&2
	exit 1
fi

WORK="$(mktemp -d "${TMPDIR:-/tmp}/qegtrain-lebanon.XXXXXX")"
trap 'rm -rf "$WORK"' EXIT
EXPORT_DIR="$WORK/export"
SCENE_COPY="$WORK/scene"

# The shell keeps all 34 supplied stations.
stations=$(python3 -c "import json,sys;print(len(json.load(open(sys.argv[1]))['stations']))" "$SCENE/stations.json")
if [[ "$stations" != "34" ]]; then
	echo "expected 34 stations, found $stations" >&2
	exit 1
fi

# The canonical scene validates without legacy compatibility errors.
"$TOOL" validate "$SCENE"

# Export remains an explicit legacy compatibility check in a clean directory.
"$TOOL" export "$SCENE" "$EXPORT_DIR"
test -f "$EXPORT_DIR/TrackLines/Stations.txt"
test -f "$EXPORT_DIR/Trains/LB-1.txt"

# Run a copied canonical scene with isolated output.
cp -R "$SCENE" "$SCENE_COPY"
APP="$ROOT/build/QEGTRAIN.app/Contents/MacOS/QEGTRAIN"
LOG="$WORK/lebanon.log"
QEGTRAIN_OUTPUT_DIR="$WORK" \
"$APP" --scene "$SCENE_COPY" -h 100 -g 0 -TSM 0 -RC 0 >"$LOG" 2>&1
grep -q "End of Simulation" "$LOG"

echo "lebanon scene smoke passed: canonical scene validated, exported, and run"
