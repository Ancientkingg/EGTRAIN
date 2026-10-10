#!/usr/bin/env python3
import contextlib
import filecmp
import io
import os
import plistlib
import re
import stat
import subprocess
import sys
import tempfile
import time
import unittest
from pathlib import Path
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parent))
import make_dmg

RELEASE_INFO = Path(__file__).resolve().parents[2] / "EGTRAIN" / "QEGTRAIN" / "update" / "ReleaseInfo.cpp"
TOOL_TIMEOUT = 120
IMAGE_BYTES = b"image bytes"
NOTICE = "hdiutil: WARNING: 'hdiutil create -srcfolder ...' is deprecated.\n"


def make_app(root, name="QEGTRAIN.app"):
    """The smallest tree that check_app accepts."""
    app = root / name
    (app / "Contents" / "MacOS").mkdir(parents=True)
    (app / "Contents" / "Info.plist").write_text("<plist/>", encoding="utf-8")
    return app


class FakeTools:
    """Stands in for ditto and hdiutil: records every call and answers it without running a program."""

    def __init__(self, failing=None, timing_out=None, stderr=""):
        self.failing = failing
        self.timing_out = timing_out
        self.stderr = stderr
        self.calls = []
        self.staged = None

    def run(self, command, **kwargs):
        self.calls.append((list(command), kwargs))
        name = command[0]
        if name == "hdiutil":
            self.staged = sorted(os.listdir(command[command.index("-srcfolder") + 1]))
            Path(command[-1]).write_bytes(IMAGE_BYTES)
        if name == self.timing_out:
            raise subprocess.TimeoutExpired(command, kwargs["timeout"])
        status = 1 if name == self.failing else 0
        return subprocess.CompletedProcess(command, status, stdout="", stderr=self.stderr)


@contextlib.contextmanager
def fake_environment(tools, which=lambda name: name):
    """No group A test starts a tool or makes a link, whatever the platform has installed."""
    with mock.patch("shutil.which", side_effect=which), \
            mock.patch.object(make_dmg.subprocess, "run", side_effect=tools.run), \
            mock.patch("os.symlink") as symlink:
        yield symlink


