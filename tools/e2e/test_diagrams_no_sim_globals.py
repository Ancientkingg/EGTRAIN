#!/usr/bin/env python3
"""Static check that the result windows and their exports read a completed run, not the simulation.

A finished run is copied once into a CompletedRunData (diagrams/CompletedRunData.h), and every
diagram and export reads that copy. So no file under EGTRAIN/QEGTRAIN/diagrams, and not
app/MainWindowSupport.h, which writes the run summary, may include a header of simulation/ or name
a run global (regional_train, train_route, numRegions, initial_variables). The include of
diagrams/SimulationHeaders.h counts as an include of simulation/, because it wraps two of those
headers.

The window, app/MainWindow.cpp and app/MainWindow.h, may include the simulation, but it may not name
a run global either. It reads the settings of the process and the run through RunController.

The files in ALLOWED are the exceptions. CompletedRunCapture.cpp copies the run out of the
simulation, RunResults.cpp builds the result rows from the runtime trains, and RunResults.h takes a
constant from simulation/InitialParameters.h. SimulationHeaders.h is the gateway that the two
sources include.

Comments and string contents are removed before the names are searched (clean_cpp_code of
tools/memory/ownership_inventory.py), so a comment that explains the rule does not fail it. The
test also runs the detector on small samples, so that a change of the patterns cannot make the
check pass on everything.
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SOURCE_ROOT = ROOT / "EGTRAIN/QEGTRAIN"

# Importing the module must not leave a __pycache__ folder in the source tree.
sys.dont_write_bytecode = True
sys.path.insert(0, str(ROOT / "tools/memory"))
from ownership_inventory import clean_cpp_code  # noqa: E402

CHECKED_FOLDER = "diagrams"
CHECKED_FILES = ["app/MainWindowSupport.h"]
# Files that may include the simulation but may not name a run global.
GLOBAL_NAMES_ONLY_FILES = ["app/MainWindow.cpp", "app/MainWindow.h"]
ALLOWED = {
    "diagrams/CompletedRunCapture.cpp": "copies the run out of the simulation",
    "diagrams/RunResults.cpp": "builds the result rows from the runtime trains",
    "diagrams/RunResults.h": "takes kDefaultRandomSeed from simulation/InitialParameters.h",
    "diagrams/SimulationHeaders.h": "wraps the simulation headers for the two sources above",
}
SOURCE_SUFFIXES = {".cpp", ".h"}

INCLUDE_LINE = re.compile(r"^\s*#\s*include\b")
INCLUDE_PATH = re.compile(r'^\s*#\s*include\s*[<"]([^">]+)[">]')
RUN_GLOBALS = re.compile(r"\b(regional_train|train_route|numRegions|initial_variables)\b")


def violations(relative, text, check_includes=True):
    """Returns (line, message) for each include of the simulation and each run global in the text.

    Without check_includes the text may include the simulation."""
    found = []
    for number, (raw, code) in enumerate(zip(text.splitlines(), clean_cpp_code(text).splitlines()), start=1):
        if check_includes and INCLUDE_LINE.match(code):
            included = INCLUDE_PATH.match(raw)
            if included and (included.group(1).startswith("simulation/")
                             or included.group(1) == "diagrams/SimulationHeaders.h"):
                found.append((number, f"includes {included.group(1)}"))
        for name in sorted(set(RUN_GLOBALS.findall(code))):
            found.append((number, f"names the run global {name}"))
    return found


def self_test():
    samples = [
        ('#include "simulation/Signalling.h"\n', 1),
        ('#include "diagrams/SimulationHeaders.h"\n', 1),
        ("int f() { return numRegions; }\n", 1),
        ("for (const Train& t : regional_train) use(train_route, initial_variables);\n", 3),
        ('// numRegions in a comment\n/* train_route */\nconst char* s = "regional_train";\n', 0),
        ('#include "diagrams/CompletedRunData.h"\n#include <vector>\n', 0),
    ]
    for text, expected in samples:
        got = len(violations("sample.cpp", text))
        if got != expected:
            sys.exit(f"the detector found {got} violations, not {expected}, in {text!r}")
    names_only = [
        ('#include "simulation/Signalling.h"\n', 0),
        ("settings.reset(initial_variables);\n", 1),
        ("for (const Train& t : regional_train) use(t);\n", 1),
        ("// initial_variables in a comment\n", 0),
    ]
    for text, expected in names_only:
        got = len(violations("sample.cpp", text, check_includes=False))
        if got != expected:
            sys.exit(f"the names-only detector found {got} violations, not {expected}, in {text!r}")


def checked_files():
    files = sorted(
        path.relative_to(SOURCE_ROOT).as_posix()
        for path in (SOURCE_ROOT / CHECKED_FOLDER).rglob("*")
        if path.is_file() and path.suffix in SOURCE_SUFFIXES
    )
    return files + CHECKED_FILES


def main():
    self_test()
    problems = []
    files = checked_files()
    for relative in ALLOWED:
        if relative not in files:
            problems.append(f"{relative}: listed in ALLOWED but not found; remove it from the list")
    for relative in files:
        if relative in ALLOWED:
            continue
        path = SOURCE_ROOT / relative
        if not path.is_file():
            problems.append(f"{relative}: not found")
            continue
        for number, message in violations(relative, path.read_text(encoding="utf-8")):
            problems.append(f"{relative}:{number}: {message}")
    for relative in GLOBAL_NAMES_ONLY_FILES:
        path = SOURCE_ROOT / relative
        if not path.is_file():
            problems.append(f"{relative}: not found")
            continue
        for number, message in violations(relative, path.read_text(encoding="utf-8"), check_includes=False):
            problems.append(f"{relative}:{number}: {message}")
    if problems:
        print("The diagrams, the exports and the window must read the completed run or RunController, not the run globals:")
        for problem in problems:
            print("  " + problem)
        print("Read the run through CompletedRunData and the settings through RunController. Only the files in ALLOWED of this test may read the simulation.")
        return 1
    print(f"diagrams, exports and the window read no run globals ({len(files) - len(ALLOWED) + len(GLOBAL_NAMES_ONLY_FILES)} files checked)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
