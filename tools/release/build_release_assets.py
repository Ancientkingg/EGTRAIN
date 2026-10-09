#!/usr/bin/env python3
"""Build the files of a release from the package artifacts of one run.

usage: build_release_assets.py --version X.Y.Z --artifacts DIR --output DIR

--artifacts holds what actions/download-artifact makes when it is given no
artifact name, one directory per artifact:

  QEGTRAIN-windows-x64-payload/   the assembled Windows package, QEGTRAIN.exe at its top level
  QEGTRAIN-macos-arm64/QEGTRAIN-macos-arm64.zip
  QEGTRAIN-linux-x86_64/QEGTRAIN-linux-x86_64.AppImage
  EGTRAIN-scenes/                 the seven .egscene files

Other directories there are ignored. --output must not exist or must be an empty
directory. The script writes only there, and the directory holds exactly the files
of a release afterwards: the three packages, the seven scene bundles and
update-manifest.json. The macOS package, the AppImage and the bundles are copies.
The Windows archive is written from the files of the payload: one member per file,
named by its path below the payload with forward slashes, in the order of the
sorted file list, with no directory entry and no top-level folder.

update-manifest.json takes the hash and the size of each package from the file in
--output, so they describe the public asset. The "files" of the Windows entry are
the members of the archive.

Every input is checked before --output is created:

- The version has the form X.Y.Z that version.py accepts.
- Each file above exists and is not empty, and the artifact directories hold
  nothing else. The payload holds regular files and directories only.
- The file list of the Windows package follows the rules that parseManifestFiles
  in update/ReleaseInfo.cpp applies, and each package is as large as
  parseUpdateManifest there accepts. The application rejects a manifest that
  breaks one of them. The fixed set of runtime files in update/WindowsStaging.h is
  not checked here; the Windows package job verifies it before it uploads the
  package.

The size of the Windows archive is known only once it is written, so it is checked
then. A failure after --output is created leaves that directory partly written.
"""
import argparse
import hashlib
import json
import os
import shutil
import zipfile
from pathlib import Path

from version import parse_version

WINDOWS_KEY = "windows-x64"
WINDOWS_NAME = "QEGTRAIN-windows-x64.zip"
WINDOWS_PAYLOAD = "QEGTRAIN-windows-x64-payload"
SCENES_ARTIFACT = "EGTRAIN-scenes"
SCENES = (
    "Netherlands",
    "Paimpol",
    "Copenhagen",
    "Milano_Brescia",
    "Assignment_Gvc_Gdg_Ut",
    "Lebanon",
    "Amsterdam_Hilversum_Student",
)
SCENE_SUFFIX = ".egscene"
MANIFEST_NAME = "update-manifest.json"
# Manifest key, public file name and artifact directory of each package, in manifest
# order. The Windows archive is built from WINDOWS_PAYLOAD and has no artifact directory.
PACKAGES = (
    ("macos-arm64", "QEGTRAIN-macos-arm64.zip", "QEGTRAIN-macos-arm64"),
    (WINDOWS_KEY, WINDOWS_NAME, None),
    ("linux-x86_64", "QEGTRAIN-linux-x86_64.AppImage", "QEGTRAIN-linux-x86_64"),
)

# kMaxFiles and kMaxPathLength in parseManifestFiles (update/ReleaseInfo.cpp).
MAX_WINDOWS_FILES = 4096
MAX_PATH_UTF16_UNITS = 260
# kMaxPackageBytes in parseUpdateManifest (update/ReleaseInfo.cpp).
MAX_PACKAGE_BYTES = 2 * 1024 ** 3
HASH_CHUNK_BYTES = 1024 * 1024


def check_version(version: str) -> None:
    try:
        parse_version(version)
    except ValueError as error:
        raise SystemExit(f"version {version!r} is not a stable X.Y.Z version: {error}")


def require_files(directory: Path, names) -> None:
    """The directory holds the named files, none of them empty, and nothing else."""
    if not directory.is_dir():
        raise SystemExit(f"missing directory {directory}")
    for name in names:
        path = directory / name
        if not path.is_file():
            raise SystemExit(f"missing file {path}")
        if path.stat().st_size == 0:
            raise SystemExit(f"empty file {path}")
    extra = sorted(set(os.listdir(directory)) - set(names))
    if extra:
        raise SystemExit(f"unexpected {extra[0]} in {directory}")


def list_payload(payload: Path) -> list:
    """Relative paths with forward slashes of the files below the payload, sorted as strings."""
    if not payload.is_dir():
        raise SystemExit(f"missing directory {payload}")
    files = []
    for directory, directories, names in os.walk(payload):
        for name in directories:
            path = Path(directory, name)
            if path.is_symlink():
                raise SystemExit(f"{path} is a link, which the package may not hold")
        for name in names:
            path = Path(directory, name)
            if path.is_symlink() or not path.is_file():
                raise SystemExit(f"{path} is a link or not a regular file, which the package may not hold")
            files.append(path.relative_to(payload).as_posix())
    if not files:
        raise SystemExit(f"{payload} holds no file")
    return sorted(files)


