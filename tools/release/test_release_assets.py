#!/usr/bin/env python3
import filecmp
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
import unittest
import zipfile
from pathlib import Path
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parent))
import build_release_assets

SCRIPT = Path(__file__).resolve().with_name("build_release_assets.py")
SCENES = ("Netherlands", "Paimpol", "Copenhagen", "Milano_Brescia", "Assignment_Gvc_Gdg_Ut", "Lebanon",
          "Amsterdam_Hilversum_Student")
MACOS, WINDOWS, LINUX = "QEGTRAIN-macos-arm64.zip", "QEGTRAIN-windows-x64.zip", "QEGTRAIN-linux-x86_64.AppImage"
MANIFEST = "update-manifest.json"
EXPECTED_NAMES = sorted([MACOS, WINDOWS, LINUX, MANIFEST] + [scene + ".egscene" for scene in SCENES])
PAYLOAD = {
    "QEGTRAIN.exe": b"MZ executable",
    "egtrain_update_helper.exe": b"MZ helper",
    "Qt5Core.dll": b"MZ core",
    "a.b": b"dot",
    "a/b": b"slash",
    "platforms/qwindows.dll": b"MZ platform",
    "Scenes/Paimpol/scene.json": b'{"name": "Paimpol"}',
}


def write(path, content):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(content)


def tree(root):
    return {path.relative_to(root).as_posix(): path.read_bytes() if path.is_file() else None
            for path in sorted(root.rglob("*"))}


def exit_message(function, *arguments):
    try:
        function(*arguments)
    except SystemExit as error:
        return str(error)
    raise AssertionError(f"{function.__name__} accepted its input")


class ReleaseAssetsTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
        self.artifacts = self.root / "artifacts"
        self.output = self.root / "release-assets"
        self.payload = self.artifacts / "QEGTRAIN-windows-x64-payload"
        self.macos = self.artifacts / "QEGTRAIN-macos-arm64" / MACOS
        self.linux = self.artifacts / "QEGTRAIN-linux-x86_64" / LINUX
        self.scenes = self.artifacts / "EGTRAIN-scenes"
        for name, content in PAYLOAD.items():
            write(self.payload / name, content)
        write(self.macos, b"macOS package")
        write(self.linux, b"AppImage")
        for scene in SCENES:
            write(self.scenes / (scene + ".egscene"), scene.encode())
        write(self.artifacts / "QEGTRAIN-windows-x64" / WINDOWS, b"transport zip")

    def tearDown(self):
        self.temp.cleanup()

    def build(self, version="1.2.3"):
        return build_release_assets.build_release_assets(self.artifacts, self.output, version)

    def manifest(self):
        return json.loads((self.output / MANIFEST).read_text(encoding="utf-8"))

    def assertRejected(self, part, version="1.2.3"):
        with self.assertRaises(SystemExit) as raised:
            self.build(version)
        self.assertIn(part, str(raised.exception))
        self.assertFalse(self.output.exists())

    def test_complete_run_writes_exactly_the_release_assets(self):
        written = self.build()
        self.assertEqual(sorted(os.listdir(self.output)), EXPECTED_NAMES)
        self.assertEqual([path.name for path in written], EXPECTED_NAMES)
        copies = [(self.macos, MACOS), (self.linux, LINUX)] + [
            (self.scenes / (scene + ".egscene"), scene + ".egscene") for scene in SCENES]
        for source, name in copies:
            self.assertTrue(filecmp.cmp(source, self.output / name, shallow=False), name)

        raw = (self.output / MANIFEST).read_bytes()
        self.assertTrue(raw.endswith(b"}\n"))
        self.assertNotIn(b"\r", raw)
        manifest = self.manifest()
        self.assertEqual(list(manifest), ["version", "assets"])
        self.assertEqual(manifest["version"], "1.2.3")
        self.assertEqual(list(manifest["assets"]), ["macos-arm64", "windows-x64", "linux-x86_64"])
        for key, name in (("macos-arm64", MACOS), ("windows-x64", WINDOWS), ("linux-x86_64", LINUX)):
            entry = manifest["assets"][key]
            data = (self.output / name).read_bytes()
            self.assertEqual(list(entry)[:3], ["name", "sha256", "size"])
            self.assertEqual(entry["name"], name)
            self.assertEqual(entry["sha256"], hashlib.sha256(data).hexdigest())
            self.assertEqual(entry["size"], os.path.getsize(self.output / name))
            self.assertEqual("files" in entry, key == "windows-x64")

    def test_portable_archive_holds_the_payload_files_in_string_order(self):
        self.build()
        expected = sorted(path.relative_to(self.payload).as_posix() for path in self.payload.rglob("*") if path.is_file())
        with zipfile.ZipFile(self.output / WINDOWS) as archive:
            names = archive.namelist()
            self.assertEqual(names, expected)
            self.assertEqual(names, self.manifest()["assets"]["windows-x64"]["files"])
            self.assertLess(names.index("a.b"), names.index("a/b"))
            self.assertIn("QEGTRAIN.exe", names)
            self.assertFalse([name for name in names if name.endswith("/") or "\\" in name])
            self.assertNotIn(self.payload.name, [name.split("/")[0] for name in names])
            for name in names:
                self.assertEqual(archive.read(name), (self.payload / name).read_bytes(), name)

    def test_artifacts_are_not_changed(self):
        before = tree(self.artifacts)
        self.build()
        self.assertEqual(tree(self.artifacts), before)

    def test_file_older_than_1980_is_archived(self):
        os.utime(self.payload / "Qt5Core.dll", (0, 0))
        self.build()
        with zipfile.ZipFile(self.output / WINDOWS) as archive:
            self.assertIn("Qt5Core.dll", archive.namelist())

    def test_payload_without_exe_at_top_level_is_rejected(self):
        (self.payload / "QEGTRAIN.exe").rename(self.payload / "platforms" / "QEGTRAIN.exe")
        self.assertRejected("no QEGTRAIN.exe")

    def test_empty_payload_is_rejected(self):
        shutil.rmtree(self.payload)
        self.payload.mkdir()
        self.assertRejected("holds no file")

    @unittest.skipIf(sys.platform == "win32", "creating a link needs a privilege on Windows")
    def test_link_in_payload_is_rejected(self):
        os.symlink(self.payload / "QEGTRAIN.exe", self.payload / "platforms" / "link.dll")
        self.assertRejected("link.dll")
        (self.payload / "platforms" / "link.dll").unlink()
        os.symlink(self.payload / "platforms", self.payload / "dirlink", target_is_directory=True)
        self.assertRejected("dirlink")

    @unittest.skipIf(sys.platform == "win32" or getattr(os, "geteuid", lambda: 1)() == 0, "needs a directory that cannot be read")
    def test_unreadable_directory_in_payload_is_rejected(self):
        platforms = self.payload / "platforms"
        platforms.chmod(0)
        try:
            self.assertRejected("cannot read " + str(platforms))
        finally:
            platforms.chmod(0o755)

    def test_missing_or_empty_package_is_rejected(self):
        self.macos.unlink()
        self.assertRejected("missing file " + str(self.macos))
        write(self.macos, b"macOS package")
        self.linux.write_bytes(b"")
        self.assertRejected("empty file " + str(self.linux))

    def test_stray_file_in_an_artifact_directory_is_rejected(self):
        write(self.linux.with_name("notes.txt"), b"x")
        self.assertRejected("unexpected notes.txt")

    def test_scene_set_must_be_exactly_the_seven(self):
        (self.scenes / "Lebanon.egscene").unlink()
        self.assertRejected("missing file " + str(self.scenes / "Lebanon.egscene"))
        write(self.scenes / "Lebanon.egscene", b"x")
        write(self.scenes / "Extra.egscene", b"x")
        self.assertRejected("unexpected Extra.egscene")

    def test_invalid_versions_are_rejected(self):
        for version in ("1.0", "v1.0.0", "1.0.0-rc.1"):
            with self.subTest(version=version):
                self.assertRejected(repr(version), version)

    def test_output_must_be_empty_or_missing(self):
        write(self.output / "keep.txt", b"x")
        before = tree(self.output)
        with self.assertRaises(SystemExit) as raised:
            self.build()
        self.assertIn(str(self.output), str(raised.exception))
        self.assertEqual(tree(self.output), before)
        shutil.rmtree(self.output)
        write(self.output, b"a file")
        with self.assertRaises(SystemExit):
            self.build()
        self.output.unlink()
        self.output.mkdir()
        self.build()
        self.assertEqual(sorted(os.listdir(self.output)), EXPECTED_NAMES)

    def test_package_sizes_are_checked_at_the_call_sites(self):
        # The macOS package has 13 bytes and the AppImage 8.
        with mock.patch.object(build_release_assets, "MAX_PACKAGE_BYTES", 10):
            self.assertRejected(MACOS)
        self.macos.write_bytes(b"macOS")
        with mock.patch.object(build_release_assets, "MAX_PACKAGE_BYTES", 6):
            self.assertRejected(LINUX)

    def test_archive_size_is_checked_before_the_manifest_is_written(self):
        with mock.patch.object(build_release_assets, "MAX_PACKAGE_BYTES", 100):
            with self.assertRaises(SystemExit) as raised:
                self.build()
        self.assertIn(WINDOWS, str(raised.exception))
        self.assertNotIn(MANIFEST, os.listdir(self.output))

    def test_runs_as_a_program_from_another_directory(self):
        result = subprocess.run(
            [sys.executable, str(SCRIPT), "--version", "1.2.3", "--artifacts", str(self.artifacts), "--output", str(self.output)],
            cwd=self.root, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, universal_newlines=True, timeout=120)
        self.assertEqual(result.returncode, 0, result.stdout)
        lines = result.stdout.splitlines()
        self.assertTrue(lines[-1].startswith("OK: 11 release assets"), lines[-1])
        self.assertEqual(len(lines), len(EXPECTED_NAMES) + 1)
        self.assertIsNone(re.search(r"[0-9a-f]{64}", result.stdout))

    @unittest.skipUnless(sys.platform == "win32", "extracts the archive with PowerShell as the application does")
    def test_powershell_extracts_the_archive_to_the_payload(self):
        self.build()
        extract = self.root / "extract"
        extract.mkdir()
        environment = dict(os.environ, EGTRAIN_UPDATE_ZIP=str(self.output / WINDOWS), EGTRAIN_UPDATE_DEST=str(extract))
        command = ("$ErrorActionPreference='Stop'; Expand-Archive -LiteralPath $env:EGTRAIN_UPDATE_ZIP "
                   "-DestinationPath $env:EGTRAIN_UPDATE_DEST -Force")
        subprocess.run(["powershell.exe", "-NoProfile", "-NonInteractive", "-Command", command],
                       env=environment, check=True, timeout=120)
        self.assertEqual(tree(extract), tree(self.payload))


