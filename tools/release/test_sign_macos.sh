#!/usr/bin/env bash
# Tests tools/release/sign_macos.sh on a fixture bundle with a framework, a plug-in and two programs next to the main one.
#
# The script runs with a codesign in front of the real one on the search path. It records each call, one line of arguments
# joined by "|", and passes the call on to /usr/bin/codesign with --timestamp turned into --timestamp=none, because a secure
# timestamp needs Apple's server. An identity of "-" lets the real tool take the Developer ID path without a certificate:
# the hardened runtime flag, the order and the final verification are the real ones. A made-up identity name with a
# recording that signs nothing shows that the name reaches every call as one argument.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
SIGN="$ROOT/tools/release/sign_macos.sh"

if [ "$(uname -s)" != Darwin ]; then
	echo "test_sign_macos needs macOS: codesign, cc and PlistBuddy" >&2
	exit 1
fi

WORK="$(mktemp -d "${TMPDIR:-/tmp}/egtrain-sign.XXXXXX")"
trap 'rm -rf "$WORK"' EXIT

fail() {
	echo "FAIL: $*" >&2
	exit 1
}

# A codesign that records its arguments in $CODESIGN_LOG and then signs for real unless CODESIGN_RECORD_ONLY is 1.
STUB="$WORK/stub"
mkdir -p "$STUB"
cat > "$STUB/codesign" <<'STUB_SCRIPT'
#!/bin/bash
(IFS='|'; echo "$*") >> "$CODESIGN_LOG"
[ "${CODESIGN_RECORD_ONLY:-}" = 1 ] && exit 0
arguments=()
for argument in "$@"; do
	[ "$argument" = --timestamp ] && argument=--timestamp=none
	arguments+=("$argument")
done
exec /usr/bin/codesign "${arguments[@]}"
STUB_SCRIPT
chmod +x "$STUB/codesign"

# The fixture holds one file of each kind the script signs. The compiler gives each Mach-O file its own ad-hoc signature.
PARTS="$WORK/parts"
PRISTINE="$PARTS/Fixture.app"
FRAMEWORK="$PRISTINE/Contents/Frameworks/Fixture.framework"
mkdir -p "$PRISTINE/Contents/MacOS" "$PRISTINE/Contents/Resources" "$PRISTINE/Contents/PlugIns/imageformats" \
	"$FRAMEWORK/Versions/A/Resources"
echo 'int main(void) { return 0; }' > "$PARTS/program.c"
echo 'int fixture_value(void) { return 1; }' > "$PARTS/library.c"
cc "$PARTS/program.c" -o "$PRISTINE/Contents/MacOS/Fixture"
cc "$PARTS/program.c" -o "$PRISTINE/Contents/MacOS/egtrain_update_helper"
cc "$PARTS/program.c" -o "$PRISTINE/Contents/MacOS/scene_tool"
cc -dynamiclib "$PARTS/library.c" -install_name @rpath/libfixture.dylib -o "$PRISTINE/Contents/Frameworks/libfixture.dylib"
cc -dynamiclib "$PARTS/library.c" -install_name @rpath/libplugin.dylib -o "$PRISTINE/Contents/PlugIns/imageformats/libplugin.dylib"
cc -dynamiclib "$PARTS/library.c" -install_name @rpath/Fixture.framework/Versions/A/Fixture -o "$FRAMEWORK/Versions/A/Fixture"
ln -s A "$FRAMEWORK/Versions/Current"
ln -s Versions/Current/Fixture "$FRAMEWORK/Fixture"
ln -s Versions/Current/Resources "$FRAMEWORK/Resources"
echo 'data' > "$PRISTINE/Contents/Resources/data.txt"
cat > "$PRISTINE/Contents/Info.plist" <<'PLIST'
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><dict>
<key>CFBundleExecutable</key><string>Fixture</string>
<key>CFBundleIdentifier</key><string>test.egtrain.signing.fixture</string>
<key>CFBundlePackageType</key><string>APPL</string>
<key>CFBundleVersion</key><string>1</string>
</dict></plist>
PLIST
cat > "$FRAMEWORK/Versions/A/Resources/Info.plist" <<'PLIST'
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><dict>
<key>CFBundleExecutable</key><string>Fixture</string>
<key>CFBundleIdentifier</key><string>test.egtrain.signing.fixture.framework</string>
<key>CFBundlePackageType</key><string>FMWK</string>
<key>CFBundleVersion</key><string>1</string>
</dict></plist>
PLIST

# What codesign prints about the signature of a path. It is read into a variable: grep -q ends a pipe early, and with pipefail
# the signal kills the writer.
describe() {
	/usr/bin/codesign -d --verbose=2 "$1" 2>&1
}

# A copy of the fixture for one case, printed as the path of its app.
fresh_app() {
	mkdir -p "$WORK/$1"
	ditto "$PRISTINE" "$WORK/$1/Fixture.app"
	echo "$WORK/$1/Fixture.app"
}