def path_problem(path: str):
    """Why the application rejects this path of a file list, or None."""
    # The application reads a backslash as a slash and accepts the path. The list has
    # to name the members of the archive, and the archive uses forward slashes only,
    # so a backslash is rejected here.
    if "\\" in path:
        return "contains a backslash"
    try:
        # QString::size() counts UTF-16 code units.
        units = len(path.encode("utf-16-le")) // 2
    except UnicodeEncodeError:
        return "is not valid text"
    if units > MAX_PATH_UTF16_UNITS:
        return f"is {units} characters long, at most {MAX_PATH_UTF16_UNITS} are accepted"
    if ":" in path or "\0" in path:
        return "contains a colon or a NUL character"
    if any(segment in ("", ".", "..") for segment in path.split("/")):
        return 'has an empty, "." or ".." segment'
    return None


def check_windows_files(files) -> None:
    if len(files) > MAX_WINDOWS_FILES:
        raise SystemExit(f"{WINDOWS_PAYLOAD} holds {len(files)} files, at most {MAX_WINDOWS_FILES} are accepted")
    for path in files:
        problem = path_problem(path)
        if problem:
            raise SystemExit(f"{WINDOWS_PAYLOAD} path {path!r} {problem}")
    if "QEGTRAIN.exe" not in files:
        raise SystemExit(f"{WINDOWS_PAYLOAD} has no QEGTRAIN.exe at its top level")


def check_package_size(name: str, size: int) -> None:
    if not 1 <= size <= MAX_PACKAGE_BYTES:
        raise SystemExit(f"{name} is {size} bytes, the application accepts 1 to {MAX_PACKAGE_BYTES} bytes")


def file_sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with open(path, "rb") as stream:
        for chunk in iter(lambda: stream.read(HASH_CHUNK_BYTES), b""):
            digest.update(chunk)
    return digest.hexdigest()


def write_windows_archive(payload: Path, files, archive: Path) -> None:
    # strict_timestamps=False clamps a modification time before 1980 instead of failing.
    with zipfile.ZipFile(archive, "w", zipfile.ZIP_DEFLATED, strict_timestamps=False) as zip_file:
        for name in files:
            zip_file.write(payload / name, name)


def build_release_assets(artifacts, output, version: str) -> list:
    """Writes the release assets into output and returns their paths."""
    artifacts, output = Path(artifacts), Path(output)
    check_version(version)

    copies = []
    for _key, name, directory in PACKAGES:
        if directory:
            require_files(artifacts / directory, [name])
            check_package_size(name, (artifacts / directory / name).stat().st_size)
            copies.append((artifacts / directory / name, name))
    scene_files = [scene + SCENE_SUFFIX for scene in SCENES]
    require_files(artifacts / SCENES_ARTIFACT, scene_files)
    copies += [(artifacts / SCENES_ARTIFACT / name, name) for name in scene_files]
    payload = artifacts / WINDOWS_PAYLOAD
    files = list_payload(payload)
    check_windows_files(files)
    if output.exists() and (not output.is_dir() or any(output.iterdir())):
        raise SystemExit(f"{output} exists and is not an empty directory")

    output.mkdir(parents=True, exist_ok=True)
    for source, name in copies:
        shutil.copyfile(source, output / name)
    write_windows_archive(payload, files, output / WINDOWS_NAME)
    check_package_size(WINDOWS_NAME, (output / WINDOWS_NAME).stat().st_size)

    assets = {}
    for key, name, _directory in PACKAGES:
        assets[key] = {"name": name, "sha256": file_sha256(output / name), "size": os.path.getsize(output / name)}
    assets[WINDOWS_KEY]["files"] = files
    manifest = {"version": version, "assets": assets}
    with open(output / MANIFEST_NAME, "w", encoding="utf-8", newline="\n") as stream:
        stream.write(json.dumps(manifest, indent=2) + "\n")

    expected = [name for _key, name, _directory in PACKAGES] + scene_files + [MANIFEST_NAME]
    found = sorted(os.listdir(output))
    if found != sorted(expected):
        raise SystemExit(f"{output} holds {found}, expected {sorted(expected)}")
    with zipfile.ZipFile(output / WINDOWS_NAME) as zip_file:
        damaged = zip_file.testzip()
        if damaged is not None:
            raise SystemExit(f"{output / WINDOWS_NAME} has a damaged member {damaged}")
        if zip_file.namelist() != files:
            raise SystemExit(f"{output / WINDOWS_NAME} does not hold the files of the manifest")
    return [output / name for name in found]


def main(argv=None) -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--version", required=True)
    parser.add_argument("--artifacts", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args(argv)
    assets = build_release_assets(args.artifacts, args.output, args.version)
    for path in assets:
        print(f"{path.name}  {path.stat().st_size} bytes")
    print(f"OK: {len(assets)} release assets in {args.output}")


if __name__ == "__main__":
    main()
