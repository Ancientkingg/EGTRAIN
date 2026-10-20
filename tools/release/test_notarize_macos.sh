#!/usr/bin/env bash
# Tests tools/release/notarize_macos.sh with codesign, xcrun and spctl in front of the real ones on the search path.
#
# Nothing here reaches Apple. The stubs record each call, one line of arguments joined by "|", and answer as the case says:
# an accepted or invalid verdict, a failing notarytool, a ticket that stapler gets only at the third attempt, a ticket that
# does not validate, a Gatekeeper that refuses. The test checks the arguments and their order, that an app goes to Apple as a
# zip archive made by the real ditto and an image as it is, that the key file is private and gone afterwards, that every
# failure stops the script with a status other than 0, that nothing runs without credentials, and that no value of a
# credential is printed. It cannot show that Apple accepts a file.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
NOTARIZE="$ROOT/tools/release/notarize_macos.sh"

if [ "$(uname -s)" != Darwin ]; then
	echo "test_notarize_macos needs macOS: ditto and stat -f" >&2
	exit 1
fi

WORK="$(mktemp -d "${TMPDIR:-/tmp}/egtrain-notarize-test.XXXXXX")"
trap 'rm -rf "$WORK"' EXIT

CASE=""
fail() {
	echo "FAIL: $*" >&2
	if [ -n "$CASE" ]; then
		for file in calls.log out err; do
			echo "--- $file of the last run" >&2
			cat "$CASE/$file" >&2 || true
		done
	fi
	exit 1
}

# The stubs. Each one logs its call to $STUB_CASE/calls.log.
STUB="$WORK/stub"
mkdir -p "$STUB"
cat > "$STUB/codesign" <<'STUB_SCRIPT'
#!/bin/bash
(IFS='|'; echo "codesign|$*") >> "$STUB_CASE/calls.log"
case "${CODESIGN_MODE:-developer-id}" in
	developer-id) echo "Authority=Developer ID Application: Throwaway Test (TESTID0000)" >&2 ;;
	other) echo "Authority=Apple Development: Throwaway Test (TESTID0000)" >&2 ;;
	*) echo "$2: code object is not signed at all" >&2; exit 1 ;;
esac
STUB_SCRIPT
cat > "$STUB/xcrun" <<'STUB_SCRIPT'
#!/bin/bash
(IFS='|'; echo "xcrun|$*") >> "$STUB_CASE/calls.log"
ID=11111111-2222-3333-4444-555555555555
case "$1 $2" in
	"notarytool submit")
		[ -f "$3" ] || { echo "no file to submit: $3" >&2; exit 9; }
		case "$3" in *.zip) unzip -Z1 "$3" > "$STUB_CASE/zip.seen" ;; esac
		previous=""
		for argument in "$@"; do
			if [ "$previous" = --key ]; then
				cp "$argument" "$STUB_CASE/key.seen"
				stat -f %Lp "$argument" > "$STUB_CASE/key.mode"
				echo "$argument" > "$STUB_CASE/key.path"
			fi
			previous="$argument"
		done
		case "${NOTARY_RESULT:-accepted}" in
			error) echo "Error: the network connection was lost" >&2; exit 1 ;;
			no-verdict) printf 'Waiting for processing to complete.\nCurrent status: Accepted.........\n' ;;
			*)
				printf 'Submission ID received\n  id: %s\nUpload complete\nWaiting for processing to complete.\n' "$ID"
				printf 'Current status: In Progress.....\nCurrent status: Accepted.........\nProcessing complete\n  id: %s\n' "$ID"
				if [ "${NOTARY_RESULT:-accepted}" = accepted ]; then echo "  status: Accepted"; else echo "  status: Invalid"; fi
				;;
		esac
		;;
	"notarytool log") echo '{"status": "Invalid", "issues": [{"message": "The binary is not signed."}]}' ;;
	"stapler staple")
		count="$(cat "$STUB_CASE/staple.count" 2> /dev/null || echo 0)"
		echo $((count + 1)) > "$STUB_CASE/staple.count"
		[ "$((count + 1))" -gt "${STAPLE_FAILS:-0}" ] || { echo "Error 65: the ticket is not available yet" >&2; exit 65; }
		;;
	"stapler validate") [ "${VALIDATE_FAILS:-}" != 1 ] || { echo "Error 65: no ticket" >&2; exit 65; } ;;
	*) echo "unexpected call of xcrun: $*" >&2; exit 99 ;;
