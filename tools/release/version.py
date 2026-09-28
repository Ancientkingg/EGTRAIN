#!/usr/bin/env python3
"""Choose one application version before building release packages."""

import os
import re
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
STABLE_VERSION = r"(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)"


def parse_version(value: str) -> tuple[int, int, int]:
    if not re.fullmatch(STABLE_VERSION, value):
        raise ValueError(f"Invalid application version: {value}")
    version = tuple(map(int, value.split(".")))
    if any(component > 65535 for component in version):
        raise ValueError("Version components must fit Windows version resources")
    return version


def select_version(base: str, tags: list[str], ref: str) -> str:
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
    major, minor, patch = max(versions)
    version = f"{major}.{minor}.{patch + 1}"
    parse_version(version)
    return version


def main() -> None:
    cmake = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
    match = re.search(r'set\(EGTRAIN_VERSION "([0-9.]+)" CACHE STRING', cmake)
    if not match:
        raise SystemExit("Missing CMake application version baseline")
    tags = subprocess.check_output(["git", "tag", "--list"], cwd=ROOT, text=True).splitlines()
    # Manual and pull-request runs validate the baseline without publishing.
    ref = os.environ["GITHUB_REF"] if os.environ["GITHUB_EVENT_NAME"] == "push" else ""
    if ref == "refs/heads/production":
        # A draft may reserve a version before GitHub creates its Git tag.
        tags += subprocess.check_output([
            "gh", "api", f"repos/{os.environ['GITHUB_REPOSITORY']}/releases",
            "--paginate", "--jq", ".[].tag_name",
        ], cwd=ROOT, text=True).splitlines()
    version = select_version(match.group(1), tags, ref)
    with open(os.environ["GITHUB_OUTPUT"], "a", encoding="utf-8") as output:
        output.write(f"version={version}\n")
    print(f"Application version: {version}")


if __name__ == "__main__":
    main()
