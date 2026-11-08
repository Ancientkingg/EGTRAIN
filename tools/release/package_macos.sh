#!/usr/bin/env bash
# Runs one step of the macOS package job on an app bundle: package_macos.sh STEP path/to/QEGTRAIN.app
#
# The steps, in the order of the job:
#   bundle            copy Qt, its plugins and the other libraries into the app, and the update helper
#   add-files         copy scene_tool and the licence files into the app
#   trim              remove the Qt modules and plugins that the app does not use
#   thin              keep only the arm64 code of the Qt 6 libraries
#   strip-paths       remove the run paths and install names of the build machine
#   min-version       write the minimum macOS version of the executable into the property list
#   verify-contents   check that the app is self-contained and that its signature is valid
#   verify-signature  print who signed the app and check the kind of signature
#   verify-package    check the programs, the licence files, the manual and the scenes, and validate the Paimpol scene
#
# The steps up to min-version change the app, so they run before tools/release/sign_macos.sh signs it, and the verify
# steps run after it. bundle needs QT_ROOT_DIR, the Qt folder with bin/macdeployqt, and Homebrew. bundle and add-files
# take egtrain_update_helper and scene_tool from the folder that holds the app, which is the build folder. trim and
# min-version also write their lines into the job summary when GITHUB_STEP_SUMMARY is set.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"

usage() {
	echo "usage: $0 STEP path/to/QEGTRAIN.app, where STEP is one of: bundle add-files trim thin strip-paths min-version" \
		"verify-contents verify-signature verify-package" >&2
	exit 2
}
if [ "$#" -ne 2 ] || [ ! -f "$2/Contents/Info.plist" ]; then
	usage
fi
case "$1" in
	bundle | add-files | trim | thin | strip-paths | min-version | verify-contents | verify-signature | verify-package) ;;
	*) usage ;;
esac
STEP="$1"
APP="$(cd "$2" && pwd)"
BUILD="$(dirname "$APP")"

# Prints a line and adds it to the job summary when there is one.
summary() {
	echo "$1"
	if [ -n "${GITHUB_STEP_SUMMARY:-}" ]; then
		echo "$1" >> "$GITHUB_STEP_SUMMARY"
	fi
}

bundle() {
	: "${QT_ROOT_DIR:?bundle needs QT_ROOT_DIR, the Qt folder that holds bin/macdeployqt}"
	set -x
	# macdeployqt copies the Qt frameworks and plugins into the bundle and also relocates the non-Qt dependencies (libzmq,
	# libsodium) into Contents/Frameworks, rewriting every reference to @executable_path so the app runs without Homebrew.
	"$QT_ROOT_DIR/bin/macdeployqt" "$APP" -verbose=1
	cp "$BUILD/egtrain_update_helper" "$APP/Contents/MacOS/egtrain_update_helper"
	chmod +x "$APP/Contents/MacOS/egtrain_update_helper"
	# A Homebrew library can name its own dependencies as @rpath/<name>. macdeployqt copies the library but neither the
	# dependency nor an rpath, so copy each missing dependency from the Homebrew prefix and point the reference at the copy.
	local frameworks="$APP/Contents/Frameworks" copied=1 dylib dependency name
	while [ "$copied" = 1 ]; do
		copied=0
		while IFS= read -r dylib; do
			for dependency in $(otool -L "$dylib" | tail -n +2 | awk '{ print $1 }' | grep -E '^@rpath/[^/]+$' || true); do
				name="${dependency#@rpath/}"
				if [ ! -f "$frameworks/$name" ]; then
					cp "$(brew --prefix)/lib/$name" "$frameworks/$name"
					chmod u+w "$frameworks/$name"
					install_name_tool -id "@executable_path/../Frameworks/$name" "$frameworks/$name"
					copied=1
				fi
				install_name_tool -change "$dependency" "@executable_path/../Frameworks/$name" "$dylib"
			done
		done < <(find "$frameworks" -maxdepth 1 -type f -name '*.dylib')
	done
}

