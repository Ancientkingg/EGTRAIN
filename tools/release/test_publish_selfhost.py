#!/usr/bin/env python3
"""Tests of publish_selfhost.py and of the release page of tools/docs/home_page.py.

The tests run the script against two servers on 127.0.0.1: a stub of the publish API of the website, which also serves
what was published, and a stub of the upload API of Zipline 4.8.1 as its source describes it (POST /api/upload with the
token as the Authorization header, the name from x-zipline-filename and x-zipline-file-extension, 400 with code 1009 for a
name that exists, 429 with Retry-After, and GET /raw/<name>). The import tests replace the GitHub CLI with a stand-in
that has one release with the made-up files of the test.

With ZIPLINE_TEST_URL and ZIPLINE_TEST_TOKEN set, the release and import tests also run against that Zipline server, with
the stub of the website. Use a server of your own that you can throw away, for example:

  docker network create zipline-test
  docker run -d --name zipline-test-db --network zipline-test -e POSTGRES_USER=zipline \\
    -e POSTGRES_PASSWORD=test-only -e POSTGRES_DB=zipline postgres:16.15-alpine
  docker run -d --name zipline-test --network zipline-test -p 127.0.0.1:38411:3000 \\
    -e DATABASE_URL=postgres://zipline:test-only@zipline-test-db:5432/zipline \\
    -e CORE_SECRET=test-only-secret-of-at-least-32-characters ghcr.io/diced/zipline:4.8.1

then create the first user with POST /api/setup, a user with the role USER with POST /api/users, log in as that user with
POST /api/auth/login and read its token with GET /api/user/token. A user who is not an administrator meets the limit of
10 uploads a minute, so the test also waits for that limit once. Remove the two containers and the network afterwards.
"""
import http.server
import io
import json
import os
import shutil
import subprocess
import sys
import tarfile
import tempfile
import threading
import unittest
import urllib.parse
import urllib.request
from email.parser import BytesParser
from email.policy import HTTP
from pathlib import Path
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parent))
import build_release_assets  # noqa: E402
import publish_selfhost  # noqa: E402
import home_page  # noqa: E402  (publish_selfhost puts tools/docs on the path)

SITE_TOKEN = "made-up-site-token"
ZIPLINE_TOKEN = "made-up-zipline-token"
NAMES = ("EGTRAIN-1.2.3-macOS-arm64.dmg", "EGTRAIN-Portable-1.2.3-x64.zip", "Paimpol.egscene", "QEGTRAIN-linux-x86_64.AppImage",
         "update-manifest.json")
FIELDS = {"tag_name", "name", "body", "html_url", "draft", "prerelease", "published_at", "assets"}


class Server:
    """A server on 127.0.0.1 in a thread of its own. Each request goes to the method handle of the subclass."""

    def __init__(self, events: list):
        self.events = events
        owner = self

        class Handler(http.server.BaseHTTPRequestHandler):
            def log_message(self, *_arguments):
                pass

            def do_GET(self):
                owner.handle(self, "GET")

            def do_HEAD(self):
                owner.handle(self, "HEAD")

            def do_PUT(self):
                owner.handle(self, "PUT")

            def do_POST(self):
                owner.handle(self, "POST")

        self.server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), Handler)
        self.url = f"http://127.0.0.1:{self.server.server_address[1]}"
        self.thread = threading.Thread(target=self.server.serve_forever, args=(0.05,), daemon=True)
        self.thread.start()

    def close(self):
        self.server.shutdown()
        self.server.server_close()

    @staticmethod
    def reply(handler, status: int, body: bytes = b"", headers=None, method="GET"):
        handler.send_response(status)
        for key, value in (headers or {}).items():
            handler.send_header(key, value)
        handler.send_header("Content-Length", str(len(body)))
        handler.end_headers()
        if method != "HEAD":
            handler.wfile.write(body)


