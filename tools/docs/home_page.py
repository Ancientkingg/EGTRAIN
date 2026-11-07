#!/usr/bin/env python3
"""Writes the pages of the website at the root of a copy of the published documentation site.

usage: home_page.py PAGES [--feed URL]

PAGES holds the published site: dev/, latest/ and a folder X.Y.Z for each released version. The script writes
PAGES/index.html, the home page, and the pages support/, source/ and privacy/, each as index.html in a folder of its own,
and replaces PAGES/assets/ with the font Archivo with its licence and the picture of the README. Every page is a part of
docs/site/ put into docs/site/layout.html, with the style docs/site/site.css in its head. The other files of PAGES, such
as the version folders or a CNAME file, stay as they are.

--feed names the list of releases that the website publishes, newest first, in the form of the update feed that
tools/release/publish_selfhost.py writes (https://egtrain.samuelbruin.com/releases/feed.json by default). The script
follows no redirect. A feed that does not exist yet is an empty list, and any other failure to read it stops the script.
The home page offers the files of the newest release that is neither a draft nor a prerelease, with the links and sizes
of the feed, and has a documentation table with a column for latest/, dev/ and each version folder. The source page
lists every release with its source archive. Without such a release, the parts between <!-- release --> and
<!-- /release --> are left out and those between <!-- no release --> and <!-- /no release --> are kept. The parts
between <!-- latest --> and <!-- /latest --> link latest/, so they are kept only when PAGES has that folder.

In a part, {name} stands for a value that the script fills in, {asset:NAME} for the download link of the release file
NAME and {size:NAME} for its size. A file that the release does not have stops the script. The train graph and the
playback of a run on the home page take their values from docs/site/home-data.json, which tools/docs/home_data.py
writes from a run of the bundled case Milano_Brescia.

To look at the pages, run the script on a folder that holds a version folder, such as $TMPDIR/pages/1.0.5, with
--feed file:///path/to/feed.json, and open $TMPDIR/pages/index.html.
"""
import argparse
import html
import json
import re
import shutil
import urllib.error
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SITE = ROOT / "docs/site"
LAYOUT = SITE / "layout.html"
STYLE = SITE / "site.css"
PAGE = SITE / "index.html"
DATA = SITE / "home-data.json"
ASSETS = (SITE / "fonts/archivo-latin-wdth-wght.woff2", SITE / "fonts/Archivo-OFL.txt", ROOT / "docs/images/readme-main-window.png")
DESCRIPTION = ("EGTRAIN is a desktop application for microscopic railway simulation. Download it for macOS, Windows or Linux, "
               "and read the documentation of every version.")
# The rows of the documentation table of the home page: the value that holds the links of each, its name and the page
# that it links in each folder.
DOCUMENTATION = (("docs_guides", "User guides", ""), ("docs_developer", "Developer pages", "development/getting-started.html"),
                 ("docs_reference", "C++ reference", "api/index.html"))
# The pages besides the home page: the folder that each is written to, its title and its description.
SITE_PAGES = {
    "support": ("Support", "How to report a problem with EGTRAIN and where its documentation is."),
    "source": ("Source code", "The source code of EGTRAIN and the source archive of each release."),
    "privacy": ("Privacy", "Which data EGTRAIN and its website receive and how long they are kept."),
}
FEED_URL = "https://egtrain.samuelbruin.com/releases/feed.json"
SOURCE_URL = "https://bin.samuelbruin.com/raw/"
USER_AGENT = "EGTRAIN-site-publisher"
VERSION = re.compile(r"\d+\.\d+\.\d+")
PLACEHOLDER = re.compile(r"\{([a-z_]+)\}")
ASSET = re.compile(r"\{asset:([A-Za-z0-9._-]+)\}")
SIZE = re.compile(r"\{size:([A-Za-z0-9._-]+)\}")


def published_versions(pages: Path) -> list:
    """The version folders of the site, newest first."""
    names = [path.name for path in pages.iterdir() if path.is_dir() and VERSION.fullmatch(path.name)]
    return sorted(names, key=lambda name: [int(part) for part in name.split(".")], reverse=True)


class NoRedirect(urllib.request.HTTPRedirectHandler):
    """Turns a redirect into an error, which is the answer of the server."""

    def redirect_request(self, *_arguments):
        return None


OPENER = urllib.request.build_opener(NoRedirect)


def read_feed(url: str):
    """The releases of the feed at url, or None when the server answers 404."""
    request = urllib.request.Request(url, headers={"User-Agent": USER_AGENT, "Cache-Control": "no-cache"})
    try:
        with OPENER.open(request, timeout=60) as response:
            data = response.read()
    except urllib.error.HTTPError as error:
        error.close()
        if error.code == 404:
            return None
        raise SystemExit(f"cannot read the release feed {url}: HTTP {error.code}")
    except (urllib.error.URLError, OSError) as error:
        raise SystemExit(f"cannot read the release feed {url}: {error}")
    try:
        feed = json.loads(data)
    except ValueError as error:
        raise SystemExit(f"the release feed {url} is not JSON: {error}")
    if not isinstance(feed, list) or not all(isinstance(entry, dict) and isinstance(entry.get("tag_name"), str) for entry in feed):
        raise SystemExit(f"the release feed {url} is not a list of releases")
    return feed


