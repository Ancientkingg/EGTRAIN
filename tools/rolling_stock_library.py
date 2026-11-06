#!/usr/bin/env python3
"""Writes or checks the rolling stock library of the bundled cases.

EGTRAIN/QEGTRAIN/resources/rolling_stock_library.json has the form of a rolling_stock.json without compositions. Its
train_units hold every rolling stock unit of EGTRAIN/QEGTRAIN/Scenes/*/rolling_stock.json as the case stores it, and
one key more, cases, holds for each unit, in the same order, the folders of the cases that hold it. So the scene loader
reads the units, and only it names their keys. A unit that two cases hold with the same values is listed once, with
both cases. Two different units with the same id are each listed with their own cases. The cases are read in the order
of their folder names, and the units of a case in the order of its file.

  python3 tools/rolling_stock_library.py          writes the file again from the cases
  python3 tools/rolling_stock_library.py --check  exits with 1 when the file differs from the cases

--scenes and --library name other folders and files, for a copy of the cases.
"""

import argparse
import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SCENES = ROOT / "EGTRAIN/QEGTRAIN/Scenes"
LIBRARY = ROOT / "EGTRAIN/QEGTRAIN/resources/rolling_stock_library.json"


def build(scenes):
    entries = []
    cases = sorted((folder for folder in scenes.iterdir() if (folder / "rolling_stock.json").is_file()),
                   key=lambda folder: folder.name)
    for case in cases:
        rolling_stock = json.loads((case / "rolling_stock.json").read_text(encoding="utf-8"))
        for unit in rolling_stock["train_units"]:
            same = next((entry for entry in entries if entry["unit"] == unit), None)
            if same:
                same["cases"].append(case.name)
            else:
                entries.append({"cases": [case.name], "unit": unit})
    return {
        "cases": [entry["cases"] for entry in entries],
        "compositions": [],
        "train_units": [entry["unit"] for entry in entries],
    }


def text(library):
    return json.dumps(library, indent=4, sort_keys=True, ensure_ascii=False) + "\n"


def pairs(library):
    """The units of a library with their cases, as (unit, cases) pairs."""
    if not isinstance(library, dict) or not isinstance(library.get("train_units"), list):
        return []
    cases = library.get("cases") if isinstance(library.get("cases"), list) else []
    return [(unit, cases[index] if index < len(cases) else None) for index, unit in enumerate(library["train_units"])]


def describe(pair):
    unit, cases = pair
    name = unit.get("id") if isinstance(unit, dict) else repr(unit)
    return "{} ({})".format(name, ", ".join(str(case) for case in cases) if isinstance(cases, list) else "no cases")


def main():
    parser = argparse.ArgumentParser(description="Write or check the rolling stock library of the bundled cases.")
    parser.add_argument("--check", action="store_true", help="exit with 1 when the file differs from the cases")
    parser.add_argument("--scenes", type=Path, default=SCENES, help="the folder of the cases")
    parser.add_argument("--library", type=Path, default=LIBRARY, help="the library file")
    arguments = parser.parse_args()

    expected = build(arguments.scenes)
    if not arguments.check:
        arguments.library.write_text(text(expected), encoding="utf-8", newline="\n")
        print("wrote {} units to {}".format(len(expected["train_units"]), arguments.library))
        return 0

    actual = json.loads(arguments.library.read_text(encoding="utf-8")) if arguments.library.is_file() else {}
    if actual == expected:
        print("{} matches the {} units of the cases".format(arguments.library.name, len(expected["train_units"])))
        return 0
    expected_units = pairs(expected)
    actual_units = pairs(actual)
    for pair in expected_units:
        if pair not in actual_units:
            print("in the cases, not in the library: " + describe(pair))
    for pair in actual_units:
        if pair not in expected_units:
            print("in the library, not in the cases: " + describe(pair))
    print("{} does not match the bundled cases. Run python3 tools/rolling_stock_library.py to write it again."
          .format(arguments.library))
    return 1


if __name__ == "__main__":
    sys.exit(main())
