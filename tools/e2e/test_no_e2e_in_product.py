#!/usr/bin/env python3
"""Static check that the application source holds no scripted GUI run.

A file that QEGTRAIN compiles without EGTRAIN_BUILD_TESTS must not hold a variable QEGTRAIN_E2E_*, a marker E2E_* or an
assertion `failures <<`. The scripted runs live in EGTRAIN/QEGTRAIN/tests/e2e, and the window reaches them through
MainWindowTestHooks. The test scans every C++ source, header and form under EGTRAIN/QEGTRAIN except tests/ and the
files in TEST_ONLY_FILES, which only a test configuration compiles.

The files in ALLOWED only test the prefix of the variables: a run with any QEGTRAIN_E2E_ variable is a scripted run.
Each entry lists the text that the file may hold; everything else in the file is scanned as usual. To allow a new
use, add the text to the file here with the reason.

Comments are scanned too: a comment that names a scripted variable is a reference to code that does not belong in
the product.

The test also checks the other way: every marker E2E_* that a script under tools/ waits for is printed by a scripted
run, in tests/e2e or in app/. The names are typed twice, in the script and in the C++, and a misspelt one otherwise
shows only as a smoke test that fails without saying why. The test_*.py files are not scanned: their markers are
fixtures.
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SOURCE_ROOT = ROOT / "EGTRAIN/QEGTRAIN"
SCRIPT_ROOT = ROOT / "tools"

MARKERS = ("QEGTRAIN_E2E_", "E2E_", "failures <<")

SUFFIXES = {".c", ".cc", ".cpp", ".cxx", ".h", ".hpp", ".mm", ".ui", ".qrc", ".qss"}

# Compiled only in a test configuration. The isolated telemetry smoke requires EGTRAIN_BUILD_TESTS.
TEST_ONLY_FILES = {
    "app/TelemetrySmokeSupport.cpp",
    "app/TelemetrySmokeSupport.h",
}

PREFIX = '"QEGTRAIN_E2E_"'
ALLOWED = {
    "update/UpdateSettings.cpp": (PREFIX,),
    "telemetry/TelemetryConsent.cpp": (PREFIX,),
    "app/OpenRequestRelay.cpp": ('"QEGTRAIN_E2E_OPEN_RELAY"', PREFIX),
    "app/OpenRequestRelay.h": ("QEGTRAIN_E2E_OPEN_RELAY", "QEGTRAIN_E2E_*"),
}

# The folders below SOURCE_ROOT whose C++ prints the markers that the scripts wait for.
MARKER_SOURCES = ("tests/e2e", "app")
MARKER_NAME = re.compile(r"(?<![A-Za-z0-9_])E2E_[A-Z0-9_]+")
# A marker that the C++ composes from a format, such as "E2E_VISUAL_POLISH_DPR_%0.1f": the part before the % sign.
COMPOSED_PREFIX = re.compile(r"(E2E_[A-Z0-9_]*)%")


def product_files(source_root):
    """Return {path relative to source_root: text} for the files that a build without tests compiles."""
    files = {}
    for path in sorted(source_root.rglob("*")):
        relative = path.relative_to(source_root).as_posix()
        if path.suffix not in SUFFIXES or relative.startswith("tests/") or relative in TEST_ONLY_FILES:
            continue
        files[relative] = path.read_text(encoding="utf-8", errors="replace")
    return files


def find_markers(relative, text):
    """Return one message for each line of text that holds a marker which the file may not hold."""
    for allowed in sorted(ALLOWED.get(relative, ()), key=len, reverse=True):
        text = text.replace(allowed, "")
    problems = []
    for number, line in enumerate(text.splitlines(), 1):
        for marker in MARKERS:
            if marker in line:
                problems.append(f"{relative}:{number}: {marker!r} in a file that is compiled without tests")
                break
    return problems


def printed_markers(texts):
    """Return the marker names and the prefixes of the composed markers in the C++ texts."""
    names, prefixes = set(), set()
    for text in texts:
        # A format string such as "\nE2E_GUI_RUN_RESULTS rows=%d" has the marker after an escape.
        text = re.sub(r"\\[nt]", " ", text)
        names.update(MARKER_NAME.findall(text))
        prefixes.update(COMPOSED_PREFIX.findall(text))
    return names, prefixes


def unprinted_markers(relative, text, names, prefixes):
    """Return one message for each marker in a script that no scripted run prints.

    A script may wait for the start of a marker, as the pattern E2E_FOLLOW_NO_(RUN|SERVICES)_OK does, or for a composed
    marker with a number, as E2E_VISUAL_POLISH_DPR_2.0 is to the format E2E_VISUAL_POLISH_DPR_%0.1f.
    """
    problems = []
    for number, line in enumerate(text.splitlines(), 1):
        for name in MARKER_NAME.findall(line):
            if any(printed.startswith(name) for printed in names):
                continue
            if any(name.startswith(prefix) and name[len(prefix):].isdigit() for prefix in prefixes):
                continue
            problems.append(f"{relative}:{number}: no scripted run prints {name}")
    return problems


def check_patterns():
    """Return the failed self-checks, run on fixture text before the scan."""
    failures = []
    for what, relative, text, expected in (
        ("a variable is found", "app/MainWindow.cpp", 'qEnvironmentVariable("QEGTRAIN_E2E_X");', 1),
        ("a marker is found", "app/MainWindow.cpp", 'fprintf(stdout, "E2E_X\\n");', 1),
        ("an assertion is found", "app/MainWindow.cpp", "failures << message;", 1),
        ("a marker in a comment is found", "app/MainWindow.cpp", "// prints E2E_X", 1),
        ("two lines are two problems", "app/MainWindow.cpp", "E2E_A\nE2E_B\n", 2),
        ("a test configuration variable is not a marker", "app/MainWindow.cpp", "QEGTRAIN_STARTUP_TIMING", 0),
        ("the prefix test is allowed", "update/UpdateSettings.cpp", 'key.startsWith(QStringLiteral("QEGTRAIN_E2E_"))', 0),
        ("another variable is not allowed", "update/UpdateSettings.cpp", 'QStringLiteral("QEGTRAIN_E2E_X")', 1),
        ("the relay variable is allowed", "app/OpenRequestRelay.cpp", 'contains(QStringLiteral("QEGTRAIN_E2E_OPEN_RELAY"))', 0),
        ("the relay variable is not allowed elsewhere", "app/main.cpp", 'contains("QEGTRAIN_E2E_OPEN_RELAY")', 1),
        ("an assertion is not allowed in an allowed file", "app/OpenRequestRelay.cpp", "failures << message;", 1),
    ):
        got = len(find_markers(relative, text))
        if got != expected:
            failures.append(f"self-check: {what}: expected {expected} problems, got {got}")
    names, prefixes = printed_markers(['fprintf(stdout, "\\nE2E_A_OK rows=%d\\n");', 'QString("E2E_DPR_%1")'])
    for what, text, expected in (
        ("a printed marker is found", 'grep -q "E2E_A_OK" "$LOG"', 0),
        ("a misspelt marker is not", 'grep -q "E2E_B_OK" "$LOG"', 1),
        ("the start of a printed marker is found", 'grep -Eq "E2E_A_(OK|FAILED)" "$LOG"', 0),
        ("a composed marker with a number is found", 'grep -q "E2E_DPR_2.0" "$LOG"', 0),
        ("a composed marker without a number is not", 'grep -q "E2E_DPR_X" "$LOG"', 1),
        ("a variable is not a marker", 'QEGTRAIN_E2E_SCENE="$SCENE"', 0),
    ):
        got = len(unprinted_markers("tools/e2e/x_smoke.sh", text, names, prefixes))
        if got != expected:
            failures.append(f"self-check: {what}: expected {expected} problems, got {got}")
    return failures


def marker_files():
    """Return {path relative to ROOT: text} for the scripts below SCRIPT_ROOT and the C++ that prints the markers."""
    scripts, sources = {}, {}
    for path in sorted(SCRIPT_ROOT.rglob("*")):
        if path.suffix in {".sh", ".py"} and not path.name.startswith("test_"):
            scripts[path.relative_to(ROOT).as_posix()] = path.read_text(encoding="utf-8", errors="replace")
    for folder in MARKER_SOURCES:
        for path in sorted((SOURCE_ROOT / folder).rglob("*")):
            if path.suffix in {".cpp", ".h", ".mm"}:
                sources[path.relative_to(ROOT).as_posix()] = path.read_text(encoding="utf-8", errors="replace")
    return scripts, sources


def main():
    problems = check_patterns()
    if problems:
        print("\n".join(problems), file=sys.stderr)
        return 1
    files = product_files(SOURCE_ROOT)
    for relative in ("app/MainWindow.cpp", "app/main.cpp", *ALLOWED):
        if relative not in files:
            problems.append(f"{relative} was not scanned")
    for relative, text in files.items():
        problems += find_markers(relative, text)
    scripts, sources = marker_files()
    for relative in ("tools/e2e/visual_polish_smoke.sh", "tools/e2e/gui_autostart_smoke.py"):
        if relative not in scripts:
            problems.append(f"{relative} was not scanned")
    names, prefixes = printed_markers(sources.values())
    for relative, text in scripts.items():
        problems += unprinted_markers(relative, text, names, prefixes)
    if problems:
        print("\n".join(problems), file=sys.stderr)
        return 1
    print(f"no scripted GUI run in {len(files)} product files, and every marker of {len(scripts)} scripts is printed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