esac
STUB_SCRIPT
cat > "$STUB/spctl" <<'STUB_SCRIPT'
#!/bin/bash
(IFS='|'; echo "spctl|$*") >> "$STUB_CASE/calls.log"
[ "${SPCTL_FAILS:-}" != 1 ] || { echo "rejected source=Unnotarized Developer ID" >&2; exit 3; }
echo "accepted source=Notarized Developer ID" >&2
STUB_SCRIPT
chmod +x "$STUB/codesign" "$STUB/xcrun" "$STUB/spctl"

# The files to notarize. The script needs only the property list of an app.
APP="$WORK/files/QEGTRAIN.app"
DMG="$WORK/files/EGTRAIN-1.2.3-macOS-arm64.dmg"
mkdir -p "$APP/Contents/MacOS"
echo '<plist/>' > "$APP/Contents/Info.plist"
echo 'program' > "$APP/Contents/MacOS/QEGTRAIN"
echo 'image' > "$DMG"
echo 'notes' > "$WORK/files/notes.txt"
echo 'not a bundle' > "$WORK/files/plain.app"
mkdir -p "$WORK/files/Empty.app"

# The values of the credentials. None of them may appear in the output of the script.
KEY_TEXT=$'-----BEGIN PRIVATE KEY-----\nsecret-key-text\n-----END PRIVATE KEY-----'
API_KEY=(MACOS_NOTARY_KEY="$KEY_TEXT" MACOS_NOTARY_KEY_ID=KEYID12345 MACOS_NOTARY_ISSUER_ID=issuer-id-12345)
APPLE_ID=(MACOS_NOTARY_APPLE_ID=someone@example.com MACOS_NOTARY_TEAM_ID=TEAMID1234 MACOS_NOTARY_PASSWORD=app-specific-secret)
VALUES=(secret-key-text KEYID12345 issuer-id-12345 someone@example.com TEAMID1234 app-specific-secret)
ALL_NAMES=(MACOS_NOTARY_KEY MACOS_NOTARY_KEY_ID MACOS_NOTARY_ISSUER_ID MACOS_NOTARY_APPLE_ID MACOS_NOTARY_TEAM_ID MACOS_NOTARY_PASSWORD)

STATUS=0

# run NAME [SCRIPT ARGUMENT...] [-- VARIABLE=VALUE...]
# Runs the script with none of the credentials but the given ones. The result is in STATUS and in the files of $CASE.
run() {
	local name="$1" arguments=() settings=() unset_flags=() variable value
	shift
	while [ "$#" -gt 0 ] && [ "$1" != -- ]; do
		arguments+=("$1")
		shift
	done
	[ "$#" -eq 0 ] || shift
	settings=("$@")
	for variable in "${ALL_NAMES[@]}"; do
		unset_flags+=(-u "$variable")
	done
	CASE="$WORK/case-$name"
	rm -rf "$CASE"
	mkdir -p "$CASE/tmp"
	: > "$CASE/calls.log"
	set +e
	env "${unset_flags[@]}" PATH="$STUB:$PATH" TMPDIR="$CASE/tmp" STUB_CASE="$CASE" STAPLE_RETRY_SECONDS=0 \
		"${settings[@]+"${settings[@]}"}" bash "$NOTARIZE" "${arguments[@]+"${arguments[@]}"}" > "$CASE/out" 2> "$CASE/err"
	STATUS=$?
	set -e
	for value in "${VALUES[@]}"; do
		if grep -qF -e "$value" "$CASE/out" "$CASE/err"; then
			fail "$name: the script printed the credential value $value"
		fi
	done
}

# The calls of the last run, with the private folder of the script written as WORK.
calls() {
	sed -E "s#$CASE/tmp/egtrain-notarize\.[A-Za-z0-9]+#WORK#g" "$CASE/calls.log"
}

expect_calls() {
	local name="$1"
	calls | diff - "$CASE/expected" > "$CASE/calls.diff" || {
		cat "$CASE/calls.diff" >&2
		fail "$name: the calls differ from the expected ones"
	}
}

expect_status() {
	[ "$STATUS" -eq "$2" ] || fail "$1: the script exited with $STATUS, not $2"
}

expect_failed() {
	[ "$STATUS" -ne 0 ] || fail "$1: the script succeeded"
}

expect_text() {
	grep -q -e "$3" "$CASE/$2" || fail "$1: $2 lacks the text '$3'"
}

expect_clean() {
	[ -z "$(ls -A "$CASE/tmp")" ] || fail "$1: the script left $(ls -A "$CASE/tmp") in its temporary folder"
}