class DiskImageInputTests(unittest.TestCase):
    """The file name, the input rules and the commands; these need no macOS tool."""

    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.app = make_app(self.root)
        self.output = self.root / "out"
        self.target = self.output / "EGTRAIN-1.2.3-macOS-arm64.dmg"
        self.tools = FakeTools()

    def build(self, app=None, output=None, version="1.2.3", **fakes):
        tools = FakeTools(**fakes) if fakes else self.tools
        self.tools = tools
        with fake_environment(tools) as self.symlink:
            return make_dmg.build_dmg(app or self.app, output or self.output, version)

    def assertRefused(self, part, **arguments):
        with self.assertRaises(SystemExit) as raised:
            self.build(**arguments)
        self.assertIn(part, str(raised.exception))
        self.assertEqual(self.tools.calls, [])

    def assertCommands(self):
        """Both commands and the link are as promised. Returns the staging folder."""
        ditto, hdiutil = self.tools.calls
        stage = Path(ditto[0][-1]).parent
        self.assertEqual(ditto[0], ["ditto", str(self.app), str(stage / "QEGTRAIN.app")])
        self.assertEqual(hdiutil[0], ["hdiutil", "create", "-volname", "EGTRAIN", "-srcfolder", str(stage), "-fs", "HFS+",
                                      "-format", "UDZO", str(self.target)])
        for _command, arguments in self.tools.calls:
            self.assertEqual(arguments.get("timeout"), TOOL_TIMEOUT)
            self.assertNotIn("shell", arguments)
        self.assertEqual(self.symlink.call_count, 1)
        self.assertEqual([str(argument) for argument in self.symlink.call_args[0]], ["/Applications", str(stage / "Applications")])
        # The fake ditto and the mocked link create nothing, so the script added nothing else.
        self.assertEqual(self.tools.staged, [])
        return stage

    def test_name_holds_the_version(self):
        self.assertEqual(make_dmg.dmg_name("1.2.3"), "EGTRAIN-1.2.3-macOS-arm64.dmg")
        self.assertEqual(make_dmg.dmg_name("1.10.0"), "EGTRAIN-1.10.0-macOS-arm64.dmg")

    def test_name_is_one_the_application_accepts(self):
        source = RELEASE_INFO.read_text(encoding="utf-8")
        patterns = re.findall(r'\{\s*"macos-arm64"\s*,\s*"([^"]+)"\s*\}', source)
        self.assertGreaterEqual(len(patterns), 2, "the macos-arm64 entries of kAssetPatterns were not found")
        for version in ("1.2.3", "1.10.0"):
            self.assertIn(make_dmg.dmg_name(version), [pattern.replace("%1", version) for pattern in patterns])

    def test_version_must_be_stable(self):
        for version in ("1.0", "v1.0.0", "01.0.0", "1.0.0-rc.1"):
            with self.subTest(version=version):
                self.assertRefused(version, version=version)
                self.assertFalse(self.output.exists())
                with fake_environment(self.tools):
                    with self.assertRaises(SystemExit) as raised:
                        make_dmg.main(["--version", version, "--app", str(self.app), "--output", str(self.output)])
                self.assertIn(version, str(raised.exception))
                self.assertEqual(self.tools.calls, [])
                self.assertFalse(self.output.exists())

    def test_app_must_be_a_bundle(self):
        regular_file = self.root / "file" / "QEGTRAIN.app"
        regular_file.parent.mkdir()
        regular_file.write_text("not a bundle", encoding="utf-8")
        no_plist = make_app(self.root / "no_plist")
        (no_plist / "Contents" / "Info.plist").unlink()
        no_macos = make_app(self.root / "no_macos")
        (no_macos / "Contents" / "MacOS").rmdir()
        wrong_plist = make_app(self.root / "wrong_plist")
        (wrong_plist / "Contents" / "Info.plist").unlink()
        (wrong_plist / "Contents" / "Info.plist").mkdir()
        other = make_app(self.root / "other", "Other.app")
        for app in (self.root / "missing" / "QEGTRAIN.app", regular_file, other, no_plist, no_macos, wrong_plist):
            with self.subTest(app=str(app)):
                with self.assertRaises(SystemExit) as raised:
                    make_dmg.check_app(app)
                self.assertIn(str(app), str(raised.exception))
                self.assertRefused(str(app), app=app)
                self.assertFalse(self.output.exists())

    def test_minimal_bundle_is_accepted(self):
        make_dmg.check_app(self.app)
        make_dmg.check_app(str(self.app))

    def test_existing_target_is_kept(self):
        self.output.mkdir()
        self.target.write_bytes(b"earlier image")
        self.assertRefused(str(self.target))
        self.assertEqual(self.target.read_bytes(), b"earlier image")
        self.assertEqual(os.listdir(self.output), [self.target.name])

    def test_output_that_is_a_file_is_kept(self):
        self.output.write_bytes(b"not a folder")
        self.assertRefused(str(self.output))
        self.assertEqual(self.output.read_bytes(), b"not a folder")

    def test_missing_tool_is_reported_before_anything_is_created(self):
        for missing in ("ditto", "hdiutil"):
            with self.subTest(missing=missing):
                tools = FakeTools()
                with fake_environment(tools, which=lambda name, missing=missing: None if name == missing else name) as symlink:
                    with self.assertRaises(SystemExit) as raised:
                        make_dmg.build_dmg(self.app, self.output, "1.2.3")
                message = str(raised.exception)
                self.assertIn(missing, message)
                self.assertIn("macOS", message)
                self.assertEqual(tools.calls, [])
                symlink.assert_not_called()
                self.assertFalse(self.output.exists())

    def test_commands_and_link_of_a_successful_build(self):
        self.assertEqual(self.build(stderr=NOTICE), self.target)
        self.assertEqual(self.target.read_bytes(), IMAGE_BYTES)
        stage = self.assertCommands()
        self.assertFalse(stage.exists())

    def test_failing_command_removes_the_partial_image_and_the_stage(self):
        with self.assertRaises(SystemExit) as raised:
            self.build(failing="hdiutil", stderr="boom")
        self.assertIn("hdiutil", str(raised.exception))
        self.assertIn("boom", str(raised.exception))
        stage = self.assertCommands()
        self.assertFalse(self.target.exists())
        self.assertFalse(stage.exists())

    def test_timed_out_command_removes_the_partial_image_and_the_stage(self):
        with self.assertRaises(SystemExit) as raised:
            self.build(timing_out="hdiutil")
        self.assertIn("hdiutil", str(raised.exception))
        self.assertIn(str(TOOL_TIMEOUT), str(raised.exception))
        stage = self.assertCommands()
        self.assertFalse(self.target.exists())
        self.assertFalse(stage.exists())

    def test_failing_ditto_stops_before_hdiutil(self):
        with self.assertRaises(SystemExit) as raised:
            self.build(failing="ditto", stderr="boom")
        self.assertIn("ditto", str(raised.exception))
        self.assertIn("boom", str(raised.exception))
        self.assertEqual([command[0] for command, _arguments in self.tools.calls], ["ditto"])
        self.assertFalse(self.target.exists())
        self.assertFalse(Path(self.tools.calls[0][0][-1]).parent.exists())

    def test_main_prints_the_name_with_its_size(self):
        output = io.StringIO()
        with fake_environment(self.tools), contextlib.redirect_stdout(output):
            make_dmg.main(["--version", "1.2.3", "--app", str(self.app), "--output", str(self.output)])
        self.assertEqual(output.getvalue().splitlines(), [f"{self.target.name}  {len(IMAGE_BYTES)} bytes", f"OK: {self.target}"])


