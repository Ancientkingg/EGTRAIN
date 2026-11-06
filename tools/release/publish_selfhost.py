#!/usr/bin/env python3
"""Publishes a release and the documentation site on egtrain.samuelbruin.com, and the release files on bin.samuelbruin.com.

usage: publish_selfhost.py release --tag TAG --name NAME --prerelease true|false --assets DIR --commit SHA --run-url URL
       publish_selfhost.py notes TAG FILE
       publish_selfhost.py site PAGES [DIR ...]

release uploads every file of DIR, the folder that build_release_assets.py writes, and an archive of the source code of
the commit SHA (git archive, EGTRAIN-<version>-source.tar.gz with the folder EGTRAIN-<version>/ in it) to the Zipline
server bin.samuelbruin.com. Each file gets the name egtrain-TAG-<name> there, so that it is served at
https://bin.samuelbruin.com/raw/egtrain-TAG-<name>, and is checked by downloading it from that address and comparing the
number of bytes with the file. A name that Zipline already has is not uploaded again when the file there has the size of
the file here, so a second run of the same release adds nothing; another size stops the script. The release then gets its
page, releases/<version>/index.html, and its entry in the update feed releases/feed.json, newest release first, in place
of an entry with the same tag. The feed is written last, after every file and the page are in place. The entry has the
fields of a GitHub release that EGTRAIN reads, and its notes are the text of the GitHub release that release.yml creates.
<version> is TAG without its leading v. A second run keeps the publication time of the entry.

notes replaces the release notes of the release TAG in the feed and on its page with the Markdown text of FILE, for a
correction after the release is published.

site publishes the folders DIR of PAGES, the copy of the documentation site that docs.yml builds, as the folders of the
same name of the website, and then the pages that tools/docs/home_page.py writes into PAGES: assets/, support/, source/,
privacy/ and, last, index.html.

The website takes a folder as a gzip-compressed tar file in PUT /api/publish/dir/<folder>, which replaces the folder, and
a file in PUT /api/publish/file/<path>, both with the bearer token of the environment variable EGTRAIN_PUBLISH_TOKEN.
Zipline takes the files in POST /api/upload with its API token, from the environment variable ZIPLINE_TOKEN, as the value
of the Authorization header. EGTRAIN_SITE_URL and ZIPLINE_URL replace the two addresses, for tests. The script follows no
redirect and prints no token.
"""
import argparse
import datetime
import io
import json
import os
import re
import subprocess
import sys
import tarfile
import tempfile
import time
import urllib.error
import urllib.parse
import urllib.request
import uuid
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools/docs"))
import home_page  # noqa: E402
from version import parse_version  # noqa: E402

SITE_URL = "https://egtrain.samuelbruin.com"
ZIPLINE_URL = "https://bin.samuelbruin.com"
FEED_PATH = "releases/feed.json"
NAME = re.compile(r"[A-Za-z0-9._-]+")
# Zipline 4.8.1 takes a file of up to 95 MB in one request (its default chunks.max) and Cloudflare a body of up to 100 MB.
MAX_UPLOAD_BYTES = 95 * 1024 * 1024
# The publish API of the website takes a body of up to 90 MB.
MAX_PUBLISH_BYTES = 90 * 1000 * 1000
# Zipline answers 429 with Retry-After when a user uploads more than 10 files a minute.
RATE_LIMIT_ATTEMPTS = 6
MAX_RETRY_SECONDS = 120
TIMEOUT_SECONDS = 300
CHUNK_BYTES = 1024 * 1024

# The text of a release, the same as the body of the GitHub release in .github/workflows/release.yml.
RELEASE_BODY = """Automated EGTRAIN build for `{name}`.

- Commit: {commit}
- Workflow run: {run_url}

Download the package for your platform. The bundled cases are also available as `.egscene` files.
"""


class NoRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, *_arguments):
        return None


OPENER = urllib.request.build_opener(NoRedirect)


def request(method: str, url: str, data=None, headers=None, stream=False):
    """Sends one request and returns the response, or the HTTPError for a status of 300 or more. A failed connection
    stops the script."""
    headers = {"User-Agent": home_page.USER_AGENT, **(headers or {})}
    try:
        return OPENER.open(urllib.request.Request(url, data=data, headers=headers, method=method), timeout=TIMEOUT_SECONDS)
    except urllib.error.HTTPError as error:
        return error
    except (urllib.error.URLError, OSError) as error:
        raise SystemExit(f"{method} {url} failed: {error}")


