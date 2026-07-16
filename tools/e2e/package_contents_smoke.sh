#!/usr/bin/env bash
# Assemble the presentation package from the build outputs and rehearse the
# Lebanon path that does not need authored train data: the package holds the
# application, scene_tool, the Lebanon scene, and the guide; scene_tool validates
# and exports the packaged scene; and the app opens and renders it under a clean
# HOME with exports written outside the bundle.
#
# The full run and the diagram exports need authored Lebanon rolling stock and a
# timetable, which are not supplied, so they are out of scope here.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
APP_BUNDLE="$ROOT/build/QEGTRAIN.app"
SCENE_TOOL="$ROOT/build/scene_tool"
SCENE_SRC="$ROOT/EGTRAIN/QEGTRAIN/Scenes/Lebanon"
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
PKG="$WORK/QEGTRAIN-Lebanon"
HOME_DIR="$WORK/home"
OUT_DIR="$WORK/out"
mkdir -p "$PKG" "$HOME_DIR" "$OUT_DIR"

# Assemble the presentation package.
cp -R "$APP_BUNDLE" "$PKG/QEGTRAIN.app"
cp "$SCENE_TOOL" "$PKG/scene_tool"
mkdir -p "$PKG/Scenes"
cp -R "$SCENE_SRC" "$PKG/Scenes/Lebanon"
cp "$GUIDE_SRC" "$PKG/lebanon-case-study.md"

# Content check: every required item must be present.
require() {
	if [[ ! -e "$1" ]]; then
		echo "package is missing: $1" >&2
		exit 1
	fi
}
require "$PKG/QEGTRAIN.app/Contents/MacOS/QEGTRAIN"
require "$PKG/QEGTRAIN.app/Contents/Resources/egtrain.icns"
test "$(/usr/libexec/PlistBuddy -c 'Print :CFBundleIconFile' "$PKG/QEGTRAIN.app/Contents/Info.plist")" = "egtrain.icns"
require "$PKG/scene_tool"
require "$PKG/Scenes/Lebanon/stations.json"
require "$PKG/Scenes/Lebanon/legacy/Tracklines/B0/BlockCumPari.txt"
require "$PKG/Scenes/Lebanon/legacy/Tracklines/B7/BlockCumPari.txt"
require "$PKG/lebanon-case-study.md"
# Note: the release workflow deploys and verifies the bundled Qt runtime. A local
# build links the system Qt, so the runtime check runs in CI, not here.

# The packaged scene_tool validates the packaged scene and reports only the
# expected missing train and service data.
val="$("$PKG/scene_tool" validate "$PKG/Scenes/Lebanon" 2>&1 || true)"
echo "$val" | grep -q "scene.services.none" || { echo "packaged validate missing services error" >&2; exit 1; }
echo "$val" | grep -q "scene.trains.none" || { echo "packaged validate missing trains error" >&2; exit 1; }
unexpected="$(echo "$val" | grep -E '^error' | grep -vE 'scene.services.none|scene.trains.none' || true)"
[[ -z "$unexpected" ]] || { echo "packaged validate reported unexpected errors: $unexpected" >&2; exit 1; }

# The packaged scene_tool exports the scene to a directory outside the package,
# keeping the B0-B7 passthrough.
"$PKG/scene_tool" export "$PKG/Scenes/Lebanon" "$OUT_DIR/export"
for b in 0 7; do
	require "$OUT_DIR/export/TrackLines/B$b/BlockCumPari.txt"
done

# The packaged app opens and renders the packaged scene under a clean HOME, with
# its save and reload check writing to a copy outside the bundle.
SCENE_COPY="$OUT_DIR/scene"
cp -R "$PKG/Scenes/Lebanon" "$SCENE_COPY"
LOG="$WORK/track_preview.log"
# Marker to detect writes into the bundle during the run (copies above reset
# file times, so compare against a fresh marker rather than a source file).
MARKER="$WORK/before_run"
touch "$MARKER"
set +e
cd "$ROOT/EGTRAIN/QEGTRAIN"
env -i HOME="$HOME_DIR" PATH="/usr/bin:/bin:/usr/sbin:/sbin" \
	QT_QPA_PLATFORM=offscreen \
	QEGTRAIN_E2E_TRACK_PREVIEW=1 \
	QEGTRAIN_E2E_SCENE="$SCENE_COPY" \
	"$PKG/QEGTRAIN.app/Contents/MacOS/QEGTRAIN" -n 3 -h 100 -g 1 -pax 0 -TSM 0 -RC 0 >"$LOG" 2>&1
set -e
for marker in E2E_TRACK_PREVIEW_OPEN_OK E2E_TRACK_PREVIEW_RUN_GATED_OK E2E_TRACK_PREVIEW_SAVE_RELOAD_OK; do
	grep -q "$marker" "$LOG" || { echo "packaged app missing marker $marker; see $LOG" >&2; exit 1; }
done

# The supplied Lebanon scene in the package must stay unchanged; the user edits
# the copy that lives outside the package.
changed="$(find "$PKG/Scenes/Lebanon" -newer "$MARKER" -type f 2>/dev/null)"
if [[ -n "$changed" ]]; then
	echo "the packaged Lebanon scene was modified during the run:" >&2
	echo "$changed" >&2
	exit 1
fi

echo "package contents smoke passed: app, scene_tool, Lebanon scene, and guide present; scene validated, exported, opened, and reloaded from the package"