# A wrong call is refused with the status 2, before anything else runs, whatever the credentials are.
run no-argument -- "${API_KEY[@]}"
expect_status no-argument 2
run two-arguments "$APP" "$DMG" -- "${API_KEY[@]}"
expect_status two-arguments 2
run missing-image "$WORK/files/missing.dmg" -- "${API_KEY[@]}"
expect_status missing-image 2
run text-file "$WORK/files/notes.txt" -- "${API_KEY[@]}"
expect_status text-file 2
run app-without-plist "$WORK/files/Empty.app" -- "${API_KEY[@]}"
expect_status app-without-plist 2
run app-as-file "$WORK/files/plain.app" -- "${API_KEY[@]}"
expect_status app-as-file 2
for name in no-argument two-arguments missing-image text-file app-without-plist app-as-file; do
	[ ! -s "$WORK/case-$name/calls.log" ] || fail "$name: a tool ran after a wrong call"
	grep -q '^usage: ' "$WORK/case-$name/err" || fail "$name: no usage message"
done

# Without credentials the script does nothing and succeeds, for an app and for an image, with none of the variables set and
# with all of them empty, which is what a workflow passes for a secret or variable that does not exist.
EMPTY=()
for variable in "${ALL_NAMES[@]}"; do
	EMPTY+=("$variable=")
done
for file in "$APP" "$DMG"; do
	for setting in unset empty; do
		name="skip-${file##*.}-$setting"
		if [ "$setting" = unset ]; then run "$name" "$file"; else run "$name" "$file" -- "${EMPTY[@]}"; fi
		expect_status "$name" 0
		expect_text "$name" out "Notarization skipped: no notarization credentials are configured, so ${file##*/} is not notarized"
		[ ! -s "$CASE/calls.log" ] || fail "$name: a tool ran without credentials: $(cat "$CASE/calls.log")"
		expect_clean "$name"
	done
done

# Credentials that are only partly there fail the script, name what is missing and run nothing.
run only-key "$APP" -- MACOS_NOTARY_KEY="$KEY_TEXT"
expect_status only-key 1
expect_text only-key err "Not set for the API key: MACOS_NOTARY_KEY_ID MACOS_NOTARY_ISSUER_ID\."
run no-issuer "$APP" -- MACOS_NOTARY_KEY="$KEY_TEXT" MACOS_NOTARY_KEY_ID=KEYID12345
expect_status no-issuer 1
expect_text no-issuer err "Not set for the API key: MACOS_NOTARY_ISSUER_ID\."
run no-password "$DMG" -- MACOS_NOTARY_APPLE_ID=someone@example.com MACOS_NOTARY_TEAM_ID=TEAMID1234
expect_status no-password 1
expect_text no-password err "Not set for the Apple ID: MACOS_NOTARY_PASSWORD\."
run two-halves "$APP" -- MACOS_NOTARY_KEY="$KEY_TEXT" MACOS_NOTARY_KEY_ID=KEYID12345 MACOS_NOTARY_APPLE_ID=someone@example.com
expect_status two-halves 1
for name in only-key no-issuer no-password two-halves; do
	[ ! -s "$WORK/case-$name/calls.log" ] || fail "$name: a tool ran with incomplete credentials"
done

# An app goes to Apple as a zip archive with the app in it. The API key goes into a private file that notarytool reads and
# that is gone afterwards. Then the ticket is stapled and checked, and Gatekeeper assesses the app.
run key-app "$APP" -- "${API_KEY[@]}"
expect_status key-app 0
cat > "$CASE/expected" <<EXPECTED
codesign|-dvv|$APP
xcrun|notarytool|submit|WORK/QEGTRAIN.zip|--key|WORK/key.p8|--key-id|KEYID12345|--issuer|issuer-id-12345|--wait
xcrun|stapler|staple|$APP
xcrun|stapler|validate|$APP
spctl|--assess|--type|execute|--verbose=2|$APP
EXPECTED
expect_calls key-app
grep -qx 'QEGTRAIN.app/Contents/Info.plist' "$CASE/zip.seen" || fail "key-app: the archive does not hold the app: $(cat "$CASE/zip.seen")"
[ "$(cat "$CASE/key.seen")" = "$KEY_TEXT" ] || fail "key-app: notarytool read another key than the one in the environment"
[ "$(cat "$CASE/key.mode")" = 600 ] || fail "key-app: the key file has the mode $(cat "$CASE/key.mode"), not 600"
expect_text key-app out "OK: QEGTRAIN.app is notarized, stapled and accepted by Gatekeeper"
expect_clean key-app

# An image goes to Apple as it is, with the Apple ID, and Gatekeeper assesses it as a document that was opened.
run id-image "$DMG" -- "${APPLE_ID[@]}"
expect_status id-image 0
cat > "$CASE/expected" <<EXPECTED
codesign|-dvv|$DMG
xcrun|notarytool|submit|$DMG|--apple-id|someone@example.com|--team-id|TEAMID1234|--password|app-specific-secret|--wait
xcrun|stapler|staple|$DMG
xcrun|stapler|validate|$DMG
spctl|--assess|--type|open|--context|context:primary-signature|--verbose=2|$DMG
EXPECTED
expect_calls id-image
[ ! -e "$CASE/zip.seen" ] || fail "id-image: the image was sent as an archive"
[ ! -e "$CASE/key.seen" ] || fail "id-image: a key file was written for an Apple ID"
expect_clean id-image