class SiteStub(Server):
    """The publish API: a folder in a gzip tar file replaces the folder, a file replaces the file. GET serves them."""

    def __init__(self, events: list, root: Path):
        self.root = root
        self.fail_feed = None
        self.redirect = False
        self.redirect_feed = False
        super().__init__(events)

    def handle(self, handler, method):
        path = urllib.parse.unquote(handler.path.split("?")[0]).lstrip("/")
        if method in ("GET", "HEAD"):
            if path == publish_selfhost.FEED_PATH and self.fail_feed:
                return self.reply(handler, self.fail_feed, b"broken", method=method)
            # A feed elsewhere that a client which follows the redirect would read.
            if path == publish_selfhost.FEED_PATH and self.redirect_feed:
                return self.reply(handler, 302, headers={"Location": f"{self.url}/moved/feed.json"}, method=method)
            target = self.root / path
            if target.is_dir():
                target = target / "index.html"
            return self.reply(handler, 200, target.read_bytes(), method=method) if target.is_file() else self.reply(handler, 404, method=method)
        body = handler.rfile.read(int(handler.headers.get("Content-Length", "0")))
        if handler.headers.get("Authorization") != f"Bearer {SITE_TOKEN}":
            return self.reply(handler, 401, b"bad token")
        if self.redirect:
            return self.reply(handler, 301, headers={"Location": self.url + "/elsewhere"})
        kind, _slash, name = path.removeprefix("api/publish/").partition("/")
        if method != "PUT" or kind not in ("dir", "file") or ".." in name.split("/"):
            return self.reply(handler, 400, b"bad request")
        self.events.append(f"site {kind} {name}")
        target = self.root / name
        if kind == "file":
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(body)
        else:
            shutil.rmtree(target, ignore_errors=True)
            target.mkdir(parents=True)
            with tarfile.open(fileobj=io.BytesIO(body), mode="r:gz") as archive:
                for member in archive.getmembers():
                    assert member.isfile() or member.isdir(), member.name
                    assert not member.name.startswith(("/", "./")) and ".." not in member.name.split("/"), member.name
                archive.extractall(target, filter="data")
        self.reply(handler, 204)


class ZiplineStub(Server):
    """POST /api/upload and GET /raw/<name> of Zipline 4.8.1 for one file a request."""

    def __init__(self, events: list):
        self.files = {}
        self.limited = 0
        self.truncate = False
        super().__init__(events)

    def handle(self, handler, method):
        path = urllib.parse.unquote(handler.path.split("?")[0])
        if method in ("GET", "HEAD") and path.startswith("/raw/"):
            name = path.removeprefix("/raw/")
            if name not in self.files:
                return self.reply(handler, 404, b"not found", method=method)
            return self.reply(handler, 200, self.files[name], method=method)
        if method != "POST" or path != "/api/upload":
            return self.reply(handler, 404)
        body = handler.rfile.read(int(handler.headers.get("Content-Length", "0")))
        if handler.headers.get("Authorization") != ZIPLINE_TOKEN:
            return self.reply(handler, 401, b'{"error":"E2001: Invalid token","code":2001,"statusCode":401}')
        if self.limited:
            self.limited -= 1
            return self.reply(handler, 429, b'{"error":"Rate limit exceeded","statusCode":429}', {"Retry-After": "7"})
        message = BytesParser(policy=HTTP).parsebytes(f"Content-Type: {handler.headers['Content-Type']}\r\n\r\n".encode() + body)
        parts = list(message.iter_parts())
        assert len(parts) == 1 and parts[0].get_param("name", header="content-disposition") == "file"
        assert handler.headers.get("x-zipline-deletes-at") == "never" and handler.headers.get("x-zipline-original-name") == "true"
        name = handler.headers["x-zipline-filename"] + handler.headers["x-zipline-file-extension"]
        if name in self.files:
            return self.reply(handler, 400, b'{"error":"E1009: file[0]: file with the same name already exists","code":1009,"statusCode":400}')
        data = parts[0].get_payload(decode=True)
        self.files[name] = data[:-1] if self.truncate else data
        self.events.append(f"zipline {name} as {parts[0].get_filename()}")
        self.reply(handler, 200, json.dumps({"files": [{"id": "x", "name": name, "type": "application/octet-stream",
                                                        "url": f"{self.url}/u/{name}"}]}).encode())


def fake_source_archive(commit, tag, folder):
    path = folder / home_page.source_archive_name(tag)
    path.write_bytes(f"source of {commit}".encode())
    return path


