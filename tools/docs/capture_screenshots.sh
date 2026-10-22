#!/usr/bin/env bash
# Usage: capture_screenshots.sh [--check | --taken]
#
# Takes the pictures of docs/images with a build that has EGTRAIN_BUILD_TESTS on, and writes them with
# docs/images/captures.json. Entries of captures.json marked manual, which no script takes, are kept. The pictures show
# the platform the program runs on, so take them on macOS with a screen at a device pixel ratio of 1, such as a virtual
# machine; on another screen the capture run stops. QT_QPA_PLATFORM, when set, chooses another platform.
#   --check  takes the pictures into a temporary folder instead and lists the ones that are missing, have another size
#            than captures.json says, or differ from docs/images in more than 2 percent of their pixels. A pixel of
#            nearly the same colour at the same place or next to it is no difference. It exits with 1 when there are any.
#   --taken  takes them on the offscreen platform and lists only the ones that are missing, because the size of a
#            dialog follows the font and style of the platform. CTest runs this.
# QEGTRAIN_APP names the program; the default is build/QEGTRAIN.app/Contents/MacOS/QEGTRAIN.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
APP="${QEGTRAIN_APP:-$ROOT/build/QEGTRAIN.app/Contents/MacOS/QEGTRAIN}"
IMAGES="$ROOT/docs/images"
MODE="${1:-}"
case "$MODE" in
"" | --check | --taken) ;;
*)
	echo "usage: capture_screenshots.sh [--check | --taken]" >&2
	exit 2
	;;
esac
if [[ ! -x "$APP" ]]; then
	echo "QEGTRAIN app not found or not executable: $APP" >&2
	exit 1
fi

WORK="$(mktemp -d "${TMPDIR:-/tmp}/egtrain-captures.XXXXXX")"
trap 'rm -rf "$WORK"' EXIT
mkdir -p "$WORK/pictures"

# The run starts in EGTRAIN/QEGTRAIN, as a user's would, with settings, output and pictures of its own.
[[ "$MODE" == --taken ]] && export QT_QPA_PLATFORM=offscreen
set +e
(cd "$ROOT/EGTRAIN/QEGTRAIN" &&
	QT_SCALE_FACTOR=1 \
		QEGTRAIN_E2E_CAPTURES="$WORK/pictures" \
		QEGTRAIN_E2E_CAPTURE_COMPARE="${MODE:+$IMAGES}" \
		QEGTRAIN_E2E_SETTINGS_DIR="$WORK/settings" \
		QEGTRAIN_OUTPUT_DIR="$WORK/output" \
		"$APP" --scene Scenes/Netherlands -g 1) >"$WORK/log" 2>&1
status=$?
set -e
# The markers can follow other output of the program on the same line.
if [[ $status -ne 0 ]] || ! grep -q 'E2E_CAPTURES_OK' "$WORK/log"; then
	echo "the capture run failed (exit $status):" >&2
	tail -20 "$WORK/log" >&2
	exit 1
fi

if [[ -z "$MODE" ]]; then
	cp "$WORK/pictures/"*.png "$IMAGES/"
	python3 - "$WORK/pictures/captures.json" "$IMAGES/captures.json" <<'EOF'
import json, os, sys
taken, kept = sys.argv[1], sys.argv[2]
captures = json.load(open(taken))["captures"]
if os.path.exists(kept):
    names = {entry["file"] for entry in captures}
    captures += [entry for entry in json.load(open(kept))["captures"] if "manual" in entry and entry["file"] not in names]
with open(kept, "w") as out:
    captures.sort(key=lambda entry: entry["file"])
    json.dump({"captures": captures}, out, indent=2)
    out.write("\n")
EOF
	echo "wrote $(grep -c 'E2E_CAPTURE_SAVED' "$WORK/log") pictures and captures.json to docs/images"
	exit 0
fi

pattern='E2E_CAPTURE_(MISSING|SIZE|CHANGED) .*'
[[ "$MODE" == --taken ]] && pattern='E2E_CAPTURE_MISSING .*'
if grep -oE "$pattern" "$WORK/log" | sed -E 's/^E2E_CAPTURE_[A-Z]+ //'; then
	exit 1
fi
[[ "$MODE" == --taken ]] && echo "every picture of docs/images was taken" && exit 0
echo "every picture matches docs/images"
