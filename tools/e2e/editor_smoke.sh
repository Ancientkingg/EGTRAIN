#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
APP="$ROOT/build/QEGTRAIN.app/Contents/MacOS/QEGTRAIN"
SCENE="${1:-$ROOT/EGTRAIN/QEGTRAIN/Scenes/Assignment_Gvc_Gdg_Ut}"
ALT_SCENE="${QEGTRAIN_E2E_SCENE_ALT:-$ROOT/EGTRAIN/QEGTRAIN/Scenes/Copenhagen}"
OUT="${TMPDIR:-/tmp}/qegtrain-editor-smoke-e2e"
LOG="${TMPDIR:-/tmp}/qegtrain-editor-smoke-e2e.log"

if [[ ! -x "$APP" ]]; then
	echo "QEGTRAIN app not found or not executable: $APP" >&2
	exit 1
fi
if [[ ! -d "$ALT_SCENE" ]]; then
	echo "Alternate scene not found: $ALT_SCENE" >&2
	exit 1
fi

mkdir -p "$OUT"

# capture the app exit code without aborting so we can require BOTH a clean exit
# and the pass token; a crash after printing the token must still fail the smoke
set +e
QEGTRAIN_E2E_EDITOR_SMOKE=1 \
QEGTRAIN_E2E_SCENE="$SCENE" \
QEGTRAIN_E2E_SCENE_ALT="$ALT_SCENE" \
QEGTRAIN_E2E_OUT="$OUT" \
"$APP" -n 3 -h 100 -g 1 -pax 0 -TSM 0 -RC 0 >"$LOG" 2>&1
APP_EXIT=$?
set -e

if [[ "$APP_EXIT" -eq 0 ]] && grep -q "E2E_EDITOR_SMOKE_OK" "$LOG"; then
	echo "editor smoke passed"
else
	echo "editor smoke failed (app exit $APP_EXIT)" >&2
	echo "--- log tail ---" >&2
	tail -20 "$LOG" >&2
	exit 1
fi
