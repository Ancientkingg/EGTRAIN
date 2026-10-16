#!/usr/bin/env bash
# Signs a macOS app bundle: sign_macos.sh path/to/QEGTRAIN.app
#
# Without MACOS_SIGNING_IDENTITY (unset or empty) the whole bundle gets an ad-hoc signature. macdeployqt rewrites install
# names, which breaks the signature of each file, and Apple Silicon kills a binary with a broken signature.
#
# With MACOS_SIGNING_IDENTITY set to a Developer ID Application identity from a keychain on the search list, each piece of
# nested code is signed on its own, innermost first, with the hardened runtime and a secure timestamp. Frameworks, then plug-ins,
# then the programs in Contents/MacOS, the main executable, and the bundle last, without --deep: notarization needs every
# piece signed with these options, and --deep would hide code that was missed. Mach-O code anywhere else in the bundle
# fails the script.
#
# The script reads no certificate, password or token, and prints none.
set -euo pipefail

if [ "$#" -ne 1 ] || [ ! -f "$1/Contents/Info.plist" ]; then
	echo "usage: $0 path/to/App.app" >&2
	exit 2
fi
APP="${1%/}"
CONTENTS="$APP/Contents"

if [ -z "${MACOS_SIGNING_IDENTITY:-}" ]; then
	echo "Signing ${APP##*/} ad hoc"
	codesign --force --deep --sign - "$APP"
	exit 0
fi

# The Mach-O files below the given folders. file pads its output and adds a line for each architecture of a universal file.
macho_files() {
	local described
	described="$(find "$@" -type f -exec file --mime-type {} +)"
	printf '%s\n' "$described" | sed -n -E '/ \(for architecture /d; s/:[[:space:]]+application\/x-mach-binary$//p'
}

# Paths on standard input, the deepest first and in byte order within one depth, so that a bundle follows what it holds.
deepest_first() {
	awk '{ printf "%d\t%s\n", gsub("/", "/"), $0 }' | LC_ALL=C sort -t "$(printf '\t')" -k1,1nr -k2 | cut -f2-
}

ALL="$(macho_files "$APP")"
MAIN="$CONTENTS/MacOS/$(/usr/libexec/PlistBuddy -c 'Print :CFBundleExecutable' "$CONTENTS/Info.plist")"

# Every piece of nested code in the order to sign it, one path per line.
code_in_order() {
	local folder
	for folder in Frameworks PlugIns; do
		if [ -d "$CONTENTS/$folder" ]; then
			{
				printf '%s\n' "$ALL" | awk -v prefix="$CONTENTS/$folder/" 'index($0, prefix) == 1'
				find "$CONTENTS/$folder" -type d \( -name '*.framework' -o -name '*.app' -o -name '*.xpc' -o -name '*.appex' \)
			} | deepest_first
		fi
	done
	printf '%s\n' "$ALL" | awk -v dir="$CONTENTS/MacOS/" -v main="$MAIN" \
		'index($0, dir) == 1 && index(substr($0, length(dir) + 1), "/") == 0 && $0 != main' | LC_ALL=C sort
	printf '%s\n' "$MAIN"
}

ORDER="$(code_in_order)"

missed=""
while IFS= read -r path; do
	[ -n "$path" ] || continue
	case $'\n'"$ORDER"$'\n' in
		*$'\n'"$path"$'\n'*) ;;
		*) missed="$missed ${path#"$APP"/}" ;;
	esac
done <<<"$ALL"
if [ -n "$missed" ]; then
	echo "Mach-O code outside Frameworks, PlugIns and MacOS, which this script does not sign:$missed" >&2
	exit 1
fi
[ -f "$MAIN" ] || { echo "The main executable ${MAIN#"$APP"/} is missing" >&2; exit 1; }

sign() {
	echo "Signing ${APP##*/}${1#"$APP"}"
	codesign --force --options runtime --timestamp --sign "$MACOS_SIGNING_IDENTITY" "$1"
}

while IFS= read -r path; do
	sign "$path"
done <<<"$ORDER"
sign "$APP"