# The disk image holds only the app and the link to /Applications, so these files live in the bundle.
add_files() {
	set -x
	cp "$BUILD/scene_tool" "$APP/Contents/MacOS/scene_tool"
	chmod +x "$APP/Contents/MacOS/scene_tool"
	cp "$ROOT/LICENSE" "$ROOT/THIRD_PARTY_NOTICES.md" "$APP/Contents/Resources/"
}

report_size() {
	local files bytes
	files="$(find "$APP" -type f | wc -l | tr -d ' ')"
	bytes="$(find "$APP" -type f -exec /usr/bin/stat -f %z {} + | awk '{ sum += $1 } END { print sum }')"
	summary "macOS app $1: $files files, $bytes bytes"
}

trim() {
	report_size "before trimming"
	# The application is a Widgets program that draws with the raster engine, loads no QML, WebP or TIFF file and takes
	# keyboard input from the system. The Qt install of the job has no virtual keyboard, WebP or TIFF plugin, so these
	# removals only keep them out of the app if a later install has them. QtGui loads QtDBus, so it stays.
	rm -rf "$APP/Contents/PlugIns/virtualkeyboard" "$APP/Contents/PlugIns/platforminputcontexts"
	rm -rf "$APP/Contents/PlugIns/imageformats/libqwebp.dylib" "$APP/Contents/PlugIns/imageformats/libqtiff.dylib"
	rm -rf "$APP/Contents/Frameworks/QtVirtualKeyboard.framework" "$APP/Contents/Frameworks/QtQuick.framework" \
		"$APP/Contents/Frameworks/QtQml.framework" "$APP/Contents/Frameworks/QtQmlModels.framework"
	rm -f "$APP"/Contents/Frameworks/libwebp*.dylib "$APP"/Contents/Frameworks/libsharpyuv*.dylib \
		"$APP"/Contents/Frameworks/libtiff*.dylib "$APP"/Contents/Frameworks/liblzma*.dylib
	# The manual reads its help file through the SQLite driver. The other database drivers need client libraries that the
	# app does not hold.
	find "$APP/Contents/PlugIns/sqldrivers" -type f ! -name 'libqsqlite.dylib' -delete
	report_size "after trimming"
}

# The frameworks of the official Qt 6 hold x86_64 and arm64 code, and the app runs on arm64 only.
thin() {
	find "$APP" -type f | while read -r file; do
		case "$(lipo -archs "$file" 2>/dev/null)" in
			*x86_64*arm64* | *arm64*x86_64*) lipo -thin arm64 "$file" -output "$file.arm64" && mv "$file.arm64" "$file" ;;
		esac
	done
}

# Some Homebrew libraries keep a run path into the Cellar or an install name under /opt/homebrew. Nothing loads through
# them, but they would let the app load a library from a machine that has Homebrew.
strip_paths() {
	python3 "$ROOT/tools/release/macos_build_paths.py" fix "$APP"
}

# The app is built for the deployment target of the configure step. This key tells Launch Services the same, so an older
# macOS says that the app needs a newer one.
min_version() {
	local minimum
	minimum="$(otool -l "$APP/Contents/MacOS/QEGTRAIN" | awk '/cmd LC_BUILD_VERSION/ { found = 1 } found && $1 == "minos" && !printed { print $2; printed = 1 }')"
	test -n "$minimum"
	/usr/libexec/PlistBuddy -c "Add :LSMinimumSystemVersion string $minimum" "$APP/Contents/Info.plist"
	summary "macOS app minimum system version: $minimum"
}

