#!/usr/bin/env bash
# Tests tools/release/package_macos.sh on a fixture build folder whose app looks like one that macdeployqt has filled.
#
# The steps run in the order of the package job, with tools/release/sign_macos.sh signing ad hoc in between, and each
# change is checked. Then the verify steps run on copies with one file missing or one file too many. The bundle step is
# not run: it needs macdeployqt, Qt and Homebrew. No step needs a signing identity.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
PACKAGE="$ROOT/tools/release/package_macos.sh"

if [ "$(uname -s)" != Darwin ]; then
	echo "test_package_macos needs macOS: cc, codesign, lipo and PlistBuddy" >&2
	exit 1
fi

WORK="$(mktemp -d "${TMPDIR:-/tmp}/egtrain-package.XXXXXX")"
trap 'rm -rf "$WORK"' EXIT

fail() {
	echo "FAIL: $*" >&2
	exit 1
}

# The fixture: build/QEGTRAIN.app, with build/scene_tool beside it as the build leaves it. The program is universal, is built
# for macOS 26 and has a run path into the Homebrew Cellar. The scene_tool of the fixture fails when FIXTURE_SCENE_TOOL_FAILS
# is set.
PARTS="$WORK/parts"
BUILD="$PARTS/build"
PRISTINE="$BUILD/QEGTRAIN.app"
mkdir -p "$PRISTINE/Contents/MacOS" "$PRISTINE/Contents/Frameworks" "$PRISTINE/Contents/Resources/Scenes/Paimpol"
echo 'int main(void) { return 0; }' > "$PARTS/program.c"
cat > "$PARTS/scene_tool.c" <<'SOURCE'
#include <stdio.h>
#include <stdlib.h>
int main(int argc, char **argv) {
	printf("fixture scene_tool %s %s\n", argc > 1 ? argv[1] : "", argc > 2 ? argv[2] : "");
	return getenv("FIXTURE_SCENE_TOOL_FAILS") ? 1 : 0;
}
SOURCE
echo 'int fixture_value(void) { return 1; }' > "$PARTS/library.c"
cc -arch arm64 -arch x86_64 -mmacosx-version-min=26.0 -Wl,-rpath,/opt/homebrew/Cellar/fixture/lib "$PARTS/program.c" \
	-o "$PRISTINE/Contents/MacOS/QEGTRAIN"
cc "$PARTS/program.c" -o "$PRISTINE/Contents/MacOS/egtrain_update_helper"
cc "$PARTS/scene_tool.c" -o "$BUILD/scene_tool"
cc -dynamiclib "$PARTS/library.c" -install_name @rpath/libfixture.dylib -o "$PARTS/library.dylib"

# A framework with its library, its property list and the links of a versioned bundle.
framework() {
	local directory="$PRISTINE/Contents/Frameworks/$1.framework"
	mkdir -p "$directory/Versions/A/Resources"
	cp "$PARTS/library.dylib" "$directory/Versions/A/$1"
	ln -s A "$directory/Versions/Current"
	ln -s "Versions/Current/$1" "$directory/$1"
	ln -s Versions/Current/Resources "$directory/Resources"
	cat > "$directory/Versions/A/Resources/Info.plist" <<PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><dict>
<key>CFBundleExecutable</key><string>$1</string>
<key>CFBundleIdentifier</key><string>test.egtrain.package.$1</string>
<key>CFBundlePackageType</key><string>FMWK</string>
<key>CFBundleVersion</key><string>1</string>
</dict></plist>
PLIST
}

# A library file below Contents, created with its folder.
library() {
	mkdir -p "$(dirname "$PRISTINE/Contents/$1")"
	cp "$PARTS/library.dylib" "$PRISTINE/Contents/$1"
}

for name in QtCore QtGui QtWidgets QtCharts QtSvg QtNetwork QtHelp QtDBus QtQuick QtQml QtQmlModels QtVirtualKeyboard; do
	framework "$name"
