#!/usr/bin/env python3
"""Choose one application version before building release packages, and give a release its name and notes.

usage: version.py                 write version=X.Y.Z to GITHUB_OUTPUT for the release workflow
       version.py name TAG        print the name of the release TAG
       version.py notes TAG       print the notes of the release TAG

A file docs/releases/X.Y.Z.md names release X.Y.Z and holds its notes: a first line "# Title" and the notes below it.
Such a release is called "EGTRAIN vX.Y.Z: Title". A push to production publishes the lowest version of such a file that
is above every stable tag and every reserved release version, and otherwise the next patch version.
"""

import os
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
NOTES_DIR = ROOT / "docs" / "releases"
STABLE_VERSION = r"(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)"
# The notes of a release without a notes file.
DEFAULT_NOTES = """Automated EGTRAIN build for `{name}`.

Download the package for your platform. The bundled cases are also available as `.egscene` files.
"""


def parse_version(value: str) -> tuple[int, int, int]:
    if not re.fullmatch(STABLE_VERSION, value):
        raise ValueError(f"Invalid application version: {value}")
    version = tuple(map(int, value.split(".")))
    if any(component > 65535 for component in version):
        raise ValueError("Version components must fit Windows version resources")
    return version


def select_version(base: str, tags: list[str], ref: str, notes: list[str] = (), drafts: list[str] = ()) -> str:
    """The version of a build of ref. tags are the stable tags and the reserved release versions, notes the versions that
    have a notes file, and drafts the reserved versions that have no Git tag yet: the drafts of runs that failed."""
    baseline = parse_version(base)
    if ref.startswith("refs/tags/v"):
        # Prerelease tags remain opt-in and are not offered by the updater.
        version = ref.removeprefix("refs/tags/v").split("-", 1)[0]
        if parse_version(version) < baseline:
            raise ValueError("Release tag is older than the application version baseline")
        return version
    if ref != "refs/heads/production":
        return base
    versions = [baseline]
    for tag in tags:
        if re.fullmatch("v" + STABLE_VERSION, tag):
            versions.append(parse_version(tag[1:]))
    highest = max(versions)
    for draft in drafts:
        if draft.removeprefix("v") in notes:
            raise ValueError(f"{draft} is a draft release of a failed run and has a notes file: delete the draft "
                             "release, then run all jobs again, so that the release keeps its version and name")
    named = sorted(parse_version(value) for value in notes if parse_version(value) > highest)
    if named:
        return ".".join(map(str, named[0]))
    major, minor, patch = highest
    version = f"{major}.{minor}.{patch + 1}"
    parse_version(version)
    return version


def notes_versions(folder: Path = NOTES_DIR) -> list[str]:
    """The versions that have a notes file in folder. Any other file in it is an error."""
    if not folder.is_dir():
        return []
    versions = []
    for path in sorted(folder.iterdir()):
        match = re.fullmatch(f"({STABLE_VERSION})\\.md", path.name)
        if not match:
            raise ValueError(f"{path.name} in {folder} is not a notes file named X.Y.Z.md")
        parse_version(match.group(1))
        release_notes(path)
        versions.append(match.group(1))
    return versions


def release_notes(path: Path) -> tuple[str, str]:
    """The title and the notes of a notes file: its first line "# Title", and the text below it."""
    text = path.read_text(encoding="utf-8")
    first, _, rest = text.partition("\n")
    title = first.removeprefix("# ").strip()
    if not first.startswith("# ") or not title:
        raise ValueError(f"{path.name} does not start with a line \"# Title\"")
    notes = rest.strip()
    if not notes:
        raise ValueError(f"{path.name} has a title but no notes")
    return title, notes + "\n"


def name_and_notes(tag: str, folder: Path = NOTES_DIR) -> tuple[str, str]:
    """The name and the notes of the release tag. A stable release with a notes file takes its title and notes."""
    name = f"EGTRAIN {tag}"
    path = folder / f"{tag.removeprefix('v')}.md"
    if "-" not in tag and path.is_file():
        title, notes = release_notes(path)
        return f"{name}: {title}", notes
    return name, DEFAULT_NOTES.format(name=name)


def main() -> None:
    if len(sys.argv) == 3 and sys.argv[1] in ("name", "notes"):
        name, notes = name_and_notes(sys.argv[2])
        print(name if sys.argv[1] == "name" else notes, end="\n" if sys.argv[1] == "name" else "")
        return
    cmake = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
    match = re.search(r'set\(EGTRAIN_VERSION "([0-9.]+)" CACHE STRING', cmake)
    if not match:
        raise SystemExit("Missing CMake application version baseline")
    tags = subprocess.check_output(["git", "tag", "--list"], cwd=ROOT, text=True).splitlines()
    notes = notes_versions()
    # Manual and pull-request runs validate the baseline without publishing.
    ref = os.environ["GITHUB_REF"] if os.environ["GITHUB_EVENT_NAME"] == "push" else ""
    drafts = []
    if ref == "refs/heads/production":
        # A draft may reserve a version before GitHub creates its Git tag.
        reserved = subprocess.check_output([
            "gh", "api", f"repos/{os.environ['GITHUB_REPOSITORY']}/releases",
            "--paginate", "--jq", ".[].tag_name",
        ], cwd=ROOT, text=True).splitlines()
        drafts = [tag for tag in reserved if tag not in tags]
        tags += reserved
    version = select_version(match.group(1), tags, ref, notes, drafts)
    with open(os.environ["GITHUB_OUTPUT"], "a", encoding="utf-8") as output:
        output.write(f"version={version}\n")
    print(f"Application version: {version}")
    if ref != "refs/heads/production":
        # What a push of this commit to production would publish, from the tags of the checkout, for a check before merging.
        upcoming = select_version(match.group(1), tags, "refs/heads/production", notes)
        print(f"A push to production would publish {name_and_notes('v' + upcoming)[0]}")


if __name__ == "__main__":
    main()