def compare_trees(source, copy):
    """The differences between two trees: relative paths, kinds of entries, bytes of files, targets of links and
    permission bits of files and directories. An empty list means the trees are equal."""
    source, copy = Path(source), Path(copy)

    def entries(root):
        found = {".": root}
        for directory, directories, names in os.walk(root):
            for name in directories + names:
                path = Path(directory, name)
                found[path.relative_to(root).as_posix()] = path
        return found

    def kind(path):
        return "link" if path.is_symlink() else "directory" if path.is_dir() else "file"

    def mode(path):
        return stat.S_IMODE(os.stat(path).st_mode)

    left, right = entries(source), entries(copy)
    problems = [f"{name}: only in the source" for name in sorted(set(left) - set(right))]
    problems += [f"{name}: only in the copy" for name in sorted(set(right) - set(left))]
    for name in sorted(set(left) & set(right)):
        before, after = left[name], right[name]
        if kind(before) != kind(after):
            problems.append(f"{name}: a {kind(before)} in the source, a {kind(after)} in the copy")
        elif kind(before) == "link":
            if os.readlink(before) != os.readlink(after):
                problems.append(f"{name}: the link leads to {os.readlink(before)} in the source, to {os.readlink(after)} in the copy")
        else:
            if kind(before) == "file" and not filecmp.cmp(before, after, shallow=False):
                problems.append(f"{name}: the bytes differ")
            if mode(before) != mode(after):
                problems.append(f"{name}: the mode is {mode(before):o} in the source, {mode(after):o} in the copy")
    return problems


def run_tool(command):
    """Runs a tool without a shell, judges it by its exit status and returns what it wrote to standard output."""
    try:
        result = subprocess.run([str(part) for part in command], capture_output=True, text=True, timeout=TOOL_TIMEOUT)
    except subprocess.TimeoutExpired:
        raise AssertionError(f"{' '.join(map(str, command))} did not finish in {TOOL_TIMEOUT} seconds")
    if result.returncode != 0:
        raise AssertionError(f"{' '.join(map(str, command))} exited with {result.returncode}\n{result.stderr}")
    return result.stdout


def check_signature(app):
    run_tool(["codesign", "--verify", "--deep", "--strict", app])


def build_fixture(root):
    """A signed bundle with a program that exits with 0, a data file and a link inside the bundle."""
    app = root / "QEGTRAIN.app"
    resources = app / "Contents" / "Resources"
    (app / "Contents" / "MacOS").mkdir(parents=True)
    (resources / "Scenes" / "Fixture").mkdir(parents=True)
    info = {"CFBundleExecutable": "QEGTRAIN", "CFBundleIdentifier": "com.egtrain.simulator", "CFBundlePackageType": "APPL",
            "CFBundleName": "EGTRAIN", "CFBundleShortVersionString": "1.2.3", "CFBundleVersion": "1"}
    with open(app / "Contents" / "Info.plist", "wb") as stream:
        plistlib.dump(info, stream)
    source = root / "fixture.c"
    source.write_text("int main(void) { return 0; }\n", encoding="utf-8")
    run_tool(["cc", source, "-o", app / "Contents" / "MacOS" / "QEGTRAIN"])
    (resources / "Scenes" / "Fixture" / "scene.json").write_text('{"name": "Fixture"}\n', encoding="utf-8")
    os.symlink("Scenes", resources / "FixtureLink")
    run_tool(["codesign", "--force", "--deep", "--sign", "-", app])
    check_signature(app)
    return app