class PublishTests(unittest.TestCase):
    zipline_url = None
    zipline_token = ZIPLINE_TOKEN

    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
        self.events = []
        self.site = SiteStub(self.events, self.root / "site")
        (self.root / "site").mkdir()
        self.addCleanup(self.site.close)
        if self.zipline_url is None:
            self.zipline = ZiplineStub(self.events)
            self.addCleanup(self.zipline.close)
            self.zipline_url = self.zipline.url
        self.tag = "v1.2.3"
        self.assets = self.root / "release-assets"
        self.assets.mkdir()
        for index, name in enumerate(NAMES):
            (self.assets / name).write_bytes(name.encode() * (index + 1))
        environment = {"EGTRAIN_PUBLISH_TOKEN": SITE_TOKEN, "ZIPLINE_TOKEN": self.zipline_token,
                       "EGTRAIN_SITE_URL": self.site.url, "ZIPLINE_URL": self.zipline_url}
        patch = mock.patch.dict(os.environ, environment)
        patch.start()
        self.addCleanup(patch.stop)
        self.addCleanup(self.temp.cleanup)

    def release(self, archive=fake_source_archive, commit="0123abc", new_feed=True):
        arguments = ["release", "--tag", self.tag, "--name", f"EGTRAIN {self.tag}", "--prerelease", "false",
                     "--assets", str(self.assets), "--commit", commit, "--run-url", "https://example.org/runs/1"]
        with mock.patch.object(publish_selfhost, "source_archive", archive), mock.patch("sys.stdout", io.StringIO()):
            publish_selfhost.main(arguments + (["--new-feed"] if new_feed else []))

    def import_release(self, github: dict, archive=fake_source_archive, new_feed=True):
        """Runs import with a stand-in for the GitHub CLI that has the release github and the files of self.assets."""
        calls = []

        def gh(*arguments):
            calls.append(arguments)
            if arguments[:3] == ("release", "view", self.tag):
                return json.dumps(github)
            if arguments[:3] == ("release", "download", self.tag):
                for path in self.assets.iterdir():
                    shutil.copyfile(path, Path(arguments[arguments.index("--dir") + 1], path.name))
                return ""
            raise AssertionError(arguments)

        with mock.patch.object(publish_selfhost, "gh", gh), mock.patch.object(publish_selfhost, "source_archive", archive), \
                mock.patch.object(publish_selfhost, "tag_commit", lambda tag: f"commit of {tag}"), mock.patch("sys.stdout", io.StringIO()):
            publish_selfhost.main(["import", self.tag] + (["--new-feed"] if new_feed else []))
        return calls

    def github_release(self, **changes):
        return {"tagName": self.tag, "name": f"EGTRAIN {self.tag}", "body": "Notes from GitHub.\r\n\r\n- one\r\n- two\r\n",
                "isDraft": False, "isPrerelease": False, "publishedAt": "2026-10-01T12:00:00Z", **changes}

    def feed(self):
        return json.loads((self.root / "site/releases/feed.json").read_text(encoding="utf-8"))

    def zipline_get(self, name):
        with urllib.request.urlopen(f"{self.zipline_url}/raw/{name}") as response:
            return response.read(), response.headers

    def test_release_publishes_the_files_then_the_page_then_the_feed(self):
        self.release()
        feed = self.feed()
        self.assertEqual(len(feed), 1)
        entry = feed[0]
        self.assertEqual(set(entry), FIELDS)
        self.assertEqual((entry["tag_name"], entry["name"], entry["draft"], entry["prerelease"]), ("v1.2.3", "EGTRAIN v1.2.3", False, False))
        self.assertEqual(entry["html_url"], f"{self.site.url}/releases/1.2.3/")
        self.assertEqual(entry["body"], publish_selfhost.RELEASE_BODY.format(name="EGTRAIN v1.2.3", commit="0123abc",
                                                                            run_url="https://example.org/runs/1"))
        self.assertRegex(entry["published_at"], r"^\d{4}-\d\d-\d\dT\d\d:\d\d:\d\dZ$")
        self.assertEqual([asset["name"] for asset in entry["assets"]], sorted(NAMES))
        for asset in entry["assets"]:
            self.assertEqual(set(asset), {"name", "browser_download_url", "size"})
            self.assertEqual(asset["browser_download_url"], f"{self.zipline_url}/raw/egtrain-v1.2.3-{asset['name']}")
            data, headers = self.zipline_get(f"egtrain-v1.2.3-{asset['name']}")
            self.assertEqual(data, (self.assets / asset["name"]).read_bytes())
            self.assertEqual(asset["size"], len(data))
        source, _headers = self.zipline_get("egtrain-v1.2.3-EGTRAIN-1.2.3-source.tar.gz")
        self.assertEqual(source, b"source of 0123abc")
        page = (self.root / "site/releases/1.2.3/index.html").read_text(encoding="utf-8")
        self.assertIn("<h1>EGTRAIN v1.2.3</h1>", page)
        self.assertIn("<li>Commit: 0123abc</li>", page)
        self.assertIn(f'href="{self.zipline_url}/raw/egtrain-v1.2.3-EGTRAIN-1.2.3-source.tar.gz">EGTRAIN-1.2.3-source.tar.gz</a>', page)
        self.assertIn('href="../../1.2.3/">documentation of EGTRAIN 1.2.3</a>', page)
        # Every upload comes first, then the page, and the feed last.
        site_events = [event for event in self.events if event.startswith("site ")]
        self.assertEqual(site_events, ["site dir releases/1.2.3", f"site file {publish_selfhost.FEED_PATH}"])
        self.assertEqual(self.events[-2:], site_events)
        self.assertIn(f"zipline egtrain-v1.2.3-{NAMES[0]} as {NAMES[0]}", self.events)

    def test_a_second_run_adds_nothing_and_keeps_the_time(self):
        with mock.patch.object(publish_selfhost, "utc_now", return_value="2026-11-06T10:00:00Z"):
            self.release()
        uploads = [event for event in self.events if event.startswith("zipline ")]
        with mock.patch.object(publish_selfhost, "utc_now", return_value="2026-11-07T10:00:00Z"):
            self.release(new_feed=False)
        self.assertEqual([event for event in self.events if event.startswith("zipline ")], uploads)
        feed = self.feed()
        self.assertEqual([entry["tag_name"] for entry in feed], ["v1.2.3"])
        self.assertEqual(feed[0]["published_at"], "2026-11-06T10:00:00Z")

    def test_feed_newest_first_with_the_same_tag_replaced(self):
        old = {"tag_name": "v1.2.1", "name": "EGTRAIN v1.2.1", "published_at": "2026-01-01T00:00:00Z"}
        newer = {"tag_name": "v1.2.2", "name": "EGTRAIN v1.2.2", "published_at": "2026-02-01T00:00:00Z"}
        stale = {"tag_name": "v1.2.3", "name": "stale", "published_at": "2026-03-01T00:00:00Z"}
        (self.root / "site/releases").mkdir()
        (self.root / "site/releases/feed.json").write_text(json.dumps([newer, stale, old]), encoding="utf-8")
        # --new-feed only matters when the website has no feed.
        self.release(new_feed=True)
        feed = self.feed()
        self.assertEqual([entry["tag_name"] for entry in feed], ["v1.2.3", "v1.2.2", "v1.2.1"])
        self.assertEqual((feed[0]["name"], feed[0]["published_at"]), ("EGTRAIN v1.2.3", "2026-03-01T00:00:00Z"))
        self.assertEqual(feed[1:], [newer, old])

    def test_a_file_of_another_size_stops_before_the_feed(self):
        if not hasattr(self, "zipline"):
            self.skipTest("needs the stub")
        self.zipline.files[f"egtrain-v1.2.3-{NAMES[2]}"] = b"other"
        with self.assertRaises(SystemExit) as raised:
            self.release()
        self.assertIn("was there before this run", str(raised.exception))
        self.assertFalse((self.root / "site/releases").exists())

    def test_a_short_upload_stops_before_the_feed(self):
        if not hasattr(self, "zipline"):
            self.skipTest("needs the stub")
        self.zipline.truncate = True
        with self.assertRaises(SystemExit) as raised:
            self.release()
        self.assertIn("bytes, not the", str(raised.exception))
        self.assertFalse((self.root / "site/releases").exists())

    def test_waits_when_zipline_limits_the_uploads(self):
        if not hasattr(self, "zipline"):
            self.skipTest("needs the stub")
        self.zipline.limited = 2
        with mock.patch.object(publish_selfhost.time, "sleep") as sleep:
            self.release()
        self.assertEqual([call.args for call in sleep.call_args_list], [(7,), (7,)])
        self.assertEqual(len(self.feed()[0]["assets"]), len(NAMES))

    def test_a_feed_that_cannot_be_read_stops_before_publishing(self):
        self.site.fail_feed = 500
        with self.assertRaises(SystemExit) as raised:
            self.release()
        self.assertIn("HTTP 500", str(raised.exception))
        self.assertEqual([event for event in self.events if event.startswith("site ")], [])

    def test_a_missing_feed_stops_a_release_without_new_feed(self):
        with self.assertRaises(SystemExit) as raised:
            self.release(new_feed=False)
        self.assertIn("HTTP 404: the website has no feed", str(raised.exception))
        self.assertIn("--new-feed", str(raised.exception))
        self.assertEqual([event for event in self.events if event.startswith("site ")], [])

    def test_a_redirect_of_the_feed_is_not_followed(self):
        (self.root / "site/moved").mkdir()
        (self.root / "site/moved/feed.json").write_text("[]", encoding="utf-8")
        self.site.redirect_feed = True
        with self.assertRaises(SystemExit) as raised:
            self.release()
        self.assertIn("HTTP 302", str(raised.exception))
        self.assertEqual([event for event in self.events if event.startswith("site ")], [])

    def test_import_publishes_a_github_release(self):
        github = self.github_release()
        calls = self.import_release(github)
        self.assertEqual([call[:3] for call in calls], [("release", "view", self.tag), ("release", "download", self.tag)])
        version = self.tag[1:]
        entry = self.feed()[0]
        self.assertEqual(set(entry), FIELDS)
        self.assertEqual((entry["name"], entry["body"], entry["prerelease"], entry["published_at"], entry["html_url"]),
                         (github["name"], github["body"], False, github["publishedAt"], f"{self.site.url}/releases/{version}/"))
        self.assertEqual([asset["name"] for asset in entry["assets"]], sorted(path.name for path in self.assets.iterdir()))
        for asset in entry["assets"]:
            data, _headers = self.zipline_get(f"egtrain-{self.tag}-{asset['name']}")
            self.assertEqual(data, (self.assets / asset["name"]).read_bytes())
        page = (self.root / f"site/releases/{version}/index.html").read_text(encoding="utf-8")
        self.assertIn("<li>two</li>", page)
        self.assertIn(f"EGTRAIN-{version}-source.tar.gz", page)
        # A second run, now that the feed exists, uploads nothing and writes the same page and feed.
        uploads = [event for event in self.events if event.startswith("zipline ")]
        self.import_release(github, new_feed=False)
        self.assertEqual([event for event in self.events if event.startswith("zipline ")], uploads)
        self.assertEqual(self.feed(), [entry])
        self.assertEqual((self.root / f"site/releases/{version}/index.html").read_text(encoding="utf-8"), page)

    def test_import_refuses_a_draft_and_a_missing_feed(self):
        with self.assertRaises(SystemExit) as raised:
            self.import_release(self.github_release(isDraft=True))
        self.assertIn("not a published release", str(raised.exception))
        with self.assertRaises(SystemExit) as raised:
            self.import_release(self.github_release(), new_feed=False)
        self.assertIn("HTTP 404: the website has no feed", str(raised.exception))
        self.assertEqual([event for event in self.events if event.startswith("site ")], [])

    def test_a_refused_token_is_not_printed(self):
        os.environ["EGTRAIN_PUBLISH_TOKEN"] = "wrong-made-up-token"
        with self.assertRaises(SystemExit) as raised:
            self.release()
        self.assertIn("HTTP 401", str(raised.exception))
        self.assertNotIn("wrong-made-up-token", str(raised.exception))

    def test_missing_tokens_stop_before_any_request(self):
        del os.environ["ZIPLINE_TOKEN"]
        with self.assertRaises(SystemExit) as raised:
            self.release()
        self.assertIn("ZIPLINE_TOKEN is not set", str(raised.exception))
        self.assertEqual(self.events, [])

    def test_redirects_are_not_followed(self):
        self.site.redirect = True
        with self.assertRaises(SystemExit) as raised:
            self.release()
        self.assertIn("HTTP 301", str(raised.exception))
        self.assertFalse((self.root / "site/releases").exists())

    def test_notes_replace_the_body_of_one_release(self):
        self.release()
        before = self.feed()[0]
        notes = self.root / "notes.md"
        notes.write_text("Corrected notes with a [link](https://example.org/a).\n\n- one\n- two\n", encoding="utf-8")
        with mock.patch("sys.stdout", io.StringIO()):
            publish_selfhost.main(["notes", "v1.2.3", str(notes)])
        after = self.feed()[0]
        self.assertEqual(after["body"], notes.read_text(encoding="utf-8"))
        self.assertEqual({key: value for key, value in after.items() if key != "body"}, {key: value for key, value in before.items() if key != "body"})
        page = (self.root / "site/releases/1.2.3/index.html").read_text(encoding="utf-8")
        self.assertIn('<a href="https://example.org/a">link</a>', page)
        self.assertIn("<li>two</li>", page)
        self.assertIn("EGTRAIN-1.2.3-source.tar.gz", page)
        with self.assertRaises(SystemExit):
            publish_selfhost.main(["notes", "v9.9.9", str(notes)])

    def test_a_prerelease_has_no_documentation_link(self):
        arguments = ["release", "--tag", "v1.2.3-rc1", "--name", "EGTRAIN v1.2.3-rc1", "--prerelease", "true",
                     "--assets", str(self.assets), "--commit", "0123abc", "--run-url", "https://example.org/runs/1", "--new-feed"]
        with mock.patch.object(publish_selfhost, "source_archive", fake_source_archive), mock.patch("sys.stdout", io.StringIO()):
            publish_selfhost.main(arguments)
        entry = self.feed()[0]
        self.assertEqual((entry["prerelease"], entry["html_url"]), (True, f"{self.site.url}/releases/1.2.3-rc1/"))
        page = (self.root / "site/releases/1.2.3-rc1/index.html").read_text(encoding="utf-8")
        self.assertIn("This is a prerelease", page)
        self.assertNotIn("documentation of EGTRAIN", page)
        self.assertIsNone(home_page.newest_release(self.feed()))


