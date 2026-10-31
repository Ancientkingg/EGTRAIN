#!/usr/bin/env python3
"""Static check of the console output statements in the application source.

The test counts the statements that write the console (std::cout, std::cerr, std::clog, printf,
fprintf to stdout or stderr) in every C++ file under EGTRAIN/QEGTRAIN, apart from the tests and
the files in EXEMPT_FILES. It compares the count of each file and kind with ALLOWED, in both
directions, so a new console write fails the test.
The test also checks that the source still holds the producers of the stdout lines that scripts
read (PRODUCERS).

To lower an entry of ALLOWED after a statement is removed, change the number to the one that the
failing test reports, and delete a kind at zero and a file with no kind left. A console write
that must stay, such as a line that a script reads or a message of the command line, is counted
in ALLOWED and named in the comment above its entry. A file that exists to write the console
goes in EXEMPT_FILES with the reason.

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
# match printf.
# Known gap: a stream passed as a conditional expression (`ok ? stdout : stderr`) does not match
# the fprintf pattern, so such a statement is not counted. The same holds for fputs, fwrite,
# perror and Qt calls, which the code base does not use for console output.
KINDS = {
    "cout": re.compile(r"\bcout\s*(?:<<|\.\s*(?:write|put)\s*\()"),
    "cerr": re.compile(r"\bcerr\s*(?:<<|\.\s*(?:write|put)\s*\()"),
    "clog": re.compile(r"\bclog\s*(?:<<|\.\s*(?:write|put)\s*\()"),
    "printf": re.compile(r"(?<!\w)(?:printf|printf_s|vprintf|puts|putchar)\s*\("),
    "fprintf": re.compile(r"\b(?:fprintf|vfprintf)\s*\(\s*(?:std::)?(?:stdout|stderr)\b"),
}

# Files whose console writes are deliberate, with the reason. They are not counted.
EXEMPT_FILES = {
    "scene/SceneTool.cpp": "the scene_tool command line",
    "update/UpdateHelper.cpp": "the separate update helper executable",
    "util/Log.cpp": "the default sink, the one place that writes the console for the logging core",
    "widgets/ConsoleWidget.cpp": "the sink of the application, which writes the console when no console dock is installed",
}

# Each entry is the number of console write statements that a file still holds. The test fails
# when a count differs in either direction, so the list can only shrink, and a file with no entry
# holds none.
ALLOWED = {
    # The command line of the application, which prints what a user or a script reads before the
    # logging core takes over, and the lines that a run without a window prints itself:
    #   cout: the three questions of --interactive, the
    #         lines "Graphical user interface (GUI): 0" and "1" that startup_launch_contract.py
    #         reads, the two statements of the --help text, and the line "End of Simulation" that
    #         scripts read.
    #   cerr: the errors of the options, the error of an unknown option, the
    #         diagnostics of a scene, the endpoint of the live stream and the problems that
    #         installConsoleLogging returns.
    "app/main.cpp": {"cout": 8, "cerr": 14},
    # Marker lines of the profiling switches and of scripted runs. Each starts with a fixed word,
    # so no logging rule may hide it and no level or category may come in front of it.
    #   QEGTRAIN_NATIVE_DETAIL: the stage times of the preparation of a scene, written with
    #   QEGTRAIN_STARTUP_NATIVE_DETAIL=1 and QEGTRAIN_STARTUP_TIMING=1.
    "app/DispatchController.cpp": {"fprintf": 1},
    #   QEGTRAIN_PLAYBACK_PROFILE: the error record of the playback profile for a startup timing
    #   run, which tools/performance/playback_profile.py reads.
    "app/MainWindow.cpp": {"fprintf": 1},
    #   QEGTRAIN_TIMING: the startup timing record, read by startup_launch_contract.py and
    #   tools/performance/startup_benchmark.py. QEGTRAIN_TIMING_ERROR: why that timing failed.
    #   QEGTRAIN_PLAYBACK_PROFILE: the error record of the playback profile for a scenario that
    #   the scene does not have, which tools/performance/playback_profile.py reads.
    "app/StartupTiming.cpp": {"fprintf": 3},
    #   QEGTRAIN_PLAYBACK_PROFILE: the records of the playback profile.
    "util/PlaybackProfiler.cpp": {"fprintf": 1},
    #   QEGTRAIN_DIALOG_SUPPRESSED: the message of a dialog that a scripted run suppresses.
    "widgets/UserPrompts.h": {"fprintf": 1},
    "diagrams/RunResults.cpp": {"cerr": 1},
}

# A statement that writes a line to the console: cout, or EG_LOG(Category, Level), whose default sink writes to std::cout.
PRODUCER_STATEMENT = re.compile(r"\b(?:cout|EG_LOG\s*\(\s*\w+\s*,\s*\w+\s*\))\s*<<")
# A statement that writes std::cout itself, which no logging rule can switch off.
COUT_STATEMENT = re.compile(r"\bcout\s*<<")

# Stdout lines that other programs read: (place under EGTRAIN/QEGTRAIN, text, who reads it, statement).
# Some source file under the place holds the text on a line that has a live statement of the kind
# given. The place is a file or a directory; a directory lets the statement move inside it.
PRODUCERS = [
    (
        "app/main.cpp",
        'End of Simulation"',
        "headless_scene_smoke.py, package_start_smoke.py and the package jobs of package.yml",
        COUT_STATEMENT,
    ),
    ("app/DispatchController.cpp", 'End of Simulation"', "the same line in the console of a run in the window", PRODUCER_STATEMENT),
    ("app/main.cpp", '"Graphical user interface (GUI): ', "tools/e2e/startup_launch_contract.py", PRODUCER_STATEMENT),
    (
        "simulation",
        '" is stopping at "',
        "tools/e2e/station_departure_smoke.py, which runs with EGTRAIN_LOG=sim.step=true",
        PRODUCER_STATEMENT,
    ),
]


def count_statements(text):
    """Return {kind: count} for the console writes in the original text of one file.

    Kinds with no statement are left out.
    """
    cleaned = clean_cpp_code(text)
    counts = {}
    for kind, pattern in KINDS.items():
        found = len(pattern.findall(cleaned))
        if found:
            counts[kind] = found
    return counts


def has_producer(text, needle, statement=PRODUCER_STATEMENT):
    """Tell whether a line of the original text holds needle and a live statement that matches."""
    original_lines = text.split("\n")
    cleaned_lines = clean_cpp_code(text).split("\n")
    return any(
        needle in original and statement.search(cleaned)
        for original, cleaned in zip(original_lines, cleaned_lines)
    )


# (what the case shows, fixture text, expected result of count_statements)
COUNT_CASES = [
    ("a line comment is not counted", '// cout << "x";\n', {}),
    ("a block comment is not counted", '/* cout << 1;\n cerr << 2; */\n', {}),
    ("a string literal is not counted", 'const char* s = "cout << x; cerr << y;";\n', {}),
    ("cout without a space is counted", "cout<<1;\n", {"cout": 1}),
    ("two statements are two", "cout << 1; std::cout << 2;\n", {"cout": 2}),
    ("cerr is counted", 'std::cerr << "x";\n', {"cerr": 1}),
    ("clog is counted", 'std::clog << "x";\n', {"clog": 1}),
    ("std::printf is counted", 'std::printf("x");\n', {"printf": 1}),
    ("snprintf and sprintf_s are not counted", 'snprintf(b, 3, "x"); sprintf_s(b, "x");\n', {}),
    ("cout.write is counted", 'std::cout.write("x", 1);\n', {"cout": 1}),
    ("cout.rdbuf is not counted", "std::cout.rdbuf(p);\n", {}),
    ("cout.flush is not counted", "std::cout.flush();\n", {}),
    ("a diagnostic is counted", r'fprintf(stderr, "problem\n");' + "\n", {"fprintf": 1}),
    ("a line that scripts read is counted", r'std::fprintf(stdout, "\nQEGTRAIN_X %d\n", 1);' + "\n", {"fprintf": 1}),
    ("fflush is not counted", "std::fflush(stdout);\n", {}),
    ("a conditional stream is not counted", r'fprintf(ok ? stdout : stderr, "problem\n");' + "\n", {}),
    ("cout.put is counted", "std::cout.put('x');\n", {"cout": 1}),
    ("printf_s, vprintf, puts and putchar are counted", 'printf_s("x"); vprintf(f, a); puts("x"); putchar(1);\n', {"printf": 4}),
    ("vfprintf to stderr is counted", "vfprintf(stderr, f, a);\n", {"fprintf": 1}),
    ("fprintf to a file is not counted", 'fprintf(file, "x");\n', {}),
]


def check_patterns():
    """Return the failed self-checks, run on fixture text before the scan."""
    failures = []
    for what, text, expected in COUNT_CASES:
        got = count_statements(text)
        if got != expected:
            failures.append(f"self-check: {what}: expected {expected}, got {got}")
    live = 'std::cout << "\\n End of Simulation";\n'
    if not has_producer(live, 'End of Simulation"'):
        failures.append("self-check: a live producer is not found")
    for what, text in (
        ("a line comment", '// ' + live),
        ("a block comment", '/* ' + live + ' */\n'),
        ("a line without a console statement", 'log("\\n End of Simulation");\n'),
    ):
        if has_producer(text, 'End of Simulation"'):
            failures.append(f"self-check: {what} satisfies the producer check")
    if has_producer('EG_LOG(AppRun, Info) << "\\n End of Simulation";\n', 'End of Simulation"', COUT_STATEMENT):
        failures.append("self-check: an EG_LOG line satisfies the cout producer check")
    if not has_producer(live, 'End of Simulation"', COUT_STATEMENT):
        failures.append("self-check: a live cout producer is not found")
    logged = 'EG_LOG(SimStep, Debug) << "At time " << t << " is stopping at " << name << "\\n";\n'
    if not has_producer(logged, '" is stopping at "'):
        failures.append("self-check: a live EG_LOG producer is not found")
    for what, text in (
        ("an EG_LOG line in a line comment", "// " + logged),
        ("an EG_LOG line in a block comment", "/* " + logged + " */\n"),
    ):
        if has_producer(text, '" is stopping at "'):
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
    for table, label in ((ALLOWED, "ALLOWED"), (EXEMPT_FILES, "EXEMPT_FILES")):
        for name in sorted(table):
            if name not in sources:
                problems.append(f"{name}: listed in {label}, but no scanned file has this path")
    for name in sorted(ALLOWED):
        if name in EXEMPT_FILES:
            problems.append(f"{name}: listed in ALLOWED and in EXEMPT_FILES. An exempt file is not counted: delete one entry.")
        for kind, listed in sorted(ALLOWED[name].items()):
            if listed <= 0:
                problems.append(f"{name}: ALLOWED has {listed} for {kind}. Delete an entry at zero.")
    found = {}
    for name, text in sources.items():
        if name not in EXEMPT_FILES:
            found[name] = count_statements(text)
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
    for place, needle, readers, statement in PRODUCERS:
        files = [text for name, text in sources.items() if name == place or name.startswith(place + "/")]
        if not any(has_producer(text, needle, statement) for text in files):
            kind = "cout" if statement is COUT_STATEMENT else "cout or EG_LOG"
            problems.append(f"{place}: no {kind} statement with the text {needle} (read by {readers})")
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