def failure(method: str, url: str, response) -> SystemExit:
    body = response.read(2000).decode("utf-8", "replace").strip()
    return SystemExit(f"{method} {url} answered HTTP {response.status}{': ' + body if body else ''}")


def token(variable: str) -> str:
    value = os.environ.get(variable, "")
    if not value:
        raise SystemExit(f"the environment variable {variable} is not set")
    return value


def check_tag(tag: str) -> str:
    """The version of the website paths of a release tag: v and a stable X.Y.Z version, with a suffix for a prerelease."""
    if not NAME.fullmatch(tag) or not tag.startswith("v"):
        raise SystemExit(f"{tag!r} is not a release tag: v, a version, and only letters, digits, '.', '_' and '-'")
    try:
        parse_version(tag[1:].split("-", 1)[0])
    except ValueError as error:
        raise SystemExit(f"{tag!r} is not a release tag: {error}")
    return home_page.release_version(tag)


# Zipline


def raw_url(zipline: str, name: str) -> str:
    return f"{zipline}/raw/{urllib.parse.quote(name)}"


def served_size(zipline: str, name: str):
    """The number of bytes that Zipline serves for the file, which this reads to its end, or None when it has no such file."""
    url = raw_url(zipline, name)
    response = request("GET", url)
    if response.status == 404:
        response.close()
        return None
    if response.status != 200:
        raise failure("GET", url, response)
    size = 0
    with response:
        for chunk in iter(lambda: response.read(CHUNK_BYTES), b""):
            size += len(chunk)
    return size


def upload(zipline: str, zipline_token: str, path: Path, name: str, original: str) -> bool:
    """Uploads the file as name, the name of the download being original. False when Zipline has a file of that name."""
    stem, _dot, extension = name.rpartition(".")
    if not stem or not extension:
        raise SystemExit(f"{name} has no extension, which Zipline needs to keep the name")
    boundary = uuid.uuid4().hex
    content_type = "application/json" if extension == "json" else "application/octet-stream"
    body = b"".join((
        f'--{boundary}\r\nContent-Disposition: form-data; name="file"; filename="{original}"\r\n'.encode(),
        f"Content-Type: {content_type}\r\n\r\n".encode(),
        path.read_bytes(),
        f"\r\n--{boundary}--\r\n".encode(),
    ))
    headers = {
        "Authorization": zipline_token,
        "Content-Type": f"multipart/form-data; boundary={boundary}",
        # Zipline stores the name and the extension that these give, refuses a name that it has, keeps the file without
        # an expiry and names the download after the original name.
        "x-zipline-filename": stem,
        "x-zipline-file-extension": "." + extension,
        "x-zipline-deletes-at": "never",
        "x-zipline-original-name": "true",
    }
    url = f"{zipline}/api/upload"
    for attempt in range(RATE_LIMIT_ATTEMPTS):
        response = request("POST", url, body, headers)
        if response.status != 429 or attempt == RATE_LIMIT_ATTEMPTS - 1:
            break
        wait = response.headers.get("Retry-After", "")
        wait = min(int(wait), MAX_RETRY_SECONDS) if wait.isdigit() else 60
        print(f"Zipline limits the uploads: waiting {wait} s", flush=True)
        response.close()
        time.sleep(wait)
    with response:
        data = response.read()
    if response.status == 400:
        try:
            error = json.loads(data)
        except ValueError:
            error = {}
        if error.get("code") == 1009 and "already exists" in str(error.get("error")):
            return False
    if response.status != 200:
        raise SystemExit(f"POST {url} for {name} answered HTTP {response.status}: {data[:2000].decode('utf-8', 'replace')}")
    try:
        stored = [file.get("name") for file in json.loads(data)["files"]]
    except (ValueError, KeyError, TypeError, AttributeError):
        raise SystemExit(f"POST {url} for {name} answered with no list of files: {data[:2000].decode('utf-8', 'replace')}")
    if stored != [name]:
        raise SystemExit(f"Zipline stored {name} as {stored}")
    return True


