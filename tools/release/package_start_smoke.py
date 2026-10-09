#!/usr/bin/env python3
"""Start QEGTRAIN from an assembled package directory with nothing else to load from.

usage: package_start_smoke.py PACKAGE_DIR [--platform NAME]

The package must bring every library and plugin that QEGTRAIN needs. Both
launches get a search path with only the operating system directories and no
Qt or vcpkg variable, so a library that is missing from the package makes the
launch fail here as it would on a machine without a developer setup.

1. Headless (-g 0) on the packaged Paimpol scene for a short horizon, to the
   end of the run.
2. With a window, in startup timing mode: the application opens the scene,
   prepares a run, paints it and exits.

Before the launches it checks that no file of the package is an OpenMP runtime
library or names one. QEGTRAIN does not use OpenMP, and the package ships no
such library, so an executable that asks for one would not start everywhere.
For a package with QEGTRAIN.app it also checks the dependencies of the bundle:
everything that a Mach-O file loads is a system library or a file of the app,
and every Mach-O file of the app is loaded by some other file, or is the
program or a plugin, which Qt loads by name.

--platform sets QT_QPA_PLATFORM for the second launch. Without it the platform
plugin of the package is used, which is what a user gets.
"""
import argparse
import os
import re
import subprocess
import sys
import tempfile
from pathlib import Path

RUN_TIMEOUT_SECONDS = 120
HORIZON_SECONDS = "120"
# Variables that point a process at a developer installation of Qt or vcpkg.
DEVELOPER_PREFIXES = ("QT_", "QML", "QT5", "VCPKG", "CMAKE_")
# File names of the OpenMP runtime of MSVC, LLVM, Intel and GCC.
OPENMP_RUNTIME = re.compile(rb"vcomp\d+d?\.dll|lib(?:i?omp|gomp)[\w.]*?\.(?:dll|dylib|so)", re.IGNORECASE)
# The first four bytes of a 64-bit Mach-O file and of a universal one.
MACHO_MAGIC = (b"\xcf\xfa\xed\xfe", b"\xca\xfe\xba\xbe")
# Load commands that name a library the file needs at load time.
DEPENDENCY_COMMANDS = ("LC_LOAD_DYLIB", "LC_LOAD_WEAK_DYLIB", "LC_REEXPORT_DYLIB", "LC_LOAD_UPWARD_DYLIB")
SYSTEM_LIBRARY_PREFIXES = ("/System/Library/", "/usr/lib/")
LOAD_COMMAND = re.compile(r"^\s*cmd (\w+)\s*$")
LOAD_COMMAND_PATH = re.compile(r"^\s*(?:name|path) (.+) \(offset \d+\)\s*$")


def find_package_layout(package: Path):
    """Returns the executable and the scene directory of a Windows, Linux or macOS package."""
    candidates = (
        (package / "QEGTRAIN.exe", package / "Scenes"),
        (package / "QEGTRAIN.app/Contents/MacOS/QEGTRAIN", package / "QEGTRAIN.app/Contents/Resources/Scenes"),
        (package / "QEGTRAIN", package / "Scenes"),
    )
    for executable, scenes in candidates:
        if executable.is_file():
            return executable, scenes / "Paimpol"
    raise SystemExit(f"no QEGTRAIN executable in {package}")


def check_no_openmp_runtime(package: Path) -> None:
    for path in sorted(package.rglob("*")):
        if not path.is_file():
            continue
        if OPENMP_RUNTIME.match(path.name.encode()):
            raise SystemExit(f"the package contains an OpenMP runtime library: {path}")
        named = OPENMP_RUNTIME.search(path.read_bytes())
        if named:
            raise SystemExit(f"{path} names the OpenMP runtime library {named.group().decode()}")
    print(f"PASS no OpenMP runtime library in {package}")


def find_app_bundle(package: Path):
    """Returns QEGTRAIN.app of a macOS package, or None for a package without it."""
    app = package / "QEGTRAIN.app"
    return app if app.is_dir() else None


def parse_load_commands(text: str):
    """Returns the dependencies and the rpaths that the text printed by otool -l names."""
    dependencies, rpaths = [], []
    command = ""
    for line in text.splitlines():
        started = LOAD_COMMAND.match(line)
        if started:
            command = started.group(1)
            continue
        named = LOAD_COMMAND_PATH.match(line)
        if not named:
            continue
        if command in DEPENDENCY_COMMANDS:
            dependencies.append(named.group(1))
        elif command == "LC_RPATH":
            rpaths.append(named.group(1))
    return dependencies, rpaths