done
KEPT_LIBRARIES=(
	Frameworks/libzmq.5.dylib
	PlugIns/platforms/libqcocoa.dylib
	PlugIns/iconengines/libqsvgicon.dylib
	PlugIns/styles/libqmacstyle.dylib
	PlugIns/imageformats/libqsvg.dylib
	PlugIns/sqldrivers/libqsqlite.dylib
)
REMOVED=(
	PlugIns/virtualkeyboard
	PlugIns/platforminputcontexts
	PlugIns/imageformats/libqwebp.dylib
	PlugIns/imageformats/libqtiff.dylib
	Frameworks/QtVirtualKeyboard.framework
	Frameworks/QtQuick.framework
	Frameworks/QtQml.framework
	Frameworks/QtQmlModels.framework
	Frameworks/libwebp.7.dylib
	Frameworks/libsharpyuv.0.dylib
	Frameworks/libtiff.6.dylib
	Frameworks/liblzma.5.dylib
	PlugIns/sqldrivers/libqsqlodbc.dylib
	PlugIns/sqldrivers/libqsqlpsql.dylib
)
for path in "${KEPT_LIBRARIES[@]}" PlugIns/virtualkeyboard/libqtvirtualkeyboardplugin.dylib \
	PlugIns/platforminputcontexts/libqtvirtualkeyboardplugin.dylib PlugIns/imageformats/libqwebp.dylib \
	PlugIns/imageformats/libqtiff.dylib Frameworks/libwebp.7.dylib Frameworks/libsharpyuv.0.dylib Frameworks/libtiff.6.dylib \
	Frameworks/liblzma.5.dylib PlugIns/sqldrivers/libqsqlodbc.dylib PlugIns/sqldrivers/libqsqlpsql.dylib; do
	library "$path"
done
echo 'icon' > "$PRISTINE/Contents/Resources/egtrain.icns"
echo 'manual' > "$PRISTINE/Contents/Resources/EGTRAIN.qch"
for name in scene stations scenarios; do
	echo '{}' > "$PRISTINE/Contents/Resources/Scenes/Paimpol/$name.json"
done
cat > "$PRISTINE/Contents/Info.plist" <<'PLIST'
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><dict>
<key>CFBundleExecutable</key><string>QEGTRAIN</string>
<key>CFBundleIconFile</key><string>egtrain.icns</string>
<key>CFBundleIdentifier</key><string>test.egtrain.package.fixture</string>
<key>CFBundlePackageType</key><string>APPL</string>
<key>CFBundleVersion</key><string>1</string>
</dict></plist>
PLIST

# Runs a step on an app and keeps its output in $WORK/<name>.out. The other arguments are environment settings for the script.
run_step() {
	local name="$1" step="$2" app="$3"
	shift 3
	env -u GITHUB_STEP_SUMMARY -u MACOS_SIGNING_IDENTITY "$@" bash "$PACKAGE" "$step" "$app" > "$WORK/$name.out" 2>&1
}

# Runs a step that has to fail and checks that its output names the reason.
expect_failure() {
	local name="$1" reason="$2" step="$3" app="$4"
	shift 4
	if run_step "$name" "$step" "$app" "$@"; then
		fail "$step passed on an app with $name"
	fi
	grep -qF -- "$reason" "$WORK/$name.out" || fail "$step on an app with $name did not print '$reason': $(cat "$WORK/$name.out")"
}

ditto "$BUILD" "$WORK/job/build"
APP="$WORK/job/build/QEGTRAIN.app"

run_step add-files add-files "$APP" || fail "add-files failed: $(cat "$WORK/add-files.out")"
test -x "$APP/Contents/MacOS/scene_tool" || fail "add-files did not copy scene_tool into Contents/MacOS"
cmp -s "$ROOT/LICENSE" "$APP/Contents/Resources/LICENSE" || fail "add-files did not copy LICENSE"
cmp -s "$ROOT/THIRD_PARTY_NOTICES.md" "$APP/Contents/Resources/THIRD_PARTY_NOTICES.md" || fail "add-files did not copy THIRD_PARTY_NOTICES.md"

# The removal writes the size before and after into the job summary when there is one, and works without one.
ditto "$APP" "$WORK/no-summary/QEGTRAIN.app"
run_step trim-without-summary trim "$WORK/no-summary/QEGTRAIN.app" || fail "trim failed without a job summary: $(cat "$WORK/trim-without-summary.out")"
run_step trim trim "$APP" GITHUB_STEP_SUMMARY="$WORK/summary.md" || fail "trim failed: $(cat "$WORK/trim.out")"
for path in "${REMOVED[@]}"; do
	[ ! -e "$APP/Contents/$path" ] || fail "trim left $path"
done
for path in "${KEPT_LIBRARIES[@]}" Frameworks/QtCore.framework Frameworks/QtDBus.framework Frameworks/QtSvg.framework; do
	[ -e "$APP/Contents/$path" ] || fail "trim removed $path"
done
grep -Eq '^macOS app before trimming: [0-9]+ files, [0-9]+ bytes$' "$WORK/summary.md" || fail "the job summary lacks the size before trimming"
grep -Eq '^macOS app after trimming: [0-9]+ files, [0-9]+ bytes$' "$WORK/summary.md" || fail "the job summary lacks the size after trimming"