class StubOnly(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.events = []
        self.site = SiteStub(self.events, self.root / "site")
        (self.root / "site").mkdir()
        self.addCleanup(self.site.close)
        patch = mock.patch.dict(os.environ, {"EGTRAIN_PUBLISH_TOKEN": SITE_TOKEN, "EGTRAIN_SITE_URL": self.site.url})
        patch.start()
        self.addCleanup(patch.stop)

    def pages(self, folders=("dev", "latest", "1.2.3"), name="pages"):
        pages = self.root / name
        for folder in folders:
            (pages / folder / "guides").mkdir(parents=True)
            (pages / folder / "index.html").write_text(f"docs of {folder}", encoding="utf-8")
            (pages / folder / "guides" / "install.html").write_text(f"install of {folder}", encoding="utf-8")
        home_page.write_home_page(pages, [])
        return pages

    def site_feed(self, *tags):
        """Puts a feed with the releases tags, newest first, on the website. A tag with a hyphen is a prerelease."""
        feed = []
        for index, tag in enumerate(tags):
            names = [template.format(version=tag[1:]) for _key, template, _folder in build_release_assets.PACKAGES]
            names += [scene + build_release_assets.SCENE_SUFFIX for scene in build_release_assets.SCENES]
            assets = [{"name": name, "browser_download_url": f"https://bin.example.org/raw/egtrain-{tag}-{name}", "size": 1}
                      for name in names + [build_release_assets.MANIFEST_NAME]]
            feed.append({"tag_name": tag, "name": f"EGTRAIN {tag}", "body": "", "html_url": f"{self.site.url}/releases/{tag[1:]}/",
                         "draft": False, "prerelease": "-" in tag, "published_at": f"2026-0{9 - index}-01T00:00:00Z", "assets": assets})
        (self.root / "site/releases").mkdir(exist_ok=True)
        (self.root / "site/releases/feed.json").write_text(json.dumps(feed), encoding="utf-8")

    def publish_site(self, pages, *folders):
        with mock.patch("sys.stdout", io.StringIO()):
            publish_selfhost.main(["site", str(pages), *folders])

    def test_site_publishes_the_folders_then_the_pages_then_the_home_page(self):
        pages = self.pages()
        self.publish_site(pages, "1.2.3", "latest")
        self.assertEqual(self.events, ["site dir 1.2.3", "site dir latest", "site dir assets", "site dir support", "site dir source",
                                       "site dir privacy", "site file index.html"])
        for name in ("1.2.3/guides/install.html", "latest/index.html"):
            self.assertEqual((self.root / "site" / name).read_bytes(), (pages / name).read_bytes(), name)
        for name in ("assets/site.css", "support/index.html", "privacy/index.html"):
            self.assertTrue((self.root / "site" / name).is_file(), name)
        self.assertFalse((self.root / "site/dev").exists())

    def test_site_pages_come_from_the_feed_whatever_the_copy(self):
        # A run of main whose copy of gh-pages is older than the release, and the run of the release, publish the same
        # pages, in either order.
        self.site_feed("v1.2.4-rc.1", "v1.2.3", "v1.2.0")
        newer, older = self.pages(), self.pages(("dev", "latest", "1.2.0"), "older")
        published = []
        for pages, folders in ((newer, ("1.2.3", "latest")), (older, ("dev",)), (newer, ())):
            self.publish_site(pages, *folders)
            published.append([(self.root / "site" / name).read_bytes() for name in ("index.html", "source/index.html")])
        self.assertEqual(published[0], published[1])
        self.assertEqual(published[0], published[2])
        text = published[0][0].decode("utf-8")
        listed = [text.find(f'<li><a href="{name}/">') for name in ("latest", "dev", "1.2.3", "1.2.0")]
        self.assertTrue(-1 not in listed and listed == sorted(listed), listed)
        self.assertNotIn('href="1.2.4-rc.1/"', text)
        self.assertIn("https://bin.example.org/raw/egtrain-v1.2.3-EGTRAIN-1.2.3-macOS-arm64.dmg", text)
        self.assertIn("egtrain-v1.2.4-rc.1-EGTRAIN-1.2.4-rc.1-source.tar.gz", published[0][1].decode("utf-8"))

    def test_site_without_a_feed_lists_dev_only(self):
        self.publish_site(self.pages())
        text = (self.root / "site/index.html").read_text(encoding="utf-8")
        self.assertIn('<li><a href="dev/">', text)
        self.assertNotIn("latest/", text)
        self.assertNotIn('href="1.2.3/"', text)
        self.assertIn("No release is published on this site yet", text)

    def test_site_does_not_follow_a_redirect_of_the_feed(self):
        (self.root / "site/moved").mkdir()
        (self.root / "site/moved/feed.json").write_text("[]", encoding="utf-8")
        self.site.redirect_feed = True
        with self.assertRaises(SystemExit) as raised:
            publish_selfhost.main(["site", str(self.pages())])
        self.assertIn("HTTP 302", str(raised.exception))
        self.assertEqual(self.events, [])

    def test_site_refuses_links_and_other_names(self):
        pages = self.pages()
        for folders in ([".."], ["a/b"], ["latest", "."]):
            with self.assertRaises(SystemExit):
                publish_selfhost.main(["site", str(pages), *folders])
        try:
            (pages / "latest" / "link.html").symlink_to(pages / "latest" / "index.html")
        except OSError:
            self.skipTest("this system makes no symbolic link")
        with self.assertRaises(SystemExit):
            publish_selfhost.main(["site", str(pages), "latest"])
        self.assertEqual(self.events, [])

    def test_the_body_limit_holds_before_sending(self):
        pages = self.pages()
        with mock.patch.object(publish_selfhost, "MAX_PUBLISH_BYTES", 100), self.assertRaises(SystemExit) as raised:
            publish_selfhost.main(["site", str(pages), "1.2.3"])
        self.assertIn("at most 100 bytes", str(raised.exception))
        self.assertEqual(self.events, [])

    def test_release_tags(self):
        for tag in ("v1.2.3", "v1.2.3-rc.1", "v0.0.0-beta_2"):
            publish_selfhost.check_tag(tag)
        for tag in ("1.2.3", "v1.2", "v01.2.3", "v1.2.3/x", "v1.2.3 ", "v1.2.3-r c", "v1.2.3?"):
            with self.assertRaises(SystemExit, msg=tag):
                publish_selfhost.check_tag(tag)

    def test_source_archive_of_a_commit(self):
        if not (publish_selfhost.ROOT / ".git").exists():
            self.skipTest("the source code is not a Git repository")
        commit = subprocess.run(["git", "rev-parse", "HEAD"], cwd=publish_selfhost.ROOT, capture_output=True, text=True, check=True).stdout.strip()
        path = publish_selfhost.source_archive(commit, "v1.2.3", self.root)
        self.assertEqual(path.name, "EGTRAIN-1.2.3-source.tar.gz")
        with tarfile.open(path) as archive:
            names = archive.getnames()
        self.assertIn("EGTRAIN-1.2.3/LICENSE", names)
        self.assertTrue(all(name.split("/")[0] == "EGTRAIN-1.2.3" for name in names))

    def test_release_body_matches_the_github_release(self):
        workflow = (publish_selfhost.ROOT / ".github/workflows/release.yml").read_text(encoding="utf-8")
        body = publish_selfhost.RELEASE_BODY.format(name="${{ steps.release-meta.outputs.name }}", commit="${{ github.sha }}",
                                                    run_url="${{ github.server_url }}/${{ github.repository }}/actions/runs/${{ github.run_id }}")
        self.assertIn("body: |\n" + "".join(f"            {line}\n" if line else "\n" for line in body.splitlines()), workflow)

    def test_markdown_of_release_notes(self):
        text = "Line one\nline <two> & `code <x>`\n\n- item **bold**\n  continued\n* other https://example.org/a.\n\n## Heading\n"
        self.assertEqual(home_page.markdown_html(text),
                         "<p>Line one<br>\nline &lt;two&gt; &amp; <code>code &lt;x&gt;</code></p>\n"
                         "<ul>\n<li>item <strong>bold</strong> continued</li>\n"
                         '<li>other <a href="https://example.org/a">https://example.org/a</a>.</li>\n</ul>\n'
                         "<h3>Heading</h3>")
        self.assertNotIn("<script", home_page.markdown_html("<script>alert(1)</script> [x](javascript:alert(1))"))
        self.assertNotIn('href="javascript', home_page.markdown_html("[x](javascript:alert(1))"))

    def test_feed_of_404_is_none(self):
        self.assertIsNone(home_page.read_feed(f"{self.site.url}/{publish_selfhost.FEED_PATH}"))
        (self.root / "site/releases").mkdir()
        (self.root / "site/releases/feed.json").write_text('{"not": "a list"}', encoding="utf-8")
        with self.assertRaises(SystemExit):
            home_page.read_feed(f"{self.site.url}/{publish_selfhost.FEED_PATH}")


@unittest.skipUnless(os.environ.get("ZIPLINE_TEST_URL") and os.environ.get("ZIPLINE_TEST_TOKEN"), "no Zipline test server")
class RealZiplineTests(PublishTests):
    """The release tests against a real Zipline server. Each test publishes under a tag of its own."""

    zipline_url = os.environ.get("ZIPLINE_TEST_URL", "").rstrip("/")
    zipline_token = os.environ.get("ZIPLINE_TEST_TOKEN", "")
    serial = 0

    def setUp(self):
        super().setUp()
        RealZiplineTests.serial += 1
        self.tag = f"v1.2.{RealZiplineTests.serial + int(os.environ.get('ZIPLINE_TEST_FIRST', '100'))}"
        for path in list(self.assets.iterdir()):
            path.rename(self.assets / path.name.replace("1.2.3", self.tag[1:]))

    def files_of_user(self):
        request = urllib.request.Request(f"{self.zipline_url}/api/user/files?page=1&perpage=1000",
                                         headers={"Authorization": self.zipline_token})
        with urllib.request.urlopen(request) as response:
            return [file["name"] for file in json.load(response)["page"]]

    def test_release_publishes_the_files_then_the_page_then_the_feed(self):
        self.release(publish_selfhost.source_archive, "HEAD")
        version = self.tag[1:]
        entry = self.feed()[0]
        for asset in entry["assets"]:
            data, headers = self.zipline_get(f"egtrain-{self.tag}-{asset['name']}")
            self.assertEqual(data, (self.assets / asset["name"]).read_bytes())
            self.assertEqual(headers["Content-Disposition"], f"filename*=utf-8''{asset['name']}")
        names = [name for name in self.files_of_user() if name.startswith(f"egtrain-{self.tag}-")]
        self.assertEqual(sorted(names), sorted([f"egtrain-{self.tag}-{asset['name']}" for asset in entry["assets"]]
                                               + [f"egtrain-{self.tag}-EGTRAIN-{version}-source.tar.gz"]))
        self.assertEqual(self.events[-2:], [f"site dir releases/{version}", f"site file {publish_selfhost.FEED_PATH}"])

    def test_a_second_run_adds_nothing_and_keeps_the_time(self):
        self.release()
        before = self.files_of_user()
        self.release()
        self.assertEqual(sorted(self.files_of_user()), sorted(before))
        self.assertEqual([entry["tag_name"] for entry in self.feed()], [self.tag])

    def test_feed_newest_first_with_the_same_tag_replaced(self):
        self.skipTest("covered by the stub")

    def test_a_refused_token_is_not_printed(self):
        self.skipTest("covered by the stub")

    def test_missing_tokens_stop_before_any_request(self):
        self.skipTest("covered by the stub")

    def test_redirects_are_not_followed(self):
        self.skipTest("covered by the stub")

    def test_a_feed_that_cannot_be_read_stops_before_publishing(self):
        self.skipTest("covered by the stub")

    def test_notes_replace_the_body_of_one_release(self):
        self.skipTest("covered by the stub")

    def test_a_prerelease_has_no_documentation_link(self):
        self.skipTest("covered by the stub")

    def test_a_missing_feed_stops_a_release_without_new_feed(self):
        self.skipTest("covered by the stub")

    def test_a_redirect_of_the_feed_is_not_followed(self):
        self.skipTest("covered by the stub")

    def test_import_refuses_a_draft_and_a_missing_feed(self):
        self.skipTest("covered by the stub")


if __name__ == "__main__":
    unittest.main()