def otool_load_info(path: Path):
    try:
        proc = subprocess.run(["/usr/bin/otool", "-l", str(path)], stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    except OSError as exc:
        raise SystemExit(f"cannot run /usr/bin/otool to read the load commands of {path}: {exc}") from exc
    if proc.returncode != 0:
        raise SystemExit(f"otool -l {path} exited with {proc.returncode}\n{proc.stderr.decode('utf-8', errors='replace')}")
    return parse_load_commands(proc.stdout.decode("utf-8", errors="replace"))


def is_macho_file(path: Path) -> bool:
    if path.is_symlink() or not path.is_file():
        return False
    with path.open("rb") as handle:
        return handle.read(4) in MACHO_MAGIC


def expand_path(name: str, holder: Path, root: Path) -> Path:
    """Replaces a leading @executable_path or @loader_path of a load command."""
    if name.startswith("@executable_path/"):
        return root / "Contents/MacOS" / name[len("@executable_path/"):]
    if name.startswith("@loader_path/"):
        return holder.parent / name[len("@loader_path/"):]
    return Path(name)


def find_dependency(name: str, holder: Path, rpaths, root: Path):
    """Returns the file inside the app that a load command names, and the reason when there is none."""
    if name.startswith("@rpath/"):
        candidates = [expand_path(entry, holder, root) / name[len("@rpath/"):] for entry in rpaths]
    elif name.startswith(("@executable_path/", "@loader_path/")):
        candidates = [expand_path(name, holder, root)]
    else:
        return None, "is neither a system library nor a file of the app"
    target = next((candidate.resolve() for candidate in candidates if candidate.is_file()), None)
    if target is None:
        return None, "does not exist inside the app"
    if root not in target.parents:
        return None, f"is outside the app, at {target}"
    return target, ""


def check_bundle_closure(app: Path, load_info=otool_load_info) -> None:
    """Fails unless every library that a Mach-O file of the app loads is a system library or a file of the
    app, and every Mach-O file of the app is the program, a plugin, or loaded by another file."""
    root = app.resolve()
    macho = []
    for directory, _, names in os.walk(root):
        macho.extend(path for path in (Path(directory) / name for name in sorted(names)) if is_macho_file(path))
    if not macho:
        raise SystemExit(f"no Mach-O file in {app}")
    known = set(macho)
    starts = (root / "Contents/MacOS", root / "Contents/PlugIns")
    queue = [path for path in sorted(macho) if any(top in path.parents for top in starts)]
    reached = set(queue)
    problems = []
    while queue:
        holder = queue.pop(0)
        label = holder.relative_to(root).as_posix()
        dependencies, rpaths = load_info(holder)
        for name in dependencies:
            if name.startswith(SYSTEM_LIBRARY_PREFIXES):
                continue
            target, problem = find_dependency(name, holder, rpaths, root)
            if problem:
                problems.append(f"{label}: {name} {problem}")
            elif target in known and target not in reached:
                reached.add(target)
                queue.append(target)
    problems.extend(f"{path.relative_to(root).as_posix()} is loaded by no file of the app" for path in sorted(known - reached))
    if problems:
        raise SystemExit(f"the dependencies of {app} do not close:\n" + "\n".join(problems))
    print(f"PASS dependency closure of {app}: {len(macho)} Mach-O files")


def clean_environment() -> dict:
    env = {name: value for name, value in os.environ.items()
           if not name.upper().startswith(DEVELOPER_PREFIXES)}
    if os.name == "nt":
        root = os.environ.get("SystemRoot", r"C:\Windows")
        env["PATH"] = os.pathsep.join((str(Path(root) / "System32"), root))
    else:
        env["PATH"] = "/usr/bin:/bin"
    return env


def launch(command, cwd: Path, env: dict, what: str) -> str:
    try:
        proc = subprocess.run(command, cwd=cwd, env=env, stdout=subprocess.PIPE,
                              stderr=subprocess.STDOUT, timeout=RUN_TIMEOUT_SECONDS)
    except subprocess.TimeoutExpired as exc:
        tail = (exc.stdout or b"").decode("utf-8", errors="replace")[-4000:]
        raise SystemExit(f"{what} timed out after {RUN_TIMEOUT_SECONDS}s\n{tail}") from exc
    text = proc.stdout.decode("utf-8", errors="replace")
    if proc.returncode != 0:
        raise SystemExit(f"{what} exited with {proc.returncode}\n{text[-4000:]}")
    return text


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("package", type=Path)
    parser.add_argument("--platform")
    args = parser.parse_args()

    package = args.package.resolve()
    executable, scene = find_package_layout(package)
    if not (scene / "scene.json").is_file():
        raise SystemExit(f"the package has no Paimpol scene at {scene}")
    check_no_openmp_runtime(package)
    app = find_app_bundle(package)
    if app:
        check_bundle_closure(app)

    with tempfile.TemporaryDirectory(prefix="qegtrain-package-") as temp:
        env = clean_environment()
        env["QEGTRAIN_OUTPUT_DIR"] = str(Path(temp) / "headless")
        text = launch([str(executable), "--scene", str(scene), "-h", HORIZON_SECONDS, "-g", "0",
                       "-pax", "0", "-TSM", "0", "-RC", "0"], executable.parent, env, "headless run")
        if "End of Simulation" not in text:
            raise SystemExit(f"headless run printed no 'End of Simulation'\n{text[-4000:]}")
        energy = list((Path(temp) / "headless" / "Output").glob("*/EnergyConsumptionPerTrain.txt"))
        if len(energy) != 1:
            raise SystemExit(f"expected one EnergyConsumptionPerTrain.txt, found {len(energy)}\n{text[-4000:]}")
        print(f"PASS headless run of {scene.name} from {package}")

        env = clean_environment()
        env["QEGTRAIN_OUTPUT_DIR"] = str(Path(temp) / "window")
        env["QEGTRAIN_STARTUP_TIMING"] = "1"
        env["QEGTRAIN_STARTUP_WARM_TRIALS"] = "0"
        if args.platform:
            env["QT_QPA_PLATFORM"] = args.platform
        text = launch([str(executable), "--scene", str(scene)], executable.parent, env, "window start")
        if "first_runtime_paint" not in text:
            raise SystemExit(f"window start did not paint the prepared run\n{text[-4000:]}")
        print(f"PASS window start of {scene.name} from {package}")


if __name__ == "__main__":
    main()
