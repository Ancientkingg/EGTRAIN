#!/usr/bin/env python3
"""Runs clang-tidy on the EGTRAIN C++ sources outside simulation/ and lists the findings.

  python3 tools/tidy.py                        analyse every source of the modules
  python3 tools/tidy.py --changed-from REV     analyse the sources changed since REV, and the sources that include a changed header
  python3 tools/tidy.py FILE...                analyse these sources only

The command configures a Ninja build in build_tidy/ (--build-dir changes it) so that
compile_commands.json exists, builds the sources that Qt generates (moc, uic and rcc), and runs
clang-tidy on each source of the build in parallel. The checks are the ones in .clang-tidy;
tests/.clang-tidy relaxes them for the tests. The exit status is 1 when there is any finding.

clang-tidy releases add and change checks, so the script accepts one minor version. CLANG_TIDY names
the executable when it is not the clang-tidy on PATH.
"""
import argparse
import json
import os
import re
import shutil
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

CLANG_TIDY_VERSION = "23.1"
ROOT = Path(__file__).resolve().parents[1]
SOURCE_ROOT = "EGTRAIN/QEGTRAIN"
# The modules that are analysed. simulation/ is legacy code and waits for its own cleanup, and io/third_party is vendored.
MODULES = ("app", "diagrams", "editors", "graphics", "io", "recovery", "scene", "telemetry", "tests", "update", "util", "widgets")
VENDORED = SOURCE_ROOT + "/io/third_party/"
HEADER_SUFFIXES = (".h", ".hpp")
# A change to one of these files can change the findings of any source. So can a change to a form or resource file, which
# the build turns into headers and sources.
GLOBAL_INPUTS = ("tools/tidy.py", "CMakeLists.txt", ".clang-tidy", ".github/workflows/tidy.yml")
GLOBAL_SUFFIXES = ("/.clang-tidy", "/CMakeLists.txt", ".ui", ".qrc")
INSTALL_HINT = "brew install llvm (macOS), or on Ubuntu add apt.llvm.org and install clang-tidy-23"
# The findings of files that other work is changing right now: the number of findings of each check in each file.
# A file with more findings of a check than listed here reports all of them. Remove an entry when its findings are fixed.
KNOWN_FINDINGS = {
    "EGTRAIN/QEGTRAIN/app/DispatchController.cpp": {"bugprone-integer-division": 1},
    "EGTRAIN/QEGTRAIN/app/MainWindow.cpp": {
        "misc-unused-parameters": 8,
        "performance-no-automatic-move": 1,
        "performance-unnecessary-value-param": 3,
        "readability-redundant-declaration": 4,
    },
    "EGTRAIN/QEGTRAIN/app/MainWindow.h": {"readability-duplicate-include": 1},
}
INCLUDE = re.compile(r'^\s*#\s*include\s*"(?:[^"]*/)?([^"/]+)"', re.MULTILINE)
FINDING = re.compile(r"^(?P<file>/\S.*?):(?P<line>\d+):(?P<column>\d+): (?:warning|error): (?P<message>.*) \[(?P<check>[^\]]+)\]$")


def find_clang_tidy() -> str:
    candidates = [
        os.environ.get("CLANG_TIDY"),
        shutil.which("clang-tidy-" + CLANG_TIDY_VERSION.split(".")[0]),
        shutil.which("clang-tidy"),
        "/opt/homebrew/opt/llvm/bin/clang-tidy",
        "/usr/local/opt/llvm/bin/clang-tidy",
    ]
    executable = next((name for name in candidates if name and shutil.which(name)), None)
    if not executable:
        sys.exit(f"clang-tidy {CLANG_TIDY_VERSION} is not installed. Install it with: {INSTALL_HINT}")
    try:
        reported = subprocess.run([executable, "--version"], capture_output=True, text=True, check=True).stdout
    except (OSError, subprocess.CalledProcessError) as error:
        sys.exit(f"{executable} does not run: {error}")
    match = re.search(r"version (\d+\.\d+)\.\d+", reported)
    found = match.group(1) if match else reported.strip()
    if found != CLANG_TIDY_VERSION:
        sys.exit(f"{executable} is clang-tidy {found}, but the findings are agreed for {CLANG_TIDY_VERSION}. Install that version with: {INSTALL_HINT}")
    return executable


def run(command: list, **options) -> subprocess.CompletedProcess:
    return subprocess.run(command, cwd=ROOT, check=True, **options)


def configure(build_dir: Path) -> None:
    arguments = ["-DCMAKE_BUILD_TYPE=Debug", "-DEGTRAIN_BUILD_TESTS=ON", "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON"]
    homebrew_qt = Path("/opt/homebrew/opt/qt@5")
    if sys.platform == "darwin" and homebrew_qt.is_dir():
        arguments.append(f"-DCMAKE_PREFIX_PATH={homebrew_qt}")
    run(["cmake", "-S", str(ROOT), "-B", str(build_dir), "-G", "Ninja", *arguments])


def build_generated_sources(build_dir: Path) -> None:
    """The sources include headers that the build writes: moc output, uic headers and the resource files. Their
    targets are the *_autogen targets."""
    listed = run(["ninja", "-C", str(build_dir), "-t", "targets", "all"], capture_output=True, text=True).stdout
    targets = sorted(set(re.findall(r"^(\w+_autogen): ", listed, re.MULTILINE)))
    run(["cmake", "--build", str(build_dir), "--target", *targets])


def tracked_files() -> list:
    listed = run(["git", "ls-files", "-z", "--", SOURCE_ROOT], capture_output=True).stdout.decode("utf-8")
    return [name for name in listed.split("\0") if name]