def detach(mountpoint):
    """Detaches the image at the mountpoint if it is mounted, and forces it after five attempts."""
    if not os.path.ismount(mountpoint):
        return
    for _attempt in range(5):
        result = subprocess.run(["hdiutil", "detach", str(mountpoint)], capture_output=True, text=True, timeout=TOOL_TIMEOUT)
        if result.returncode == 0:
            return
        time.sleep(1)
    run_tool(["hdiutil", "detach", "-force", mountpoint])


@unittest.skipUnless(sys.platform == "darwin", "the disk image can only be built and mounted on macOS")
class DiskImageTests(unittest.TestCase):
    """Builds a real image of a signed fixture bundle and reads it back. A Mac that builds EGTRAIN has cc, codesign,
    ditto and hdiutil, so a missing tool fails these tests instead of skipping them."""

    @classmethod
    def setUpClass(cls):
        # Class cleanups run in reverse order, also when this method raises: the image is detached before its folder goes.
        temp = tempfile.TemporaryDirectory()
        cls.addClassCleanup(temp.cleanup)
        root = Path(temp.name)
        cls.fixture = build_fixture(root / "fixture")
        try:
            cls.dmg = make_dmg.build_dmg(cls.fixture, root / "image", "1.2.3")
        except SystemExit as error:
            # SystemExit would end the run without the class cleanups.
            raise AssertionError(f"make_dmg ended while building the image: {error}") from error
        cls.mount = root / "mnt"
        cls.addClassCleanup(detach, cls.mount)
        run_tool(["hdiutil", "attach", "-readonly", "-nobrowse", "-noautoopen", "-noverify", "-mountpoint", cls.mount, cls.dmg])
        cls.mounted_app = cls.mount / "QEGTRAIN.app"
        # What dragging the app to the Applications folder does.
        cls.copy = root / "copy" / "QEGTRAIN.app"
        cls.copy.parent.mkdir()
        run_tool(["ditto", cls.mounted_app, cls.copy])
        cls.root = root

    def test_image_has_the_promised_name_format_and_volume(self):
        self.assertEqual(self.dmg.name, "EGTRAIN-1.2.3-macOS-arm64.dmg")
        self.assertGreater(self.dmg.stat().st_size, 0)
        image = plistlib.loads(run_tool(["hdiutil", "imageinfo", "-plist", self.dmg]).encode("utf-8"))
        self.assertEqual(image["Format"], "UDZO")
        volume = plistlib.loads(run_tool(["diskutil", "info", "-plist", self.mount]).encode("utf-8"))
        self.assertEqual(volume["VolumeName"], "EGTRAIN")

    def test_volume_holds_the_app_and_the_link_only(self):
        self.assertEqual(sorted(name for name in os.listdir(self.mount) if not name.startswith(".")), ["Applications", "QEGTRAIN.app"])
        link = self.mount / "Applications"
        self.assertTrue(link.is_symlink())
        self.assertEqual(os.readlink(link), "/Applications")
        for directory, directories, names in os.walk(self.mount):
            for name in names + directories:
                self.assertFalse(name.endswith(".egscene"), f"{Path(directory, name)} is a scene bundle")
            if "Scenes" in directories:
                self.assertIn(self.mounted_app, Path(directory, "Scenes").parents, "a Scenes folder outside the app")

    def test_app_in_the_image_equals_the_fixture(self):
        self.assertEqual(compare_trees(self.fixture, self.mounted_app), [])
        self.assertEqual(compare_trees(self.fixture, self.copy), [])

    def test_signature_is_valid_in_the_image_in_the_copy_and_on_the_source(self):
        check_signature(self.mounted_app)
        check_signature(self.copy)
        check_signature(self.fixture)

    def test_copy_runs(self):
        run_tool([self.copy / "Contents" / "MacOS" / "QEGTRAIN"])

    def test_main_builds_an_image(self):
        output = self.root / "main"
        printed = io.StringIO()
        with contextlib.redirect_stdout(printed):
            make_dmg.main(["--version", "1.2.3", "--app", str(self.fixture), "--output", str(output)])
        self.assertEqual(os.listdir(output), ["EGTRAIN-1.2.3-macOS-arm64.dmg"])
        self.assertIn("EGTRAIN-1.2.3-macOS-arm64.dmg  ", printed.getvalue())


if __name__ == "__main__":
    unittest.main()