verify_contents() {
	test -d "$APP/Contents/Frameworks/QtCore.framework"
	test -d "$APP/Contents/Frameworks/QtCharts.framework"
	test -d "$APP/Contents/Frameworks/QtSvg.framework"
	test -d "$APP/Contents/Frameworks/QtNetwork.framework"
	test -d "$APP/Contents/Frameworks/QtHelp.framework"
	test -f "$APP/Contents/PlugIns/imageformats/libqsvg.dylib"
	# The manual reads its help file through Qt's SQLite driver.
	test -f "$APP/Contents/PlugIns/sqldrivers/libqsqlite.dylib"
	test -f "$APP/Contents/Resources/egtrain.icns"
	test "$(/usr/libexec/PlistBuddy -c 'Print :CFBundleIconFile' "$APP/Contents/Info.plist")" = "egtrain.icns"
	find "$APP" -iname 'libzmq*.dylib' | grep -q . || { echo "ERROR: libzmq not bundled"; exit 1; }
	# No Mach-O file may name a path under /opt/homebrew or /usr/local in its dependencies, install name or run paths.
	python3 "$ROOT/tools/release/macos_build_paths.py" check "$APP"
	for plugin in platforms/libqcocoa.dylib iconengines/libqsvgicon.dylib styles/libqmacstyle.dylib; do
		test -f "$APP/Contents/PlugIns/$plugin" || { echo "ERROR: missing $plugin"; exit 1; }
	done
	for name in virtualkeyboard platforminputcontexts 'QtVirtualKeyboard*' 'QtQuick*' 'QtQml*' 'libqwebp*' 'libqtiff*' 'libwebp*' 'libtiff*'; do
		found="$(find "$APP" -name "$name")"
		if [ -n "$found" ]; then echo "ERROR: unexpected $name in the app:"; echo "$found"; exit 1; fi
	done
	# The signature must be valid, or Apple Silicon kills the app at launch.
	codesign --verify --deep --strict --verbose=2 "$APP"
	echo "OK: app is self-contained and signed, without the virtual keyboard, QML, WebP and TIFF files"
}

# A Developer ID signature needs a team, the hardened runtime and a secure timestamp. Without an identity the signature has
# to be ad hoc.
verify_signature() {
	local details
	details="$(codesign -dvv "$APP" 2>&1)"
	grep -E '^(Identifier|Signature|Authority|TeamIdentifier|Timestamp|CodeDirectory)[ =]' <<< "$details"
	expect() {
		grep -Eq "$1" <<< "$details" || { echo "ERROR: $2"; exit 1; }
	}
	if [ -n "${MACOS_SIGNING_IDENTITY:-}" ]; then
		expect '^Authority=Developer ID Application: ' "the app is not signed with a Developer ID Application identity"
		expect '^TeamIdentifier=[A-Z0-9]{10}$' "the signature names no team"
		expect '^CodeDirectory .*flags=.*runtime' "the hardened runtime is off"
		expect '^Timestamp=' "the signature has no secure timestamp"
	else
		expect '^Signature=adhoc$' "the signature is not ad hoc"
	fi
}

# The job starts a copy of the app with tools/release/package_start_smoke.py afterwards, which runs Paimpol without the GUI.
verify_package() {
	test -x "$APP/Contents/MacOS/QEGTRAIN"
	test -x "$APP/Contents/MacOS/egtrain_update_helper"
	test -x "$APP/Contents/MacOS/scene_tool"
	test -f "$APP/Contents/Resources/LICENSE"
	test -f "$APP/Contents/Resources/THIRD_PARTY_NOTICES.md"
	test -f "$APP/Contents/Resources/EGTRAIN.qch"
	test -f "$APP/Contents/Resources/Scenes/Paimpol/stations.json"
	test -f "$APP/Contents/Resources/Scenes/Paimpol/scene.json"
	test -f "$APP/Contents/Resources/Scenes/Paimpol/scenarios.json"
	test -d "$APP/Contents/Resources/Scenes/Paimpol"
	# scene_tool uses header-only JSON and vendored miniz, so it runs without Qt.
	local validation_output validation_status
	set +e
	validation_output="$("$APP/Contents/MacOS/scene_tool" validate "$APP/Contents/Resources/Scenes/Paimpol" 2>&1)"
	validation_status=$?
	set -e
	printf '%s\n' "$validation_output"
	[ "$validation_status" -eq 0 ]
	echo "OK: the app holds scene_tool, the canonical scenes and the guide"
}

"${STEP//-/_}"