def analysed(name: str) -> bool:
    parts = name.split("/")
    return name.startswith(SOURCE_ROOT + "/") and parts[2] in MODULES and not name.startswith(VENDORED)


def compiled_sources(build_dir: Path) -> set:
    """The tracked sources of the modules that the build compiles, relative to the repository root."""
    database = json.loads((build_dir / "compile_commands.json").read_text(encoding="utf-8"))
    names = set()
    for entry in database:
        path = Path(entry["file"])
        if not path.is_absolute():
            path = Path(entry["directory"]) / path
        try:
            names.add(path.resolve().relative_to(ROOT).as_posix())
        except ValueError:
            pass
    return {name for name in names if analysed(name) and name.endswith(".cpp")}


def changed_files(revision: str) -> list:
    listed = run(
        ["git", "-c", "core.quotepath=false", "diff", "--name-only", "--no-renames", "--diff-filter=d", revision, "HEAD"],
        capture_output=True,
        text=True,
    ).stdout
    return listed.splitlines()


def select_changed(changed: list, sources: set) -> list:
    """A source is analysed when it changed, when it includes a changed header directly or through other headers, or when
    the configuration changed. The include test reads the name of the header, not the include path."""
    if any(name in GLOBAL_INPUTS or name.endswith(GLOBAL_SUFFIXES) for name in changed):
        return sorted(sources)
    selected = {name for name in changed if name in sources}
    pending = [Path(name).name for name in changed if name.endswith(HEADER_SUFFIXES) and name.startswith(SOURCE_ROOT + "/")]
    if pending:
        includers = {}
        for name in tracked_files():
            if name.endswith(HEADER_SUFFIXES + (".cpp",)):
                for included in INCLUDE.findall((ROOT / name).read_text(encoding="utf-8", errors="replace")):
                    includers.setdefault(included, []).append(name)
        reached = set(pending)
        while pending:
            for name in includers.get(pending.pop(), []):
                if name in sources:
                    selected.add(name)
                elif name.endswith(HEADER_SUFFIXES) and Path(name).name not in reached:
                    reached.add(Path(name).name)
                    pending.append(Path(name).name)
    return sorted(selected)


def analyse(clang_tidy: str, build_dir: Path, name: str, extra: list) -> tuple:
    result = subprocess.run(
        [clang_tidy, "-p", str(build_dir), "--quiet", *extra, name], cwd=ROOT, capture_output=True, text=True
    )
    found = {}
    for line in result.stdout.splitlines():
        match = FINDING.match(line)
        if match:
            path = Path(match["file"])
            shown = path.relative_to(ROOT).as_posix() if path.is_relative_to(ROOT) else str(path)
            found[(shown, int(match["line"]), int(match["column"]), match["check"])] = match["message"]
    return name, found, result.returncode, result.stderr


def without_known(findings: dict) -> dict:
    counts = {}
    for name, _, _, check in findings:
        counts[(name, check)] = counts.get((name, check), 0) + 1
    return {
        key: text
        for key, text in findings.items()
        if counts[(key[0], key[3])] > KNOWN_FINDINGS.get(key[0], {}).get(key[3], 0)
    }


def main() -> int:
    parser = argparse.ArgumentParser(description="Run clang-tidy on the C++ sources outside simulation/.")
    parser.add_argument("files", nargs="*", help="analyse these sources only")
    parser.add_argument("--changed-from", metavar="REV", help="analyse the sources changed since REV")
    parser.add_argument("--build-dir", default="build_tidy", help="build directory (default: build_tidy)")
    parser.add_argument("-j", "--jobs", type=int, default=os.cpu_count() or 2, help="parallel clang-tidy processes")
    parser.add_argument("--no-build", action="store_true", help="reuse the configured build directory and its generated sources")
    arguments = parser.parse_args()
    clang_tidy = find_clang_tidy()
    build_dir = (ROOT / arguments.build_dir).resolve()
    if not arguments.no_build:
        configure(build_dir)
        build_generated_sources(build_dir)
    sources = compiled_sources(build_dir)
    if arguments.files:
        names = sorted({Path(f).resolve().relative_to(ROOT).as_posix() for f in arguments.files})
        unknown = [name for name in names if name not in sources]
        if unknown:
            sys.exit("Not a source of the analysed modules in this build: " + ", ".join(unknown))
    elif arguments.changed_from:
        names = select_changed(changed_files(arguments.changed_from), sources)
    else:
        names = sorted(sources)
    if not names:
        print("no sources to analyse")
        return 0
    extra = []
    if sys.platform == "darwin":
        sdk = subprocess.run(["xcrun", "--show-sdk-path"], capture_output=True, text=True, check=True).stdout.strip()
        extra = ["--extra-arg=-isysroot", f"--extra-arg={sdk}"]
    findings = {}
    failures = []
    with ThreadPoolExecutor(max_workers=arguments.jobs) as pool:
        for name, found, code, errors in pool.map(lambda n: analyse(clang_tidy, build_dir, n, extra), names):
            findings.update(found)
            if code != 0 and not found:
                failures.append((name, errors.strip()))
    findings = without_known(findings)
    for (name, line, column, check), message in sorted(findings.items()):
        print(f"{name}:{line}:{column}: {message} [{check}]")
    for name, errors in failures:
        print(f"clang-tidy failed on {name}: {errors}")
    if findings or failures:
        print(f"{len(findings)} findings in {len(names)} sources")
        return 1
    print(f"{len(names)} sources have no findings")
    return 0


if __name__ == "__main__":
    sys.exit(main())