def publish_file(zipline: str, zipline_token: str, path: Path, name: str, original: str) -> int:
    """Puts the file on Zipline under name unless it is there already, checks the size that Zipline serves and returns it."""
    size = path.stat().st_size
    if not 1 <= size <= MAX_UPLOAD_BYTES:
        raise SystemExit(f"{path} is {size} bytes; Zipline takes 1 to {MAX_UPLOAD_BYTES} bytes in one request")
    uploaded = upload(zipline, zipline_token, path, name, original)
    served = served_size(zipline, name)
    if served != size:
        problem = "is missing after the upload" if served is None else f"has {served} bytes, not the {size} bytes of {path}"
        hint = "" if uploaded else ". The file was there before this run: delete it on Zipline, or publish a new version"
        raise SystemExit(f"{raw_url(zipline, name)} {problem}{hint}")
    print(f"{name}  {size} bytes  {'uploaded' if uploaded else 'already there'}", flush=True)
    return size


# The website


def publish(site: str, site_token: str, kind: str, path: str, data, size: int, content_type: str) -> None:
    if size > MAX_PUBLISH_BYTES:
        raise SystemExit(f"{path} is {size} bytes; the website takes at most {MAX_PUBLISH_BYTES} bytes")
    url = f"{site}/api/publish/{kind}/{urllib.parse.quote(path)}"
    headers = {"Authorization": f"Bearer {site_token}", "Content-Type": content_type, "Content-Length": str(size)}
    response = request("PUT", url, data, headers)
    with response:
        if not 200 <= response.status < 300:
            raise failure("PUT", url, response)
    print(f"published {path} ({size} bytes)", flush=True)


def publish_text(site: str, site_token: str, path: str, text: str, content_type: str) -> None:
    data = text.encode("utf-8")
    publish(site, site_token, "file", path, data, len(data), content_type)


def publish_directory(site: str, site_token: str, name: str, folder: Path) -> None:
    """Publishes the folder as the folder name of the website, in a gzip-compressed tar file of its files and folders."""
    if not folder.is_dir():
        raise SystemExit(f"{folder} is not a folder")
    with tempfile.TemporaryFile() as stream:
        with tarfile.open(fileobj=stream, mode="w:gz") as archive:
            for directory, folders, files in os.walk(folder):
                folders.sort()
                for entry in sorted(folders) + sorted(files):
                    path = Path(directory, entry)
                    if path.is_symlink() or not (path.is_dir() or path.is_file()):
                        raise SystemExit(f"{path} is a link or not a regular file, which the website does not take")
                    info = archive.gettarinfo(path, path.relative_to(folder).as_posix())
                    info.uid = info.gid = 0
                    info.uname = info.gname = ""
                    if info.isdir():
                        archive.addfile(info)
                    else:
                        with open(path, "rb") as source:
                            archive.addfile(info, source)
        size = stream.tell()
        stream.seek(0)
        publish(site, site_token, "dir", name, stream, size, "application/gzip")


# Releases


def source_archive(commit: str, tag: str, folder: Path) -> Path:
    """Writes the archive of the source code of the commit into folder."""
    name = home_page.source_archive_name(tag)
    prefix = name.removesuffix("-source.tar.gz") + "/"
    subprocess.run(["git", "archive", "--format=tar.gz", f"--prefix={prefix}", "-o", str(folder / name), commit],
                   cwd=ROOT, check=True)
    return folder / name


def release_assets(folder: Path) -> list:
    files = sorted(path for path in folder.iterdir()) if folder.is_dir() else []
    if not files:
        raise SystemExit(f"{folder} holds no release file")
    for path in files:
        if not NAME.fullmatch(path.name) or path.is_symlink() or not path.is_file():
            raise SystemExit(f"{path} is not a regular file with a name of letters, digits, '.', '_' and '-'")
    return files