# With both groups complete the API key is used. A trailing slash on the app does not matter.
run both "$APP/" -- "${APPLE_ID[@]}" "${API_KEY[@]}"
expect_status both 0
grep -q '|--key|' "$CASE/calls.log" || fail "both: the API key was not used"
! grep -q -e '--apple-id' "$CASE/calls.log" || fail "both: the Apple ID was used as well"

# Apple's verdict decides. A verdict of Invalid with the exit status 0 fails the script and shows Apple's log, a missing verdict
# fails it, and so does notarytool failing. Nothing is stapled or assessed in these cases.
run invalid "$APP" -- "${API_KEY[@]}" NOTARY_RESULT=invalid
expect_status invalid 1
cat > "$CASE/expected" <<EXPECTED
codesign|-dvv|$APP
xcrun|notarytool|submit|WORK/QEGTRAIN.zip|--key|WORK/key.p8|--key-id|KEYID12345|--issuer|issuer-id-12345|--wait
xcrun|notarytool|log|11111111-2222-3333-4444-555555555555|--key|WORK/key.p8|--key-id|KEYID12345|--issuer|issuer-id-12345
EXPECTED
expect_calls invalid
expect_text invalid out "The binary is not signed"
expect_text invalid err "Apple did not accept QEGTRAIN.app"
expect_clean invalid
run no-verdict "$DMG" -- "${APPLE_ID[@]}" NOTARY_RESULT=no-verdict
expect_status no-verdict 1
expect_text no-verdict err "Apple did not accept EGTRAIN-1.2.3-macOS-arm64.dmg"
grep -q 'stapler' "$CASE/calls.log" && fail "no-verdict: the script stapled without a verdict"
run notary-error "$DMG" -- "${APPLE_ID[@]}" NOTARY_RESULT=error
expect_status notary-error 1
expect_text notary-error err "Apple did not accept"
grep -Eq 'stapler|spctl|notarytool\|log' "$CASE/calls.log" && fail "notary-error: the script went on after notarytool failed"

# The ticket may not be there at once: stapler is tried up to five times. After that, and when the ticket does not validate or
# Gatekeeper refuses, the script fails.
run staple-late "$APP" -- "${API_KEY[@]}" STAPLE_FAILS=2
expect_status staple-late 0
[ "$(grep -c 'xcrun|stapler|staple|' "$CASE/calls.log")" -eq 3 ] || fail "staple-late: stapler was not called three times"
run staple-never "$APP" -- "${API_KEY[@]}" STAPLE_FAILS=99
expect_status staple-never 1
[ "$(grep -c 'xcrun|stapler|staple|' "$CASE/calls.log")" -eq 5 ] || fail "staple-never: stapler was not called five times"
expect_text staple-never err "stapler could not attach the ticket to QEGTRAIN.app in 5 attempts"
grep -Eq 'stapler\|validate|spctl' "$CASE/calls.log" && fail "staple-never: the script went on without a ticket"
run bad-ticket "$APP" -- "${API_KEY[@]}" VALIDATE_FAILS=1
expect_status bad-ticket 1
expect_text bad-ticket err "the ticket of QEGTRAIN.app does not validate"
grep -q 'spctl' "$CASE/calls.log" && fail "bad-ticket: Gatekeeper was asked about an app without a valid ticket"
run gatekeeper-refuses "$DMG" -- "${APPLE_ID[@]}" SPCTL_FAILS=1
expect_status gatekeeper-refuses 1
expect_text gatekeeper-refuses err "Gatekeeper does not accept EGTRAIN-1.2.3-macOS-arm64.dmg"
expect_clean gatekeeper-refuses

# Apple rejects what has no Developer ID signature, so the script stops before it sends anything.
for mode in other unsigned; do
	run "signed-$mode" "$DMG" -- "${APPLE_ID[@]}" CODESIGN_MODE="$mode"
	expect_status "signed-$mode" 1
	expect_text "signed-$mode" err "EGTRAIN-1.2.3-macOS-arm64.dmg is not signed with a Developer ID Application identity"
	[ "$(wc -l < "$CASE/calls.log" | tr -d ' ')" -eq 1 ] || fail "signed-$mode: a tool other than codesign ran"
done

echo "OK: notarize_macos skips without credentials, sends an app as an archive and an image as it is, and stops on every failure"