class RuleTests(unittest.TestCase):
    def test_file_list_limits(self):
        accepted = (
            ["QEGTRAIN.exe"] + [f"f{number}" for number in range(4095)],
            ["QEGTRAIN.exe", "a" * 260],
            ["QEGTRAIN.exe", "\U0001F600" * 130],
            ["QEGTRAIN.exe", "dir/file.txt", "a.b", "a/b"],
        )
        for files in accepted:
            build_release_assets.check_windows_files(files)

    def test_file_list_rejections_name_the_cause(self):
        rejected = (
            (["QEGTRAIN.exe"] + [f"f{number}" for number in range(4096)], "4097 files"),
            (["QEGTRAIN.exe", "a" * 261], "261 characters"),
            (["QEGTRAIN.exe", "\U0001F600" * 131], "262 characters"),
            (["QEGTRAIN.exe", "C:/x"], "colon"),
            (["QEGTRAIN.exe", "a/b:stream"], "colon"),
            (["QEGTRAIN.exe", "a\0b"], "NUL"),
            (["QEGTRAIN.exe", "a\\b"], "backslash"),
            (["QEGTRAIN.exe", "a//b"], "segment"),
            (["QEGTRAIN.exe", "/a"], "segment"),
            (["QEGTRAIN.exe", "a/"], "segment"),
            (["QEGTRAIN.exe", ".."], "segment"),
            (["QEGTRAIN.exe", "a/../b"], "segment"),
            (["QEGTRAIN.exe", "."], "segment"),
            (["QEGTRAIN.exe", "a/./b"], "segment"),
            (["QEGTRAIN.exe", "a\udcffb"], "not valid text"),
            (["bin/QEGTRAIN.exe"], "no QEGTRAIN.exe"),
            ([], "no QEGTRAIN.exe"),
        )
        for files, part in rejected:
            with self.subTest(files=files[-1:]):
                self.assertIn(part, exit_message(build_release_assets.check_windows_files, files))
        message = exit_message(build_release_assets.check_windows_files, ["QEGTRAIN.exe", "a:b", "c:d"])
        self.assertIn("'a:b'", message)
        self.assertNotIn("c:d", message)

    def test_package_size_bounds(self):
        for size in (1, 2 * 1024 ** 3):
            build_release_assets.check_package_size(WINDOWS, size)
        for size in (0, 2 * 1024 ** 3 + 1):
            self.assertIn(WINDOWS, exit_message(build_release_assets.check_package_size, WINDOWS, size))


if __name__ == "__main__":
    unittest.main()