def feed_versions(feed: list) -> list:
    """The versions of the stable releases of the feed, newest first. The documentation workflow publishes a folder for
    each of them on the website."""
    names = {release_version(entry["tag_name"]) for entry in feed if not entry.get("draft") and not entry.get("prerelease")}
    names = [name for name in names if VERSION.fullmatch(name)]
    return sorted(names, key=lambda name: [int(part) for part in name.split(".")], reverse=True)


def newest_release(feed: list):
    """The first release of the feed that is neither a draft nor a prerelease, or None."""
    return next((entry for entry in feed if not entry.get("draft") and not entry.get("prerelease")), None)


def release_version(tag: str) -> str:
    """The version of a release in the paths of the website: its tag without the leading v."""
    return tag[1:]


def source_archive_name(tag: str) -> str:
    """The name of the source archive of a release."""
    return f"EGTRAIN-{release_version(tag)}-source.tar.gz"


def zipline_name(tag: str, name: str) -> str:
    """The name of a release file on bin.samuelbruin.com, which serves it at /raw/ under that name."""
    return f"egtrain-{tag}-{name}"


def keep(text: str, part: str, wanted: bool) -> str:
    """Keeps the text between <!-- part --> and <!-- /part --> without the two comments, or leaves it all out."""
    pattern = re.compile(rf"[ \t]*<!-- {part} -->\n?(.*?)[ \t]*<!-- /{part} -->\n?", re.DOTALL)
    return pattern.sub(lambda match: match.group(1) if wanted else "", text)


def fill(text: str, values: dict) -> str:
    """Puts the values in place of their {name}. The values are not read again, so they may hold braces."""
    return PLACEHOLDER.sub(lambda match: values[match.group(1)], text)


def render(main: str, title: str, description: str, root: str, kind: str = "text") -> str:
    """A whole page: the part main in the layout. root leads from the folder of the page to the root of the site, and kind
    is the class of the page: home for the home page, text for the others."""
    style = fill(STYLE.read_text(encoding="utf-8"), {"root": root}).rstrip("\n")
    values = {"title": html.escape(title), "description": html.escape(description), "root": root, "kind": kind, "style": style}
    return fill(LAYOUT.read_text(encoding="utf-8"), {**values, "main": main.rstrip("\n")})


def human_size(size: int) -> str:
    for unit, scale in (("MB", 1000 ** 2), ("KB", 1000)):
        if size >= scale:
            return f"{size / scale:.1f} {unit}"
    return f"{size} bytes"


INLINE = re.compile(r"`([^`]+)`|\*\*(.+?)\*\*|\[([^\]]+)\]\((https?://[^\s)]+)\)|(https?://[^\s<>]*[^\s<>.,;:!?)'\"])")


def inline_html(text: str) -> str:
    """Code spans, bold text, links and bare http and https addresses of one line of Markdown, the rest escaped."""
    parts, position = [], 0
    for match in INLINE.finditer(text):
        code, bold, label, url, bare = match.groups()
        parts.append(html.escape(text[position:match.start()]))
        if code is not None:
            parts.append(f"<code>{html.escape(code)}</code>")
        elif bold is not None:
            parts.append(f"<strong>{html.escape(bold)}</strong>")
        elif label is not None:
            parts.append(f'<a href="{html.escape(url)}">{html.escape(label)}</a>')
        else:
            parts.append(f'<a href="{html.escape(bare)}">{html.escape(bare)}</a>')
        position = match.end()
    parts.append(html.escape(text[position:]))
    return "".join(parts)


def markdown_html(text: str) -> str:
    """The HTML of release notes in Markdown: paragraphs whose lines break as written, lists of lines that start with -,
    * or +, headings and the inline forms of inline_html. Anything else is text."""
    blocks = []
    for block in re.split(r"\n[ \t]*\n", text.strip().replace("\r\n", "\n")):
        lines, items = [], []

        def close():
            if lines:
                blocks.append("<p>" + "<br>\n".join(inline_html(line) for line in lines) + "</p>")
                lines.clear()
            if items:
                blocks.append("<ul>\n" + "\n".join(f"<li>{inline_html(item)}</li>" for item in items) + "\n</ul>")
                items.clear()

        for line in block.split("\n"):
            item = re.match(r"\s{0,3}[-*+]\s+(.*)", line)
            heading = re.match(r"#{1,6}\s+(.*?)\s*#*\s*$", line)
            if item:
                if lines:
                    close()
                items.append(item.group(1))
            elif items and line[:1] in (" ", "\t"):
                items[-1] += " " + line.strip()
            elif heading:
                close()
                blocks.append(f"<h3>{inline_html(heading.group(1))}</h3>")
            else:
                if items:
                    close()
                lines.append(line.strip())
        close()
    return "\n".join(blocks)


