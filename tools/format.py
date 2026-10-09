#!/usr/bin/env python3
"""Checks or applies the code format of the EGTRAIN C++ sources.

  python3 tools/format.py          check; lists the files that differ and exits with 1
  python3 tools/format.py --fix    format the files in place

The sources are the .cpp, .h and .hpp files under EGTRAIN/QEGTRAIN that git tracks,
without the vendored code in io/third_party. The style is .clang-format in the
repository root. clang-format releases format some constructs differently, so the
script accepts one version only. CLANG_FORMAT names the executable when it is not
the clang-format on PATH.
"""
import argparse
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path

CLANG_FORMAT_VERSION = "23.1.3"
ROOT = Path(__file__).resolve().parents[1]
SOURCE_ROOT = "EGTRAIN/QEGTRAIN"
VENDORED = SOURCE_ROOT + "/io/third_party/"
SUFFIXES = (".cpp", ".h", ".hpp")
INSTALL_HINT = f"python3 -m pip install clang-format=={CLANG_FORMAT_VERSION}"


def source_files() -> list:
    listed = subprocess.run(
        ["git", "ls-files", "-z", "--", SOURCE_ROOT], cwd=ROOT, check=True, capture_output=True
    ).stdout.decode("utf-8")
    return sorted(
        name for name in listed.split("\0") if name.endswith(SUFFIXES) and not name.startswith(VENDORED)
    )


def find_clang_format() -> str:
    executable = os.environ.get("CLANG_FORMAT") or shutil.which("clang-format")
    if not executable:
        sys.exit(f"clang-format {CLANG_FORMAT_VERSION} is not installed. Install it with: {INSTALL_HINT}")
    try:
        reported = subprocess.run([executable, "--version"], capture_output=True, text=True, check=True).stdout
    except (OSError, subprocess.CalledProcessError) as error:
        sys.exit(f"{executable} does not run: {error}")
    match = re.search(r"version (\d+\.\d+\.\d+)", reported)
    found = match.group(1) if match else reported.strip()
    if found != CLANG_FORMAT_VERSION:
        sys.exit(
            f"{executable} is clang-format {found}, but the sources are formatted with {CLANG_FORMAT_VERSION}. "
            f"Install that version with: {INSTALL_HINT}"
        )
    return executable


def main() -> int:
    parser = argparse.ArgumentParser(description="Check or apply the code format of the C++ sources.")
    parser.add_argument("--fix", action="store_true", help="format the files in place")
    arguments = parser.parse_args()
    clang_format = find_clang_format()
    files = source_files()
    if arguments.fix:
        for start in range(0, len(files), 50):
            subprocess.run([clang_format, "-i", "--style=file", *files[start:start + 50]], cwd=ROOT, check=True)
        print(f"formatted {len(files)} files")
        return 0
    differing = []
    for name in files:
        formatted = subprocess.run(
            [clang_format, "--style=file", name], cwd=ROOT, check=True, capture_output=True
        ).stdout
        if formatted != (ROOT / name).read_bytes():
            differing.append(name)
    if differing:
        print(f"{len(differing)} of {len(files)} files are not formatted:")
        for name in differing:
            print(f"  {name}")
        print("Format them with: python3 tools/format.py --fix")
        return 1
    print(f"{len(files)} files are formatted")
    return 0


if __name__ == "__main__":
    sys.exit(main())
