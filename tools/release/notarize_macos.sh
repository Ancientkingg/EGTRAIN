#!/usr/bin/env bash
# Notarizes a macOS app or disk image with Apple and staples the ticket to it:
#
#   notarize_macos.sh path/to/QEGTRAIN.app
#   notarize_macos.sh path/to/EGTRAIN-X.Y.Z-macOS-arm64.dmg
#
# Apple accepts only code that carries a Developer ID Application signature (tools/release/sign_macos.sh signs the app, with
# the hardened runtime and a secure timestamp; the disk image is signed with codesign). The script checks that, sends the
# file to Apple with notarytool and waits for the verdict, staples the ticket with stapler so that Gatekeeper accepts the
# file without a network, and then checks the ticket with stapler validate and the verdict of Gatekeeper with spctl. An app
# is sent as a zip archive made with ditto, an image as it is. A command that fails, or a verdict other than Accepted,
# ends the script with a status other than 0. The status 2 means a wrong call.
#
# The credentials come from the environment and nowhere else. Either an App Store Connect API key:
#
#   MACOS_NOTARY_KEY         the text of the .p8 key file
#   MACOS_NOTARY_KEY_ID      the key id
#   MACOS_NOTARY_ISSUER_ID   the issuer id
#
# or an Apple ID:
#
#   MACOS_NOTARY_APPLE_ID    the Apple ID
#   MACOS_NOTARY_TEAM_ID     the team id
#   MACOS_NOTARY_PASSWORD    an app-specific password of the Apple ID
#
# The API key is used when both are complete. When none of the six is set, or all are empty, the script says that it skipped
# the file and exits with 0: the file is not notarized. When some are set and neither group is complete, the script fails
# and names the missing variables. It prints no value of any of them. The key goes into a file of a private temporary folder
# for notarytool, which the script removes on every exit.
#
# STAPLE_RETRY_SECONDS (default 15) is the wait between the up to five attempts to staple: Apple's servers may not hand out
# the ticket right after they accepted the file.
set -euo pipefail

API_KEY_NAMES=(MACOS_NOTARY_KEY MACOS_NOTARY_KEY_ID MACOS_NOTARY_ISSUER_ID)
APPLE_ID_NAMES=(MACOS_NOTARY_APPLE_ID MACOS_NOTARY_TEAM_ID MACOS_NOTARY_PASSWORD)
STAPLE_ATTEMPTS=5
STAPLE_RETRY_SECONDS="${STAPLE_RETRY_SECONDS:-15}"

usage() {
	echo "usage: $0 path/to/App.app|path/to/Image.dmg" >&2
	exit 2
}

fail() {
	echo "ERROR: $*" >&2
	exit 1
}

# The names among the arguments whose variable is unset or empty, each after a space.
unset_names() {
	local name
	for name in "$@"; do
		[ -n "${!name:-}" ] || printf ' %s' "$name"
	done
}

[ "$#" -eq 1 ] || usage
TARGET="${1%/}"
NAME="${TARGET##*/}"
case "$TARGET" in
	*.app) [ -f "$TARGET/Contents/Info.plist" ] || usage ;;
	*.dmg) [ -f "$TARGET" ] || usage ;;
	*) usage ;;
esac

configured=0
for name in "${API_KEY_NAMES[@]}" "${APPLE_ID_NAMES[@]}"; do
	[ -z "${!name:-}" ] || configured=1
done
if [ "$configured" = 0 ]; then
	echo "Notarization skipped: no notarization credentials are configured, so $NAME is not notarized."
	exit 0
fi

missing_key="$(unset_names "${API_KEY_NAMES[@]}")"
missing_apple_id="$(unset_names "${APPLE_ID_NAMES[@]}")"
if [ -n "$missing_key" ] && [ -n "$missing_apple_id" ]; then
	fail "the notarization credentials are incomplete. Set all of ${API_KEY_NAMES[*]} for an API key," \
		"or all of ${APPLE_ID_NAMES[*]} for an Apple ID. Not set for the API key:$missing_key." \
		"Not set for the Apple ID:$missing_apple_id."
fi

signature="$(codesign -dvv "$TARGET" 2>&1 || true)"
grep -q '^Authority=Developer ID Application: ' <<< "$signature" ||
	fail "$NAME is not signed with a Developer ID Application identity, which Apple requires"

umask 077
WORK="$(mktemp -d "${TMPDIR:-/tmp}/egtrain-notarize.XXXXXX")"
trap 'rm -rf "$WORK"' EXIT

if [ -z "$missing_key" ]; then
	printf '%s\n' "$MACOS_NOTARY_KEY" > "$WORK/key.p8"
	AUTH=(--key "$WORK/key.p8" --key-id "$MACOS_NOTARY_KEY_ID" --issuer "$MACOS_NOTARY_ISSUER_ID")
else
	AUTH=(--apple-id "$MACOS_NOTARY_APPLE_ID" --team-id "$MACOS_NOTARY_TEAM_ID" --password "$MACOS_NOTARY_PASSWORD")
fi

if [ -d "$TARGET" ]; then
	SUBJECT="$WORK/${NAME%.app}.zip"
	ditto -c -k --keepParent "$TARGET" "$SUBJECT"
else
	SUBJECT="$TARGET"
fi

echo "Sending $NAME to Apple and waiting for the verdict"
status=0
xcrun notarytool submit "$SUBJECT" "${AUTH[@]}" --wait 2>&1 | tee "$WORK/submission.log" || status=$?
output="$(cat "$WORK/submission.log")"
if [ "$status" -ne 0 ] || ! grep -Eq '^[[:space:]]*status: Accepted[[:space:]]*$' <<< "$output"; then
	id="$(awk '$1 == "id:" { print $2; exit }' <<< "$output")"
	if [ -n "$id" ]; then
		echo "Apple's log of the submission $id:"
		xcrun notarytool log "$id" "${AUTH[@]}" || true
	fi
	fail "Apple did not accept $NAME"
fi

echo "Stapling the ticket to $NAME"
attempt=1
until xcrun stapler staple "$TARGET"; do
	[ "$attempt" -lt "$STAPLE_ATTEMPTS" ] || fail "stapler could not attach the ticket to $NAME in $STAPLE_ATTEMPTS attempts"
	attempt=$((attempt + 1))
	echo "Trying again in $STAPLE_RETRY_SECONDS seconds"
	sleep "$STAPLE_RETRY_SECONDS"
done
xcrun stapler validate "$TARGET" || fail "the ticket of $NAME does not validate"
if [ -d "$TARGET" ]; then
	ASSESS=(--type execute)
else
	ASSESS=(--type open --context context:primary-signature)
fi
spctl --assess "${ASSESS[@]}" --verbose=2 "$TARGET" || fail "Gatekeeper does not accept $NAME"
echo "OK: $NAME is notarized, stapled and accepted by Gatekeeper"