# Runs the script on an app with the recording codesign. The other arguments are environment settings for the script.
sign_app() {
	local name="$1" app="$2"
	shift 2
	: > "$WORK/$name.log"
	if ! env -u MACOS_SIGNING_IDENTITY PATH="$STUB:$PATH" CODESIGN_LOG="$WORK/$name.log" "$@" \
		bash "$SIGN" "$app" > "$WORK/$name.out" 2> "$WORK/$name.err"; then
		cat "$WORK/$name.err" >&2
		return 1
	fi
}

# The pieces of nested code in the order the script has to sign them, relative to the app. The bundle itself comes last.
NESTED=(
	Contents/Frameworks/Fixture.framework/Versions/A/Fixture
	Contents/Frameworks/Fixture.framework
	Contents/Frameworks/libfixture.dylib
	Contents/PlugIns/imageformats/libplugin.dylib
	Contents/MacOS/egtrain_update_helper
	Contents/MacOS/scene_tool
	Contents/MacOS/Fixture
)

# The script signs nothing when no identity is set but the whole bundle, ad hoc, as the workflow did before it.
for setting in unset empty; do
	app="$(fresh_app "adhoc-$setting")"
	if [ "$setting" = unset ]; then
		sign_app "adhoc-$setting" "$app"
	else
		sign_app "adhoc-$setting" "$app" MACOS_SIGNING_IDENTITY=
	fi
	[ "$(cat "$WORK/adhoc-$setting.log")" = "--force|--deep|--sign|-|$app" ] || fail "ad-hoc signing with the identity $setting called codesign as: $(cat "$WORK/adhoc-$setting.log")"
	/usr/bin/codesign --verify --deep --strict "$app" || fail "the ad-hoc signed bundle ($setting) does not verify"
	grep -q '^Signature=adhoc$' <<< "$(describe "$app")" || fail "the bundle ($setting) is not signed ad hoc"
done

# With an identity set, the nested code is signed one by one, innermost first, with the hardened runtime and a secure
# timestamp, and the bundle is signed last without --deep.
app="$(fresh_app identity)"
sign_app identity "$app" MACOS_SIGNING_IDENTITY=-
{
	for path in "${NESTED[@]}"; do
		echo "--force|--options|runtime|--timestamp|--sign|-|$app/$path"
	done
	echo "--force|--options|runtime|--timestamp|--sign|-|$app"
} > "$WORK/identity.expected"
diff "$WORK/identity.expected" "$WORK/identity.log" || fail "the calls of codesign differ from the expected order and options"
! grep -q -e '--deep' "$WORK/identity.log" || fail "an identity signature used --deep"
/usr/bin/codesign --verify --deep --strict "$app" || fail "the bundle signed inside-out does not verify"
for path in "${NESTED[@]}" ""; do
	grep -q '^CodeDirectory .*flags=.*runtime' <<< "$(describe "$app/$path")" || fail "${path:-the bundle} lacks the hardened runtime"
done

# The identity name reaches every call as one argument. A name has spaces and parentheses.
IDENTITY='Developer ID Application: Throwaway Test (TESTID0000)'
app="$(fresh_app named)"
sign_app named "$app" MACOS_SIGNING_IDENTITY="$IDENTITY" CODESIGN_RECORD_ONLY=1 \
	MACOS_CERTIFICATE=certificate-that-must-not-appear MACOS_CERTIFICATE_PASSWORD=password-that-must-not-appear
{
	for path in "${NESTED[@]}"; do
		echo "--force|--options|runtime|--timestamp|--sign|$IDENTITY|$app/$path"
	done
	echo "--force|--options|runtime|--timestamp|--sign|$IDENTITY|$app"
} > "$WORK/named.expected"
diff "$WORK/named.expected" "$WORK/named.log" || fail "the identity name did not reach every call as one argument"
if grep -q -e 'must-not-appear' "$WORK/named.out" "$WORK/named.err"; then
	fail "the script printed a credential"
fi

# Mach-O code outside Frameworks, PlugIns and MacOS cannot be signed in order, so the script stops before it signs anything.
app="$(fresh_app stray)"
cp "$app/Contents/MacOS/scene_tool" "$app/Contents/Resources/tool"
if sign_app stray "$app" MACOS_SIGNING_IDENTITY=- 2> /dev/null; then
	fail "the script signed a bundle with Mach-O code in Resources"
fi
grep -q 'Resources/tool' "$WORK/stray.err" || fail "the message does not name the file in Resources"
[ ! -s "$WORK/stray.log" ] || fail "the script signed something before it found the file in Resources"

# A call without an app, or with a folder that is no app, is refused.
for argument in "" "$WORK"; do
	set +e
	if [ -z "$argument" ]; then bash "$SIGN" 2> /dev/null; else bash "$SIGN" "$argument" 2> /dev/null; fi
	status=$?
	set -e
	[ "$status" -eq 2 ] || fail "the script exited with $status for the argument '$argument', not 2"
done

echo "OK: sign_macos signs ad hoc without an identity and inside-out with one"
