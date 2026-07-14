#!/usr/bin/env bash
# Check the committed Lebanon infrastructure shell: 34 stations, only the
# expected missing train and service data, a clean export with the B0-B7 track
# passthrough, and a GUI load that renders the network and gates the run until
# train data is added.
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

# Validation reports only the expected missing train and service data.
val="$("$TOOL" validate "$SCENE" 2>&1 || true)"
echo "$val" | grep -q "scene.services.none" || { echo "missing expected services error" >&2; exit 1; }
echo "$val" | grep -q "scene.trains.none" || { echo "missing expected trains error" >&2; exit 1; }
unexpected="$(echo "$val" | grep -E '^error' | grep -vE 'scene.services.none|scene.trains.none' || true)"
if [[ -n "$unexpected" ]]; then
	echo "unexpected validation errors:" >&2
	echo "$unexpected" >&2
	exit 1
fi

# Export to a clean directory keeps the B0-B7 legacy track passthrough.
"$TOOL" export "$SCENE" "$EXPORT_DIR"
for b in 0 1 2 3 4 5 6 7; do
	if [[ ! -f "$EXPORT_DIR/TrackLines/B$b/BlockCumPari.txt" ]]; then
		echo "export is missing the B$b track passthrough" >&2
		exit 1
	fi
done

# The scene loads in the GUI, renders the network, gates the run on the missing
# train data, and survives save and reload. Work on a copy so the committed
# scene is never modified.
cp -R "$SCENE" "$SCENE_COPY"
"$ROOT/tools/e2e/track_preview_smoke.sh" "$SCENE_COPY"

echo "lebanon scene smoke passed: 34 stations, B0-B7 passthrough, run gated on missing train data"
