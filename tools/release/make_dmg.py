#!/usr/bin/env python3
"""Build the macOS disk image of a release from a finished QEGTRAIN.app.

usage: make_dmg.py --version X.Y.Z --app PATH/QEGTRAIN.app --output DIR [--layout]

The image is DIR/EGTRAIN-X.Y.Z-macOS-arm64.dmg, a name that the application accepts
for its macos-arm64 package (kAssetPatterns in update/ReleaseInfo.cpp). It is a
compressed, read-only (UDZO) HFS+ image with the volume name EGTRAIN. The volume holds
exactly two entries: QEGTRAIN.app and Applications, a symbolic link to /Applications
to drag the app onto. The bundle is copied with ditto, as the package job does, and
never changed afterwards, so a signed app keeps a valid signature.

With --layout the volume also holds the hidden file .DS_Store, a copy of the committed
dmg/DS_Store. It tells Finder to show the volume as an icon view of 600 by 360 points
with large icons, without toolbar, sidebar, path bar and status bar, QEGTRAIN.app on
the left and Applications on the right at the same height. dmg/make_ds_store.py writes
that file. Without --layout the volume has the placement that Finder chooses itself.

Every input is checked before anything is created:

- The version has the form X.Y.Z that version.py accepts.
- --app is a directory named QEGTRAIN.app that holds Contents/Info.plist and the
  directory Contents/MacOS.
- With --layout, dmg/DS_Store is a file.
- DIR is not a file, and DIR/EGTRAIN-X.Y.Z-macOS-arm64.dmg does not exist. The script
  never overwrites a file.
- ditto and hdiutil are on the path. Both are macOS tools, so the script runs only on
  macOS.

Only then is DIR created, if it does not exist. The script stages the app, the link and,
with --layout, .DS_Store in a temporary folder, which is removed on every exit, and runs
hdiutil on it. Each tool gets 120 seconds, and the exit status alone decides whether it
worked: hdiutil prints deprecation notices on standard error and still exits with 0. When
hdiutil fails or times out, the script removes the partly written image and exits with
the command and its error text.

Two images made from the same bundle have the same name, volume name and contents. The
script does not promise equal image bytes.

The script sets no background picture, volume icon or licence text, and it does not
sign, notarize or upload anything.
"""
import argparse
import os
import shutil
import subprocess
import tempfile
from pathlib import Path

from build_release_assets import check_version

APP_NAME = "QEGTRAIN.app"
LINK_NAME = "Applications"
VOLUME_NAME = "EGTRAIN"
LAYOUT_FILE = Path(__file__).resolve().parent / "dmg" / "DS_Store"
TOOL_TIMEOUT_SECONDS = 120


def dmg_name(version: str) -> str:
    return f"EGTRAIN-{version}-macOS-arm64.dmg"


def check_app(app) -> None:
    """The path is a QEGTRAIN.app directory with an Info.plist and a MacOS directory."""
    app = Path(app)
    if app.name != APP_NAME or not app.is_dir():
        raise SystemExit(f"{app} is not a directory named {APP_NAME}")
    if not (app / "Contents" / "Info.plist").is_file():
        raise SystemExit(f"{app} has no Contents/Info.plist")
    if not (app / "Contents" / "MacOS").is_dir():
        raise SystemExit(f"{app} has no Contents/MacOS directory")


def run(command) -> None:
    command = [str(part) for part in command]
    shown = " ".join(command)
    try:
        result = subprocess.run(command, capture_output=True, text=True, timeout=TOOL_TIMEOUT_SECONDS)
    except subprocess.TimeoutExpired:
        raise SystemExit(f"{shown} did not finish in {TOOL_TIMEOUT_SECONDS} seconds")
    if result.returncode != 0:
        raise SystemExit(f"{shown} exited with {result.returncode}\n{(result.stderr or result.stdout).strip()}")


def build_dmg(app, output, version: str, layout: bool = False) -> Path:
    """Writes the disk image of the app into the output directory and returns its path. With layout, the volume
    also holds the Finder layout file."""
    app, output = Path(app), Path(output)
    check_version(version)
    check_app(app)
    if layout and not LAYOUT_FILE.is_file():
        raise SystemExit(f"{LAYOUT_FILE} is not a file")
    target = output / dmg_name(version)
    if output.exists() and not output.is_dir():
        raise SystemExit(f"{output} exists and is not a directory")
    if os.path.lexists(target):
        raise SystemExit(f"{target} exists, the script does not overwrite it")
    tools = []
    for name in ("ditto", "hdiutil"):
        path = shutil.which(name)
        if path is None:
            raise SystemExit(f"{name} not found: the disk image can only be built on macOS")
        tools.append(path)
    ditto, hdiutil = tools

    output.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory() as folder:
        stage = Path(folder)
        run([ditto, app, stage / APP_NAME])
        os.symlink("/Applications", stage / LINK_NAME)
        if layout:
            shutil.copyfile(LAYOUT_FILE, stage / ".DS_Store")
        try:
            run([hdiutil, "create", "-volname", VOLUME_NAME, "-srcfolder", stage, "-fs", "HFS+", "-format", "UDZO", target])
        except SystemExit:
            # The target did not exist before this run, so it can only be the partial image.
            target.unlink(missing_ok=True)
            raise
    return target


def main(argv=None) -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--version", required=True)
    parser.add_argument("--app", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--layout", action="store_true", help="put the committed Finder layout on the image")
    args = parser.parse_args(argv)
    target = build_dmg(args.app, args.output, args.version, args.layout)
    print(f"{target.name}  {target.stat().st_size} bytes")
    print(f"OK: {target}")


if __name__ == "__main__":
    main()
