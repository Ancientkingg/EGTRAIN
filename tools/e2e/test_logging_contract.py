#!/usr/bin/env python3
"""Static check of the console output statements in the application source.

The test counts the statements that write the console (std::cout, std::cerr, std::clog, printf,
fprintf to stdout or stderr, and the loggers owl and eglogger) in every C++ file under
EGTRAIN/QEGTRAIN, apart from the tests and the files in EXEMPT_FILES. It compares the count of
each file and kind with ALLOWED, in both directions, so a new console write fails the test.
The test also checks that the source still holds the producers of the stdout lines that scripts
read (PRODUCERS).

To lower an entry of ALLOWED after a statement is removed, change the number to the one that the
failing test reports, and delete a kind at zero and a file with no kind left. To add a deliberate
console write, put the file in EXEMPT_FILES with the reason. A test marker (a format string that
starts with E2E_ or QEGTRAIN_) is never counted. A marker helper that prints a name built by its
caller belongs in PLAIN_MARKER_FILES.

Comments and string contents are removed before counting (clean_cpp_code of
tools/memory/ownership_inventory.py), so a statement in a comment or in a string is not counted.
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SOURCE_ROOT = ROOT / "EGTRAIN/QEGTRAIN"

# Importing the module must not leave a __pycache__ folder in the source tree.
sys.dont_write_bytecode = True
sys.path.insert(0, str(ROOT / "tools/memory"))
from ownership_inventory import clean_cpp_code, source_files  # noqa: E402

# One regular expression per kind, matched on the cleaned text. std::cout.rdbuf(...) and
# std::cout.flush() are not writes; std::cout.write(...) is. snprintf and sprintf_s do not
# match printf. The kinds owl and eglogger are the two loggers that write nothing.
# Known gap: a stream passed as a conditional expression (`ok ? stdout : stderr`) does not match
# the fprintf pattern, so such a statement is not counted, marker or not. The same holds for
# fputs, fwrite, perror and Qt calls, which the code base does not use for console output.
KINDS = {
    "cout": re.compile(r"\bcout\s*(?:<<|\.\s*(?:write|put)\s*\()"),
    "cerr": re.compile(r"\bcerr\s*(?:<<|\.\s*(?:write|put)\s*\()"),
    "clog": re.compile(r"\bclog\s*(?:<<|\.\s*(?:write|put)\s*\()"),
    "printf": re.compile(r"(?<!\w)(?:printf|printf_s|vprintf|puts|putchar)\s*\("),
    "fprintf": re.compile(r"\b(?:fprintf|vfprintf)\s*\(\s*(?:std::)?(?:stdout|stderr)\b"),
    "owl": re.compile(r"\bowl\s*<<"),
    "eglogger": re.compile(r"\beglogger\s*<<"),
}

# Test markers are read by scripts. Both patterns are matched on the original text, right after
# the stream of an fprintf statement, because the cleaned text has the format string blanked.
# A prefix marker starts with E2E_ or QEGTRAIN_ (after an optional newline). The format can be a
# conditional expression of two such strings.
MARKER_START = r'"(?:\\n)?(?:E2E_|QEGTRAIN_)'
PREFIX_MARKER = re.compile(r'\s*,\s*(?:[^?;",]*\?\s*' + MARKER_START + r'(?:[^"\\]|\\.)*"\s*:\s*)?' + MARKER_START)
# A plain marker prints a name that the caller built: the format is exactly %s\n or %s=%s\n.
PLAIN_MARKER = re.compile(r'\s*,\s*"%s(?:=%s)?\\n"')

# Files where a plain marker is not counted, with the reason. Elsewhere
# fprintf(stderr, "%s\n", message) is an ordinary diagnostic.
PLAIN_MARKER_FILES = {
    "app/MainWindow.cpp": "its marker helpers print a name that the caller built",
}

# Files whose console writes are deliberate, with the reason. They are not counted.
EXEMPT_FILES = {
    "app/main.cpp": "the questions of interactive mode, usage text, startup errors and the banner lines the launch test reads",
    "scene/SceneTool.cpp": "the scene_tool command line",
    "update/UpdateHelper.cpp": "the separate update helper executable",
    "util/Log.cpp": "the default sink, the one place that writes the console for the logging core",
}

# Each entry is the number of console write statements that a file still holds. The test fails
# when a count differs in either direction, so the list can only shrink, and a file with no entry
# holds none. The kinds owl and eglogger are the two loggers that write nothing.
ALLOWED = {
    "app/DispatchController.cpp": {"cout": 9, "cerr": 1, "owl": 2, "eglogger": 2},
    "app/MainWindow.cpp": {"cout": 2},
    "diagrams/RunResults.cpp": {"cerr": 1},
    "io/RailMLParser.cpp": {"cout": 11, "cerr": 2},
    "simulation/InitialParameters.cpp": {"cout": 2},
    "simulation/RollingStock.cpp": {"cout": 3, "cerr": 1, "owl": 1},
    "simulation/RollingStock.h": {"cout": 15, "fprintf": 1, "eglogger": 2},
    "simulation/Signalling.cpp": {"cout": 26, "cerr": 1, "owl": 7, "eglogger": 5},
    "simulation/Simulation.cpp": {"cout": 2},
    "util/Logger.hpp": {"cout": 2},
}

# Stdout lines that other programs read: (place under EGTRAIN/QEGTRAIN, text, who reads it).
# Some source file under the place holds the text on a line that has a cout statement. The place
# is a file or a directory; a directory lets the statement move inside it.
PRODUCERS = [
    (
        "app",
        'End of Simulation"',
        "headless_scene_smoke.py, lebanon_scene_smoke.sh, package_start_smoke.py and the Linux package job of release.yml",
    ),
    ("app/main.cpp", '"Graphical user interface (GUI): ', "tools/e2e/startup_launch_contract.py"),
    ("simulation", '" is stopping at "', "tools/e2e/station_departure_smoke.py"),
]

COUT_STATEMENT = re.compile(r"\bcout\s*<<")


def count_statements(text, plain_markers=False):
    """Return {kind: count} for the console writes in the original text of one file.

    Kinds with no statement are left out. A plain marker is not counted when plain_markers is set.
    """
    cleaned = clean_cpp_code(text)
    counts = {}
    for kind, pattern in KINDS.items():
        found = 0
        for match in pattern.finditer(cleaned):
            if kind == "fprintf":
                if PREFIX_MARKER.match(text, match.end()):
                    continue
                if plain_markers and PLAIN_MARKER.match(text, match.end()):
                    continue
            found += 1
        if found:
            counts[kind] = found
    return counts


def has_producer(text, needle):
    """Tell whether a line of the original text holds needle and a live cout statement."""
    original_lines = text.split("\n")
    cleaned_lines = clean_cpp_code(text).split("\n")
    return any(
        needle in original and COUT_STATEMENT.search(cleaned)
        for original, cleaned in zip(original_lines, cleaned_lines)
    )


# (what the case shows, fixture text, plain_markers, expected result of count_statements)
COUNT_CASES = [
    ("a line comment is not counted", '// cout << "x";\n', False, {}),
    ("a block comment is not counted", '/* cout << 1;\n cerr << 2; */\n', False, {}),
    ("a string literal is not counted", 'const char* s = "cout << x; owl << y;";\n', False, {}),
    ("cout without a space is counted", "cout<<1;\n", False, {"cout": 1}),
    ("two statements are two", "cout << 1; std::cout << 2;\n", False, {"cout": 2}),
    ("cerr is counted", 'std::cerr << "x";\n', False, {"cerr": 1}),
    ("clog is counted", 'std::clog << "x";\n', False, {"clog": 1}),
    ("std::printf is counted", 'std::printf("x");\n', False, {"printf": 1}),
    ("owl is counted", 'owl << "x";\n', False, {"owl": 1}),
    ("eglogger is counted", 'eglogger << "x";\n', False, {"eglogger": 1}),
    ("snprintf and sprintf_s are not counted", 'snprintf(b, 3, "x"); sprintf_s(b, "x");\n', False, {}),
    ("cout.write is counted", 'std::cout.write("x", 1);\n', False, {"cout": 1}),
    ("cout.rdbuf is not counted", "std::cout.rdbuf(p);\n", False, {}),
    ("cout.flush is not counted", "std::cout.flush();\n", False, {}),
    ("an E2E_ marker is not counted", r'fprintf(stdout, "E2E_X\n");' + "\n", False, {}),
    ("a QEGTRAIN_ marker is not counted", r'fprintf(stderr, "QEGTRAIN_X\n");' + "\n", False, {}),
    ("a marker after a newline is not counted", r'std::fprintf(stdout, "\nE2E_X %d\n", 1);' + "\n", False, {}),
    ("a diagnostic is counted", r'fprintf(stderr, "problem\n");' + "\n", False, {"fprintf": 1}),
    ("a plain marker is counted by default", r'fprintf(stdout, "%s\n", name);' + "\n", False, {"fprintf": 1}),
    ("a plain marker is not counted in a plain marker file", r'fprintf(stdout, "%s\n", name);' + "\n", True, {}),
    ("a two-part plain marker is not counted in a plain marker file", r'fprintf(stdout, "%s=%s\n", a, b);' + "\n", True, {}),
    ("a longer format is counted in a plain marker file", r'fprintf(stdout, "%s\n extra", name);' + "\n", True, {"fprintf": 1}),
    ("a conditional format of two markers is not counted", r'fprintf(stdout, ok ? "E2E_A\n" : "\nQEGTRAIN_B\n");' + "\n", False, {}),
    ("a conditional format with a diagnostic is counted", r'fprintf(stdout, ok ? "E2E_A\n" : "problem\n");' + "\n", False, {"fprintf": 1}),
    ("a conditional stream is not counted", r'fprintf(ok ? stdout : stderr, "problem\n");' + "\n", False, {}),
]


def check_patterns():
    """Return the failed self-checks, run on fixture text before the scan."""
    failures = []
    for what, text, plain_markers, expected in COUNT_CASES:
        got = count_statements(text, plain_markers)
        if got != expected:
            failures.append(f"self-check: {what}: expected {expected}, got {got}")
    live = 'std::cout << "\\n End of Simulation";\n'
    if not has_producer(live, 'End of Simulation"'):
        failures.append("self-check: a live producer is not found")
    for what, text in (
        ("a line comment", '// ' + live),
        ("a block comment", '/* ' + live + ' */\n'),
        ("a line without a cout statement", 'log("\\n End of Simulation");\n'),
    ):
        if has_producer(text, 'End of Simulation"'):
            failures.append(f"self-check: {what} satisfies the producer check")
    return failures


def read_sources():
    """Return {path relative to EGTRAIN/QEGTRAIN: text} for the files outside tests/."""
    sources = {}
    for path in source_files(ROOT):
        name = path.relative_to(SOURCE_ROOT).as_posix()
        if not name.startswith("tests/"):
            sources[name] = path.read_text(encoding="utf-8", errors="replace")
    return sources


def check_counts(sources):
    """Return the differences between the scan and ALLOWED, and the scan itself."""
    problems = []
    for table, label in ((ALLOWED, "ALLOWED"), (EXEMPT_FILES, "EXEMPT_FILES"), (PLAIN_MARKER_FILES, "PLAIN_MARKER_FILES")):
        for name in sorted(table):
            if name not in sources:
                problems.append(f"{name}: listed in {label}, but no scanned file has this path")
    found = {}
    for name, text in sources.items():
        if name not in EXEMPT_FILES:
            found[name] = count_statements(text, plain_markers=name in PLAIN_MARKER_FILES)
    for name in sorted(found):
        have = found[name]
        want = ALLOWED.get(name, {})
        for kind in sorted(set(want) - set(KINDS)):
            problems.append(f"{name}: ALLOWED has the unknown kind {kind}")
        for kind in KINDS:
            now = have.get(kind, 0)
            listed = want.get(kind, 0)
            if now > listed:
                problems.append(
                    f"{name}: {now} {kind} statements, ALLOWED has {listed}. Remove the new statement."
                )
            elif now < listed:
                problems.append(
                    f"{name}: {now} {kind} statements, ALLOWED has {listed}. "
                    f"Lower the entry to {now} and delete an entry at zero."
                )
    return problems, found


def check_producers(sources):
    problems = []
    for place, needle, readers in PRODUCERS:
        files = [text for name, text in sources.items() if name == place or name.startswith(place + "/")]
        if not any(has_producer(text, needle) for text in files):
            problems.append(f"{place}: no cout statement with the text {needle} (read by {readers})")
    return problems


def main():
    problems = check_patterns()
    if problems:
        print("\n".join(problems), file=sys.stderr)
        return 1
    sources = read_sources()
    problems, found = check_counts(sources)
    problems += check_producers(sources)
    if problems:
        print("\n".join(problems), file=sys.stderr)
        return 1
    statements = sum(sum(counts.values()) for counts in found.values())
    print(f"logging contract: {statements} console write statements in {len(ALLOWED)} files, {len(PRODUCERS)} producers found")
    return 0


if __name__ == "__main__":
    sys.exit(main())
