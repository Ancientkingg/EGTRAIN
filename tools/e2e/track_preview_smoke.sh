#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
APP="$ROOT/build/QEGTRAIN.app/Contents/MacOS/QEGTRAIN"
LOG="${TMPDIR:-/tmp}/qegtrain-track-preview-e2e.log"
TMP_ROOT=""

if [[ ! -x "$APP" ]]; then
	echo "QEGTRAIN app not found or not executable: $APP" >&2
	exit 1
fi

if [[ $# -gt 0 ]]; then
	SCENE="$1"
else
	TMP_ROOT="$(mktemp -d "${TMPDIR:-/tmp}/qegtrain-track-preview.XXXXXX")"
	trap 'rm -rf "$TMP_ROOT"' EXIT
	SCENE="$TMP_ROOT/scene"
	cp -R "$ROOT/EGTRAIN/QEGTRAIN/Scenes/Assignment_Gvc_Gdg_Ut" "$SCENE"
	python3 -c 'from pathlib import Path; import json, sys; Path(sys.argv[1]).write_text(json.dumps({"services": []}))' "$SCENE/services.json"
fi

set +e
cd "$ROOT/EGTRAIN/QEGTRAIN"
QEGTRAIN_E2E_TRACK_PREVIEW=1 \
QEGTRAIN_E2E_SCENE="$SCENE" \
"$APP" -n 3 -h 100 -g 1 -pax 0 -TSM 0 -RC 0 >"$LOG" 2>&1
APP_EXIT=$?
set -e

if [[ "$APP_EXIT" -eq 0 ]] && grep -q "E2E_TRACK_PREVIEW_OK" "$LOG"; then
	echo "track preview smoke passed"
else
	echo "track preview smoke failed (app exit $APP_EXIT)" >&2
	tail -20 "$LOG" >&2
	exit 1
fi