def release_page(release: dict, source: tuple) -> str:
    """The page of a release: its notes, its files and the source archive. source is the name, the link and the size of
    that archive."""
    version = release_version(release["tag_name"])
    rows = [(asset["name"], asset["browser_download_url"], asset["size"]) for asset in release["assets"]] + [source]
    files = "\n".join(f'          <tr><td><a href="{html.escape(url)}">{html.escape(name)}</a></td><td>{human_size(size)}</td></tr>'
                      for name, url, size in rows)
    stable = VERSION.fullmatch(version) is not None
    main = keep(keep(SITE.joinpath("release.html").read_text(encoding="utf-8"), "prerelease", not stable), "documentation", stable)
    main = fill(main, {
        "name": html.escape(release["name"]),
        "date": html.escape(release["published_at"][:10]),
        "notes": markdown_html(release["body"]),
        "files": files,
        "version": html.escape(version),
        "root": "../../",
    })
    return render(main, release["name"], f"The release notes and files of {release['name']}.", "../../")


def home_page(latest: bool, dev: bool, versions: list, release) -> str:
    """The home page. Its documentation table has a column for latest/ and dev/ when they are there and for each version
    folder."""
    version = release_version(release["tag_name"]) if release is not None else None
    columns = [(name, note) for name, note, shown in (("latest", "newest release", latest), ("dev", "main branch", dev)) if shown]
    columns += [(name, "this release" if name == version else "") for name in versions]
    main = keep(PAGE.read_text(encoding="utf-8"), "latest", latest)
    main = keep(keep(main, "release", release is not None), "no release", release is None)
    values = json.loads(DATA.read_text(encoding="utf-8"))
    values["docs"] = f"{columns[0][0]}/" if columns else "dev/"
    values["docs_head"] = "".join(f'<th scope="col"><b>{name}</b>{note}</th>' for name, note in columns)
    for key, label, page in DOCUMENTATION:
        values[key] = "".join(f'<td><a href="{name}/{page}" aria-label="{label}, {name}">{name}</a></td>' for name, _note in columns)
    if release is not None:
        values.update(version=html.escape(version), release_page=html.escape(release["html_url"]),
                      published=html.escape(release["published_at"][:10]))
    main = fill(main, values)
    if release is not None:
        files = {asset["name"]: asset for asset in release["assets"]}

        def asset(match):
            if match.group(1) not in files:
                raise SystemExit(f"docs/site/index.html links {match.group(1)}, which release {release['tag_name']} does not have")
            return files[match.group(1)]

        main = ASSET.sub(lambda match: html.escape(asset(match)["browser_download_url"]), main)
        main = SIZE.sub(lambda match: human_size(asset(match)["size"]), main)
    return render(main, "EGTRAIN", DESCRIPTION, "./", "home")


def source_page(feed: list) -> str:
    releases = [entry for entry in feed if not entry.get("draft")]
    items = []
    for entry in releases:
        tag = entry["tag_name"]
        archive = source_archive_name(tag)
        items.append(f'        <li>{html.escape(entry["name"])}, {html.escape(entry.get("published_at", "")[:10])}: '
                     f'<a href="{html.escape(SOURCE_URL + zipline_name(tag, archive))}">{html.escape(archive)}</a> '
                     f'(<a href="{html.escape(entry["html_url"])}">release page</a>)</li>')
    main = keep(keep(SITE.joinpath("source.html").read_text(encoding="utf-8"), "releases", bool(releases)), "no releases", not releases)
    return fill(main, {"releases": "\n".join(items), "root": "../"})


def write_home_page(pages: Path, feed: list, from_feed: bool = False) -> None:
    """Writes the home page, the other pages and assets/ into pages, from the releases of feed. The home page lists the
    folders latest/ and dev/ and the version folders that pages has. With from_feed it lists the folders that the website
    has by its feed alone instead: dev/, and latest/ and the versions of feed_versions when the feed has a stable release.
    Every run that reads the same feed then writes the same pages, whatever copy of the site it has."""
    if from_feed:
        versions = feed_versions(feed)
        documentation = (bool(versions), True, versions)
    else:
        documentation = ((pages / "latest").is_dir(), (pages / "dev").is_dir(), published_versions(pages))
    (pages / "index.html").write_text(home_page(*documentation, newest_release(feed)), encoding="utf-8")
    for folder, (title, description) in SITE_PAGES.items():
        main = source_page(feed) if folder == "source" else fill(SITE.joinpath(f"{folder}.html").read_text(encoding="utf-8"), {"root": "../"})
        shutil.rmtree(pages / folder, ignore_errors=True)
        (pages / folder).mkdir()
        (pages / folder / "index.html").write_text(render(main, f"{title} - EGTRAIN", description, "../"), encoding="utf-8")
    assets = pages / "assets"
    shutil.rmtree(assets, ignore_errors=True)
    assets.mkdir()
    for path in ASSETS:
        shutil.copyfile(path, assets / path.name)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("pages", type=Path, help="the root of the copy of the published site")
    parser.add_argument("--feed", default=FEED_URL, help="the address of the release feed")
    arguments = parser.parse_args()
    write_home_page(arguments.pages, read_feed(arguments.feed) or [])