def utc_now() -> str:
    return datetime.datetime.now(datetime.timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")


def updated_feed(feed: list, entry: dict) -> list:
    """The feed with the entry in place of the release of the same tag, newest publication first."""
    entries = [old for old in feed if old.get("tag_name") != entry["tag_name"]] + [entry]
    return sorted(entries, key=lambda old: str(old.get("published_at", "")), reverse=True)


def publish_release_page(site: str, site_token: str, entry: dict, source: tuple) -> None:
    folder = f"releases/{home_page.release_version(entry['tag_name'])}"
    with tempfile.TemporaryDirectory() as temporary:
        Path(temporary, "index.html").write_text(home_page.release_page(entry, source), encoding="utf-8")
        publish_directory(site, site_token, folder, Path(temporary))


def publish_feed(site: str, site_token: str, feed: list) -> None:
    publish_text(site, site_token, FEED_PATH, json.dumps(feed, indent=2) + "\n", "application/json")


def publish_release(site: str, zipline: str, arguments) -> dict:
    tag, version = arguments.tag, check_tag(arguments.tag)
    site_token, zipline_token = token("EGTRAIN_PUBLISH_TOKEN"), token("ZIPLINE_TOKEN")
    assets = release_assets(arguments.assets)
    with tempfile.TemporaryDirectory() as temporary:
        source = source_archive(arguments.commit, tag, Path(temporary))
        listed = []
        for path in assets:
            name = home_page.zipline_name(tag, path.name)
            size = publish_file(zipline, zipline_token, path, name, path.name)
            listed.append({"name": path.name, "browser_download_url": raw_url(zipline, name), "size": size})
        source_name = home_page.zipline_name(tag, source.name)
        source_size = publish_file(zipline, zipline_token, source, source_name, source.name)
    feed = home_page.read_feed(f"{site}/{FEED_PATH}")
    published = next((old.get("published_at") for old in feed if old.get("tag_name") == tag), None)
    entry = {
        "tag_name": tag,
        "name": arguments.name,
        "body": RELEASE_BODY.format(name=arguments.name, commit=arguments.commit, run_url=arguments.run_url),
        "html_url": f"{site}/releases/{version}/",
        "draft": False,
        "prerelease": arguments.prerelease == "true",
        "published_at": published or utc_now(),
        "assets": listed,
    }
    publish_release_page(site, site_token, entry, (source.name, raw_url(zipline, source_name), source_size))
    publish_feed(site, site_token, updated_feed(feed, entry))
    print(f"OK: {tag} is published at {entry['html_url']}")
    return entry


def publish_notes(site: str, zipline: str, arguments) -> dict:
    check_tag(arguments.tag)
    site_token = token("EGTRAIN_PUBLISH_TOKEN")
    body = arguments.file.read_text(encoding="utf-8")
    feed = home_page.read_feed(f"{site}/{FEED_PATH}")
    entry = next((old for old in feed if old.get("tag_name") == arguments.tag), None)
    if entry is None:
        raise SystemExit(f"the feed has no release {arguments.tag}")
    entry["body"] = body
    source = home_page.source_archive_name(arguments.tag)
    source_name = home_page.zipline_name(arguments.tag, source)
    response = request("HEAD", raw_url(zipline, source_name))
    with response:
        if response.status != 200:
            raise failure("HEAD", raw_url(zipline, source_name), response)
        source_size = int(response.headers.get("Content-Length", "0"))
    publish_release_page(site, site_token, entry, (source, raw_url(zipline, source_name), source_size))
    publish_feed(site, site_token, feed)
    print(f"OK: the notes of {arguments.tag} are replaced")
    return entry


def publish_site(site: str, arguments) -> None:
    site_token = token("EGTRAIN_PUBLISH_TOKEN")
    pages = arguments.pages
    for name in arguments.dirs:
        if not NAME.fullmatch(name) or name in (".", ".."):
            raise SystemExit(f"{name!r} is not the name of a folder of the site")
        publish_directory(site, site_token, name, pages / name)
    for name in ("assets", *home_page.SITE_PAGES):
        publish_directory(site, site_token, name, pages / name)
    page = (pages / "index.html").read_bytes()
    publish(site, site_token, "file", "index.html", page, len(page), "text/html; charset=utf-8")


def main(argv=None) -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    commands = parser.add_subparsers(dest="command", required=True)
    release = commands.add_parser("release", help="publish the files, the page and the feed entry of a release")
    release.add_argument("--tag", required=True)
    release.add_argument("--name", required=True)
    release.add_argument("--prerelease", required=True, choices=("true", "false"))
    release.add_argument("--assets", required=True, type=Path)
    release.add_argument("--commit", required=True)
    release.add_argument("--run-url", required=True)
    notes = commands.add_parser("notes", help="replace the release notes of a published release")
    notes.add_argument("tag")
    notes.add_argument("file", type=Path)
    site = commands.add_parser("site", help="publish folders of the documentation site and the pages of the website")
    site.add_argument("pages", type=Path)
    site.add_argument("dirs", nargs="*")
    arguments = parser.parse_args(argv)
    site_url = os.environ.get("EGTRAIN_SITE_URL", SITE_URL).rstrip("/")
    zipline_url = os.environ.get("ZIPLINE_URL", ZIPLINE_URL).rstrip("/")
    if arguments.command == "release":
        publish_release(site_url, zipline_url, arguments)
    elif arguments.command == "notes":
        publish_notes(site_url, zipline_url, arguments)
    else:
        publish_site(site_url, arguments)


if __name__ == "__main__":
    main()
