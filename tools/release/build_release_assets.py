#!/usr/bin/env python3
"""Build the files of a release from the package artifacts of one run, or only the portable Windows archive.

usage: build_release_assets.py --version X.Y.Z --artifacts DIR --output DIR
       build_release_assets.py --version X.Y.Z --payload DIR --output DIR

--artifacts holds what actions/download-artifact makes when it is given no
artifact name, one directory per artifact:

  QEGTRAIN-windows-x64-payload/                       the assembled Windows package, QEGTRAIN.exe at its top level
  EGTRAIN-windows-x64-portable/EGTRAIN-Portable-X.Y.Z-x64.zip
  EGTRAIN-windows-x64-installer/EGTRAIN-Setup-X.Y.Z-x64.exe
  QEGTRAIN-macos-arm64/EGTRAIN-X.Y.Z-macOS-arm64.dmg
  QEGTRAIN-linux-x86_64/QEGTRAIN-linux-x86_64.AppImage
  EGTRAIN-scenes/                                     the seven .egscene files

Other directories there are ignored. --output must not exist or must be an empty
directory. The script writes only there, and the directory holds exactly the files
of a release afterwards: the four packages, the seven scene bundles and
update-manifest.json. The packages and the bundles are copies.

The portable archive is written by the second form, which the Windows finalize job
runs on the payload before it uploads the archive. The archive has one member per
file of the payload, named by its path below the payload with forward slashes, in
the order of the sorted file list, with no directory entry and no top-level folder.
The first form checks that the archive it copies holds exactly the files of the
payload artifact, with the same bytes.

update-manifest.json takes the hash and the size of each package from the file in
--output, so they describe the public asset. The "files" of the portable entry are
the members of the archive.

Every input is checked before --output is created:

- The version has the form X.Y.Z that version.py accepts.
- Each file above exists and is not empty, and the artifact directories hold
  nothing else. The payload holds regular files and directories only, and every
  directory of it can be read.
- The file list of the Windows package follows the rules that parseManifestFiles
  in update/ReleaseInfo.cpp applies, and each package is as large as
  parseUpdateManifest there accepts. The application rejects a manifest that
  breaks one of them. The fixed set of runtime files in update/WindowsStaging.h is
  not checked here; the Windows package job verifies it before it uploads the
  package.

The second form writes EGTRAIN-Portable-X.Y.Z-x64.zip into --output after the same
checks of the version and the payload. The size of the archive is known only once
it is written, so it is checked then. A failure after --output is created leaves
that directory partly written.
"""
import argparse
import hashlib
import json
import os
import shutil
import zipfile
import zlib
from pathlib import Path

from version import parse_version

WINDOWS_KEY = "windows-x64"
PORTABLE_NAME = "EGTRAIN-Portable-{version}-x64.zip"
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
# order. {version} in a name stands for the release version.
PACKAGES = (
    ("macos-arm64", "EGTRAIN-{version}-macOS-arm64.dmg", "QEGTRAIN-macos-arm64"),
    (WINDOWS_KEY, PORTABLE_NAME, "EGTRAIN-windows-x64-portable"),
    ("windows-x64-installer", "EGTRAIN-Setup-{version}-x64.exe", "EGTRAIN-windows-x64-installer"),
    ("linux-x86_64", "QEGTRAIN-linux-x86_64.AppImage", "QEGTRAIN-linux-x86_64"),
)

# kMaxFiles and kMaxPathLength in parseManifestFiles (update/ReleaseInfo.cpp).
MAX_WINDOWS_FILES = 4096
MAX_PATH_UTF16_UNITS = 260
# kMaxPackageBytes in parseUpdateManifest (update/ReleaseInfo.cpp).
MAX_PACKAGE_BYTES = 2 * 1024 ** 3
CHUNK_BYTES = 1024 * 1024


def check_version(version: str) -> None:
    try:
        parse_version(version)
    except ValueError as error:
        raise SystemExit(f"version {version!r} is not a stable X.Y.Z version: {error}")


def package_name(template: str, version: str) -> str:
    return template.format(version=version)


def check_output_directory(output: Path) -> None:
    if output.exists() and (not output.is_dir() or any(output.iterdir())):
        raise SystemExit(f"{output} exists and is not an empty directory")


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

    def unreadable(error):
        raise SystemExit(f"cannot read {error.filename}: {error.strerror}")

    for directory, directories, names in os.walk(payload, onerror=unreadable):
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
        raise SystemExit(f"the Windows package holds {len(files)} files, at most {MAX_WINDOWS_FILES} are accepted")
    for path in files:
        problem = path_problem(path)
        if problem:
            raise SystemExit(f"the Windows package path {path!r} {problem}")
    if "QEGTRAIN.exe" not in files:
        raise SystemExit("the Windows package has no QEGTRAIN.exe at its top level")


