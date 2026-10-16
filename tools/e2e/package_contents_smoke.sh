#!/usr/bin/env bash
# Assemble the macOS app as the release holds it from the build outputs and
# rehearse the Lebanon path: the app holds scene_tool, the complete canonical
# Lebanon scene, and the guide; scene_tool validates and exports the packaged
# scene; and the app opens and renders it under a clean HOME with exports
# written outside the bundle.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
APP_BUNDLE="$ROOT/build/QEGTRAIN.app"
SCENE_TOOL="$ROOT/build/scene_tool"
GUIDE_SRC="$ROOT/docs/guides/lebanon-case-study.md"

if [[ ! -d "$APP_BUNDLE" ]]; then
	echo "app bundle not found: $APP_BUNDLE (build QEGTRAIN first)" >&2
	exit 1
fi
if [[ ! -x "$SCENE_TOOL" ]]; then
	echo "scene_tool not found: $SCENE_TOOL" >&2
	exit 1
fi

WORK="$(mktemp -d "${TMPDIR:-/tmp}/qegtrain-package.XXXXXX")"
trap 'rm -rf "$WORK"' EXIT
APP="$WORK/QEGTRAIN.app"
HOME_DIR="$WORK/home"
OUT_DIR="$WORK/out"
mkdir -p "$HOME_DIR" "$OUT_DIR"

# Assemble the packaged app.
cp -R "$APP_BUNDLE" "$APP"
cp "$SCENE_TOOL" "$APP/Contents/MacOS/scene_tool"
cp "$GUIDE_SRC" "$APP/Contents/Resources/lebanon-case-study.md"
SCENE="$APP/Contents/Resources/Scenes/Lebanon"

# Content check: every required item must be present.
require() {
	if [[ ! -e "$1" ]]; then
		echo "package is missing: $1" >&2
		exit 1
	fi
}
require "$APP/Contents/MacOS/QEGTRAIN"
require "$APP/Contents/Resources/egtrain.icns"
test "$(/usr/libexec/PlistBuddy -c 'Print :CFBundleIconFile' "$APP/Contents/Info.plist")" = "egtrain.icns"
require "$APP/Contents/MacOS/scene_tool"
require "$SCENE/stations.json"
require "$SCENE/scene.json"
require "$SCENE/scenarios.json"
require "$APP/Contents/Resources/lebanon-case-study.md"
# Note: the release workflow deploys and verifies the bundled Qt runtime. A local
# build links the system Qt, so the runtime check runs in CI, not here.

# The packaged scene_tool validates the complete canonical scene.
"$APP/Contents/MacOS/scene_tool" validate "$SCENE"

# The packaged scene_tool exports the scene to a directory outside the package.
"$APP/Contents/MacOS/scene_tool" export "$SCENE" "$OUT_DIR/export"
require "$OUT_DIR/export/TrackLines/Stations.txt"

# The packaged app opens and renders the packaged scene under a clean HOME, with
# its save and reload check writing to a copy outside the bundle.
SCENE_COPY="$OUT_DIR/scene"
cp -R "$SCENE" "$SCENE_COPY"
python3 -c 'from pathlib import Path; import json, sys; Path(sys.argv[1]).write_text(json.dumps({"services": []}))' "$SCENE_COPY/services.json"
python3 -c 'from pathlib import Path; import json, sys; Path(sys.argv[1]).write_text(json.dumps({"train_units": [], "compositions": []}))' "$SCENE_COPY/rolling_stock.json"
LOG="$WORK/track_preview.log"
# Marker to detect writes into the bundle during the run (copies above reset
# file times, so compare against a fresh marker rather than a source file).
MARKER="$WORK/before_run"
touch "$MARKER"
set +e
cd "$ROOT/EGTRAIN/QEGTRAIN"
env -i HOME="$HOME_DIR" PATH="/usr/bin:/bin:/usr/sbin:/sbin" \
	QT_QPA_PLATFORM=offscreen \
	QEGTRAIN_E2E_SETTINGS_DIR="$HOME_DIR/settings" \
	QEGTRAIN_E2E_TRACK_PREVIEW=1 \
	QEGTRAIN_E2E_SCENE="$SCENE_COPY" \
	"$APP/Contents/MacOS/QEGTRAIN" --scene "$SCENE" -h 100 -g 1 -pax 0 -TSM 0 -RC 0 >"$LOG" 2>&1
set -e
for marker in E2E_TRACK_PREVIEW_OPEN_OK E2E_TRACK_PREVIEW_RUN_GATED_OK E2E_TRACK_PREVIEW_SAVE_RELOAD_OK; do
	grep -q "$marker" "$LOG" || { echo "packaged app missing marker $marker; see $LOG" >&2; exit 1; }
done

# The supplied Lebanon scene in the app must stay unchanged; the user edits the
# copy that lives outside the app.
changed="$(find "$SCENE" -newer "$MARKER" -type f 2>/dev/null)"
if [[ -n "$changed" ]]; then
	echo "the packaged Lebanon scene was modified during the run:" >&2
	echo "$changed" >&2
	exit 1
fi

echo "package contents smoke passed: app, scene_tool, Lebanon scene, and guide present; scene validated, exported, opened, and reloaded from the app"
