#!/usr/bin/env python3
import subprocess
import sys
from pathlib import Path
import os

ROOT = Path(__file__).resolve().parents[2]

def find_executable():
    # Attempt macOS app bundle path first
    app_mac = ROOT / "build/QEGTRAIN.app/Contents/MacOS/QEGTRAIN"
    if app_mac.exists():
        return app_mac

    # Attempt standard build dir
    app_std = ROOT / "build/QEGTRAIN"
    if app_std.exists():
        return app_std

    # Attempt to search in build/
    build_dir = ROOT / "build"
    if build_dir.exists():
        for root, dirs, files in os.walk(build_dir):
            if "QEGTRAIN" in files:
                return Path(root) / "QEGTRAIN"
    return None

def main():
    app = find_executable()
    if not app:
        print("FAIL: QEGTRAIN executable not found.")
        sys.exit(1)

    # 1. The loose files must not exist next to the executable
    loose_files = ["train_station.png", "pax_icon.png", "window_icon.jpg"]
    app_dir = app.parent
    for lf in loose_files:
        p = app_dir / lf
        if p.exists():
            print(f"FAIL: Loose image asset found: {p}")
            sys.exit(1)

    # 2. The resource strings must be present in the binary
    proc = subprocess.run(["strings", str(app)], stdout=subprocess.PIPE, text=True, errors="replace")
    strings_out = proc.stdout

    expected_resources = [":/icons/station.png", ":/icons/passenger.png", ":/app/window.jpg"]
    for res in expected_resources:
        if res not in strings_out:
            print(f"FAIL: Resource path '{res}' not found in binary.")
            sys.exit(1)

    print("PASS: Qt resources are embedded and loose files are gone.")

if __name__ == "__main__":
    main()
