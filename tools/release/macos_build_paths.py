#!/usr/bin/env python3
"""Remove or find build machine paths in the Mach-O files of a macOS app bundle.

usage: macos_build_paths.py {fix,check} APP

A library that Homebrew built can carry a run path into /opt/homebrew/Cellar, and a Qt plugin of Homebrew keeps the
install name that it had in /opt/homebrew/opt/qtbase/share/qt/plugins. Nothing loads through such a path, but a run path
would let the app load a library from a machine that has Homebrew, and the path shows where the package was built.

fix deletes every run path under /opt/homebrew or /usr/local and gives each Mach-O file whose install name is under
one of them the install name @loader_path/NAME. It changes the files, so it runs before the signature.

check reads otool -L, otool -D and otool -l of every Mach-O file of the app and fails when any line names a path under
/opt/homebrew or /usr/local.
"""
import argparse
import os
import re
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import package_start_smoke

OTOOL = "/usr/bin/otool"
INSTALL_NAME_TOOL = "/usr/bin/install_name_tool"
# The option of otool for the dependencies, the install name and the load commands.
OTOOL_OPTIONS = ("-L", "-D", "-l")
BUILD_PATH = re.compile(r"/(?:opt/homebrew|usr/local)(?:/|\s|$)")


def run(command):
    try:
        proc = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    except OSError as exc:
        raise SystemExit(f"cannot run {command[0]}: {exc}") from exc
    if proc.returncode != 0:
        raise SystemExit(f"{' '.join(command)} exited with {proc.returncode}\n{proc.stderr.decode('utf-8', errors='replace')}")
    return proc.stdout.decode("utf-8", errors="replace")


def otool_text(option: str, path: Path) -> str:
    """The output of otool without the lines that name the file itself."""
    lines = run([OTOOL, option, str(path)]).splitlines()
    return "\n".join(line for line in lines if not (line.startswith(str(path)) and line.endswith(":")))


def build_paths(text: str):
    """The lines of otool output that name a path under /opt/homebrew or /usr/local."""
    return [line.strip() for line in text.splitlines() if BUILD_PATH.search(line)]


def install_name(text: str) -> str:
    """The install name that otool -D printed, or an empty string for a file without one."""
    names = [line.strip() for line in text.splitlines() if line.strip()]
    return names[0] if names else ""


def macho_files(app: Path):
    found = []
    for directory, _, names in os.walk(app):
        found.extend(path for path in (Path(directory) / name for name in sorted(names)) if package_start_smoke.is_macho_file(path))
    return found


def check_app(app: Path, read=otool_text) -> None:
    """Fails when otool -L, -D or -l of a Mach-O file of the app names a path under /opt/homebrew or /usr/local."""
    files = macho_files(app)
    if not files:
        raise SystemExit(f"no Mach-O file in {app}")
    problems = []
    for path in files:
        for option in OTOOL_OPTIONS:
            problems.extend(f"{path.relative_to(app).as_posix()}: otool {option}: {line}" for line in build_paths(read(option, path)))
    if problems:
        raise SystemExit(f"{app} names build machine paths:\n" + "\n".join(problems))
    print(f"PASS no /opt/homebrew or /usr/local path in {app}: {len(files)} Mach-O files")


def fix_app(app: Path, read=otool_text, tool=run) -> None:
    """Deletes the run paths under /opt/homebrew or /usr/local and renames the install names under them."""
    changed = 0
    for path in macho_files(app):
        os.chmod(path, path.stat().st_mode | 0o200)
        _, rpaths = package_start_smoke.parse_load_commands(read("-l", path))
        for rpath in (entry for entry in rpaths if BUILD_PATH.search(entry)):
            tool([INSTALL_NAME_TOOL, "-delete_rpath", rpath, str(path)])
            changed += 1
        name = install_name(read("-D", path))
        if BUILD_PATH.search(name):
            tool([INSTALL_NAME_TOOL, "-id", f"@loader_path/{path.name}", str(path)])
            changed += 1
    print(f"Changed {changed} build machine paths in {app}")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("action", choices=("fix", "check"))
    parser.add_argument("app", type=Path)
    args = parser.parse_args()
    app = args.app.resolve()
    (fix_app if args.action == "fix" else check_app)(app)


if __name__ == "__main__":
    main()
