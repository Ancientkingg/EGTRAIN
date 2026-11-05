#!/usr/bin/env python3
"""Writes the home page of the documentation site into a copy of the gh-pages branch.

usage: home_page.py PAGES

PAGES holds the published site: dev/, latest/ and a folder X.Y.Z for each released version. The script writes
PAGES/index.html from docs/site/index.html and replaces PAGES/assets/ with docs/site/site.css and the picture of the
README. In the page, {version} stands for the newest version folder and {versions} for the list of the published
folders. Without a version folder, the parts between <!-- release --> and <!-- /release --> are left out, since they
link the files and the documentation of a release. The other files of PAGES, such as a CNAME file, stay as they are.

To look at the page, run the script on a folder that holds one version folder, such as $TMPDIR/pages/1.0.5, and open
$TMPDIR/pages/index.html.
"""
import argparse
import re
import shutil
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
PAGE = ROOT / "docs/site/index.html"
ASSETS = (ROOT / "docs/site/site.css", ROOT / "docs/images/readme-main-window.png")
VERSION = re.compile(r"\d+\.\d+\.\d+")
RELEASE_PART = re.compile(r"<!-- release -->.*?<!-- /release -->", re.DOTALL)


def published_versions(pages: Path) -> list:
    """The version folders of the site, newest first."""
    names = [path.name for path in pages.iterdir() if path.is_dir() and VERSION.fullmatch(path.name)]
    return sorted(names, key=lambda name: [int(part) for part in name.split(".")], reverse=True)


def write_home_page(pages: Path) -> None:
    versions = published_versions(pages)
    items = [f'<li><a href="{name}/">{name}</a>{note}</li>'
             for name, note in (("latest", ": the newest release"), ("dev", ": the main branch")) if (pages / name).is_dir()]
    items += [f'<li><a href="{name}/">{name}</a></li>' for name in versions]
    text = PAGE.read_text(encoding="utf-8")
    text = text.replace("{version}", versions[0]) if versions else RELEASE_PART.sub("", text)
    (pages / "index.html").write_text(text.replace("{versions}", "\n".join(items)), encoding="utf-8")
    assets = pages / "assets"
    shutil.rmtree(assets, ignore_errors=True)
    assets.mkdir()
    for path in ASSETS:
        shutil.copyfile(path, assets / path.name)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("pages", type=Path, help="the root of the copy of the gh-pages branch")
    write_home_page(parser.parse_args().pages)