run_step thin thin "$APP" || fail "thin failed: $(cat "$WORK/thin.out")"
[ "$(lipo -archs "$APP/Contents/MacOS/QEGTRAIN" 2>/dev/null)" = arm64 ] || fail "thin left $(lipo -archs "$APP/Contents/MacOS/QEGTRAIN") in the program"

expect_failure "a Homebrew run path" /opt/homebrew/Cellar verify-contents "$APP"
run_step strip-paths strip-paths "$APP" || fail "strip-paths failed: $(cat "$WORK/strip-paths.out")"
! otool -l "$APP/Contents/MacOS/QEGTRAIN" | grep -q /opt/homebrew || fail "strip-paths left the run path into the Homebrew Cellar"

run_step min-version min-version "$APP" GITHUB_STEP_SUMMARY="$WORK/summary.md" || fail "min-version failed: $(cat "$WORK/min-version.out")"
[ "$(/usr/libexec/PlistBuddy -c 'Print :LSMinimumSystemVersion' "$APP/Contents/Info.plist")" = 26.0 ] || fail "min-version did not write 26.0"
grep -q '^macOS app minimum system version: 26.0$' "$WORK/summary.md" || fail "the job summary lacks the minimum system version"

# Signs an app ad hoc, as the job does without an identity.
sign() {
	env -u MACOS_SIGNING_IDENTITY bash "$ROOT/tools/release/sign_macos.sh" "$1" > /dev/null 2>&1 || fail "sign_macos.sh failed on $1"
}

sign "$APP"
run_step verify-contents verify-contents "$APP" || fail "verify-contents failed on the packaged fixture: $(cat "$WORK/verify-contents.out")"
grep -q '^OK: app is self-contained and signed' "$WORK/verify-contents.out" || fail "verify-contents printed no OK line"
run_step verify-signature verify-signature "$APP" || fail "verify-signature failed on an ad-hoc signature: $(cat "$WORK/verify-signature.out")"
expect_failure "an identity but an ad-hoc signature" "is not signed with a Developer ID Application identity" verify-signature "$APP" \
	MACOS_SIGNING_IDENTITY='Developer ID Application: Throwaway Test (TESTID0000)'
run_step verify-package verify-package "$APP" || fail "verify-package failed on the packaged fixture: $(cat "$WORK/verify-package.out")"
grep -Eq '^fixture scene_tool validate /.*/QEGTRAIN\.app/Contents/Resources/Scenes/Paimpol$' "$WORK/verify-package.out" ||
	fail "verify-package did not run scene_tool validate on the Paimpol scene of the app"
expect_failure "a scene that does not validate" "fixture scene_tool validate" verify-package "$APP" FIXTURE_SCENE_TOOL_FAILS=1

# A copy of the packaged app for one case, printed as its path.
copy_of_app() {
	ditto "$APP" "$WORK/$1/QEGTRAIN.app"
	echo "$WORK/$1/QEGTRAIN.app"
}

app="$(copy_of_app missing-plugin)"
rm "$app/Contents/PlugIns/iconengines/libqsvgicon.dylib"
sign "$app"
expect_failure "no SVG icon engine" "ERROR: missing iconengines/libqsvgicon.dylib" verify-contents "$app"

app="$(copy_of_app missing-framework)"
rm -rf "$app/Contents/Frameworks/QtSvg.framework"
sign "$app"
if run_step missing-framework verify-contents "$app"; then
	fail "verify-contents passed on an app without QtSvg.framework"
fi

app="$(copy_of_app webp-plugin)"
cp "$PARTS/library.dylib" "$app/Contents/PlugIns/imageformats/libqwebp.dylib"
sign "$app"
expect_failure "a WebP plugin" "ERROR: unexpected libqwebp* in the app" verify-contents "$app"

app="$(copy_of_app changed-after-signing)"
echo 'changed' > "$app/Contents/Resources/egtrain.icns"
if run_step changed-after-signing verify-contents "$app"; then
	fail "verify-contents passed on an app changed after its signature"
fi

app="$(copy_of_app missing-manual)"
rm "$app/Contents/Resources/EGTRAIN.qch"
if run_step missing-manual verify-package "$app"; then
	fail "verify-package passed on an app without the manual"
fi

# A call without a step and an app, with an unknown step or with a folder that is no app is refused.
for arguments in "" "trim" "sign $APP" "trim $WORK"; do
	set +e
	bash "$PACKAGE" $arguments 2> /dev/null
	status=$?
	set -e
	[ "$status" -eq 2 ] || fail "the script exited with $status for the arguments '$arguments', not 2"
done

echo "OK: package_macos adds the files, trims, thins, strips the build paths, sets the minimum version and verifies the app"
