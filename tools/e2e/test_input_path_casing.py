#!/usr/bin/env python3
"""Check built-in input directory names used by the case-study loaders."""

from pathlib import Path
import sys


ROOT = Path(__file__).resolve().parents[2]
INPUT_ROOT = ROOT / "EGTRAIN" / "QEGTRAIN" / "Input"
EXPECTED_NAMES = {"tracklines": "TrackLines", "timetable": "TimeTable"}


def main() -> int:
    offending = []
    for case_dir in sorted(INPUT_ROOT.glob("Input_EGTRAIN_*")):
        if case_dir.is_symlink() or not case_dir.is_dir():
            continue
        for child in sorted(case_dir.iterdir()):
            expected = EXPECTED_NAMES.get(child.name.casefold())
            if expected is not None and child.name != expected:
                offending.append(child.relative_to(INPUT_ROOT).as_posix())

    if offending:
        print("input path casing check failed:", file=sys.stderr)
        for path in offending:
            print(path, file=sys.stderr)
        return 1

    print("input path casing check passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