def check_package_size(name: str, size: int) -> None:
    if not 1 <= size <= MAX_PACKAGE_BYTES:
        raise SystemExit(f"{name} is {size} bytes, the application accepts 1 to {MAX_PACKAGE_BYTES} bytes")


def file_sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with open(path, "rb") as stream:
        for chunk in iter(lambda: stream.read(CHUNK_BYTES), b""):
            digest.update(chunk)
    return digest.hexdigest()


def write_windows_archive(payload: Path, files, archive: Path) -> None:
    # strict_timestamps=False clamps a modification time before 1980 instead of failing.
    with zipfile.ZipFile(archive, "w", zipfile.ZIP_DEFLATED, strict_timestamps=False) as zip_file:
        for name in files:
            zip_file.write(payload / name, name)


def same_content(zip_file: zipfile.ZipFile, name: str, path: Path) -> bool:
    """True when the member and the file have the same bytes. Reading a member to its end checks its CRC."""
    with zip_file.open(name) as member, open(path, "rb") as source:
        while True:
            chunk = member.read(CHUNK_BYTES)
            if chunk != source.read(CHUNK_BYTES):
                return False
            if not chunk:
                return True


def check_archive_holds_payload(archive: Path, payload: Path, files) -> None:
    """The archive has one member per listed file, in the order of the list, with the bytes of the file."""
    try:
        with zipfile.ZipFile(archive) as zip_file:
            if zip_file.namelist() != files:
                raise SystemExit(f"{archive} does not hold the files of {payload}")
            for name in files:
                if not same_content(zip_file, name, payload / name):
                    raise SystemExit(f"{archive} member {name} differs from the file of {payload}")
    except (zipfile.BadZipFile, zlib.error) as error:
        raise SystemExit(f"{archive} is not a readable archive: {error}")


def build_portable_archive(payload, output, version: str) -> Path:
    """Writes the portable archive of the payload into output and returns its path."""
    payload, output = Path(payload), Path(output)
    check_version(version)
    files = list_payload(payload)
    check_windows_files(files)
    check_output_directory(output)

    name = package_name(PORTABLE_NAME, version)
    output.mkdir(parents=True, exist_ok=True)
    write_windows_archive(payload, files, output / name)
    check_package_size(name, (output / name).stat().st_size)
    check_archive_holds_payload(output / name, payload, files)
    return output / name


def build_release_assets(artifacts, output, version: str) -> list:
    """Writes the release assets into output and returns their paths."""
    artifacts, output = Path(artifacts), Path(output)
    check_version(version)

    copies, package_names = [], []
    for key, template, directory in PACKAGES:
        name = package_name(template, version)
        require_files(artifacts / directory, [name])
        check_package_size(name, (artifacts / directory / name).stat().st_size)
        copies.append((artifacts / directory / name, name))
        package_names.append((key, name))
        if key == WINDOWS_KEY:
            portable = artifacts / directory / name
    scene_files = [scene + SCENE_SUFFIX for scene in SCENES]
    require_files(artifacts / SCENES_ARTIFACT, scene_files)
    copies += [(artifacts / SCENES_ARTIFACT / name, name) for name in scene_files]
    payload = artifacts / WINDOWS_PAYLOAD
    files = list_payload(payload)
    check_windows_files(files)
    check_archive_holds_payload(portable, payload, files)
    check_output_directory(output)

    output.mkdir(parents=True, exist_ok=True)
    for source, name in copies:
        shutil.copyfile(source, output / name)

    assets = {}
    for key, name in package_names:
        assets[key] = {"name": name, "sha256": file_sha256(output / name), "size": os.path.getsize(output / name)}
    assets[WINDOWS_KEY]["files"] = files
    manifest = {"version": version, "assets": assets}
    with open(output / MANIFEST_NAME, "w", encoding="utf-8", newline="\n") as stream:
        stream.write(json.dumps(manifest, indent=2) + "\n")

    expected = [name for _key, name in package_names] + scene_files + [MANIFEST_NAME]
    found = sorted(os.listdir(output))
    if found != sorted(expected):
        raise SystemExit(f"{output} holds {found}, expected {sorted(expected)}")
    return [output / name for name in found]


def main(argv=None) -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--version", required=True)
    source = parser.add_mutually_exclusive_group(required=True)
    source.add_argument("--artifacts", type=Path)
    source.add_argument("--payload", type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args(argv)
    if args.payload is not None:
        archive = build_portable_archive(args.payload, args.output, args.version)
        print(f"{archive.name}  {archive.stat().st_size} bytes")
        print(f"OK: portable archive in {args.output}")
        return
    assets = build_release_assets(args.artifacts, args.output, args.version)
    for path in assets:
        print(f"{path.name}  {path.stat().st_size} bytes")
    print(f"OK: {len(assets)} release assets in {args.output}")


if __name__ == "__main__":
    main()
