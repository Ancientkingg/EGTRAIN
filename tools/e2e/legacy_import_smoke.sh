#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
APP="$ROOT/build/QEGTRAIN.app/Contents/MacOS/QEGTRAIN"
LOG="${TMPDIR:-/tmp}/qegtrain-legacy-import-e2e.log"
TMP_ROOT="$(mktemp -d "${TMPDIR:-/tmp}/qegtrain-legacy-import.XXXXXX")"
trap 'rm -rf "$TMP_ROOT"' EXIT
SOURCE="$TMP_ROOT/legacy"
DEST="$TMP_ROOT/imported-scene"
BAD_DEST="$TMP_ROOT/failed-scene"
mkdir -p "$SOURCE/B0" "$DEST" "$BAD_DEST"
printf '0\tFlatStart\n1.5\tFlatEnd\n' >"$SOURCE/Stations.txt"
printf '0\t0.5\t1\t0.5\n' >"$SOURCE/Connections.txt"
printf '0\nFlatStart FlatEnd\n' >"$SOURCE/TrackandStations.txt"
printf '1\t0\t0\n2\t1\t0\n' >"$SOURCE/B0/NodiCumPari.txt"
SOURCE_HASH_BEFORE="$(cksum <"$SOURCE/Stations.txt")"

if [[ ! -x "$APP" ]]; then
	echo "QEGTRAIN app not found or not executable: $APP" >&2
	exit 1
fi

set +e
cd "$ROOT/EGTRAIN/QEGTRAIN"
QEGTRAIN_E2E_LEGACY_IMPORT=1 \
QEGTRAIN_E2E_LEGACY_SOURCE="$SOURCE" \
QEGTRAIN_E2E_LEGACY_DEST="$DEST" \
QEGTRAIN_E2E_LEGACY_BAD_SOURCE="$TMP_ROOT/missing-source" \
QEGTRAIN_E2E_LEGACY_BAD_DEST="$BAD_DEST" \
QT_QPA_PLATFORM="${QT_QPA_PLATFORM:-offscreen}" \
"$APP" -n 3 -h 100 -g 1 -pax 0 -TSM 0 -RC 0 >"$LOG" 2>&1
APP_EXIT=$?
set -e

required_markers=(
	E2E_LEGACY_IMPORT_SUCCESS
	E2E_LEGACY_IMPORT_FAILURE
	E2E_LEGACY_IMPORT_STATE_PRESERVED
)
missing_markers=()
for marker in "${required_markers[@]}"; do
	if ! grep -q "$marker" "$LOG"; then
		missing_markers+=("$marker")
	fi
done

if [[ "$APP_EXIT" -eq 0 ]] && [[ "${#missing_markers[@]}" -eq 0 ]] && grep -q "E2E_LEGACY_IMPORT_OK" "$LOG"; then
	SOURCE_HASH_AFTER="$(cksum <"$SOURCE/Stations.txt")"
	if [[ "$SOURCE_HASH_BEFORE" != "$SOURCE_HASH_AFTER" ]]; then
		echo "legacy import smoke failed: source was modified" >&2
		exit 1
	fi
	echo "legacy import smoke passed"
else
	echo "legacy import smoke failed (app exit $APP_EXIT)" >&2
	if [[ "${#missing_markers[@]}" -gt 0 ]]; then
		echo "missing markers: ${missing_markers[*]}" >&2
	fi
	tail -20 "$LOG" >&2
	exit 1
fi
