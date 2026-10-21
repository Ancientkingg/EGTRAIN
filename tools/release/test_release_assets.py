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
import make_dmg

SCRIPT = Path(__file__).resolve().with_name("build_release_assets.py")
SCENES = ("Netherlands", "Paimpol", "Copenhagen", "Milano_Brescia", "Assignment_Gvc_Gdg_Ut",
          "Amsterdam_Hilversum_Student")
MACOS, LINUX = "EGTRAIN-1.2.3-macOS-arm64.dmg", "QEGTRAIN-linux-x86_64.AppImage"
PORTABLE, INSTALLER = "EGTRAIN-Portable-1.2.3-x64.zip", "EGTRAIN-Setup-1.2.3-x64.exe"
MANIFEST = "update-manifest.json"
EXPECTED_NAMES = sorted([MACOS, PORTABLE, INSTALLER, LINUX, MANIFEST] + [scene + ".egscene" for scene in SCENES])
PAYLOAD = {
    "QEGTRAIN.exe": b"MZ executable",
    "egtrain_update_helper.exe": b"MZ helper",
    "Qt6Core.dll": b"MZ core",
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
        self.portable = self.artifacts / "EGTRAIN-windows-x64-portable" / PORTABLE
        self.installer = self.artifacts / "EGTRAIN-windows-x64-installer" / INSTALLER
        self.macos = self.artifacts / "QEGTRAIN-macos-arm64" / MACOS
        self.linux = self.artifacts / "QEGTRAIN-linux-x86_64" / LINUX
        self.scenes = self.artifacts / "EGTRAIN-scenes"
        for name, content in PAYLOAD.items():
            write(self.payload / name, content)
        build_release_assets.build_portable_archive(self.payload, self.portable.parent, "1.2.3")
        write(self.installer, b"installer")
        write(self.macos, b"macOS package")
        write(self.linux, b"AppImage")
        for scene in SCENES:
            write(self.scenes / (scene + ".egscene"), scene.encode())
        write(self.artifacts / "finalize-windows-diagnostics" / "installer.log", b"log")

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
        copies = [(self.macos, MACOS), (self.portable, PORTABLE), (self.installer, INSTALLER), (self.linux, LINUX)] + [
            (self.scenes / (scene + ".egscene"), scene + ".egscene") for scene in SCENES]
        for source, name in copies:
            self.assertTrue(filecmp.cmp(source, self.output / name, shallow=False), name)

        raw = (self.output / MANIFEST).read_bytes()
        self.assertTrue(raw.endswith(b"}\n"))
        self.assertNotIn(b"\r", raw)
        manifest = self.manifest()
        self.assertEqual(list(manifest), ["version", "assets"])
        self.assertEqual(manifest["version"], "1.2.3")
        self.assertEqual(list(manifest["assets"]), ["macos-arm64", "windows-x64", "windows-x64-installer", "linux-x86_64"])
        for key, name in (("macos-arm64", MACOS), ("windows-x64", PORTABLE), ("windows-x64-installer", INSTALLER),
                          ("linux-x86_64", LINUX)):
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
        with zipfile.ZipFile(self.output / PORTABLE) as archive:
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
        os.utime(self.payload / "Qt6Core.dll", (0, 0))
        archive = build_release_assets.build_portable_archive(self.payload, self.root / "portable", "1.2.3")
        with zipfile.ZipFile(archive) as zip_file:
            self.assertIn("Qt6Core.dll", zip_file.namelist())

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
        for path in (self.macos, self.portable, self.installer, self.linux):
            with self.subTest(package=path.name):
                content = path.read_bytes()
                path.unlink()
                self.assertRejected("missing file " + str(path))
                path.write_bytes(b"")
                self.assertRejected("empty file " + str(path))
                path.write_bytes(content)

    def test_package_named_for_another_version_is_rejected(self):
        self.assertRejected("missing file " + str(self.macos.with_name("EGTRAIN-1.2.4-macOS-arm64.dmg")), "1.2.4")
        self.macos.rename(self.macos.with_name("EGTRAIN-1.2.4-macOS-arm64.dmg"))
        self.assertRejected("missing file " + str(self.portable.with_name("EGTRAIN-Portable-1.2.4-x64.zip")), "1.2.4")

    def test_macos_package_is_the_image_that_make_dmg_writes(self):
        self.assertEqual(MACOS, make_dmg.dmg_name("1.2.3"))

    def test_stray_file_in_an_artifact_directory_is_rejected(self):
        write(self.linux.with_name("notes.txt"), b"x")
        self.assertRejected("unexpected notes.txt")

    def test_scene_set_must_be_exactly_the_six(self):
        (self.scenes / "Paimpol.egscene").unlink()
        self.assertRejected("missing file " + str(self.scenes / "Paimpol.egscene"))
        write(self.scenes / "Paimpol.egscene", b"x")
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
        # Each package alone is larger than the limit, which the others stay under.
        with mock.patch.object(build_release_assets, "MAX_PACKAGE_BYTES", 4096):
            for path in (self.macos, self.portable, self.installer, self.linux):
                with self.subTest(package=path.name):
                    content = path.read_bytes()
                    path.write_bytes(b"x" * 4097)
                    self.assertRejected(path.name)
                    path.write_bytes(content)

    def test_portable_archive_must_hold_exactly_the_payload(self):
        def rewrite(mutate):
            members = {name: (self.payload / name).read_bytes() for name in sorted(PAYLOAD)}
            mutate(members)
            with zipfile.ZipFile(self.portable, "w") as archive:
                for name, content in members.items():
                    archive.writestr(name, content)

        def reverse(members):
            items = list(members.items())
            members.clear()
            members.update(reversed(items))

        changes = (
            ("missing member", lambda members: members.pop("a.b")),
            ("extra member", lambda members: members.update({"zz.txt": b"extra"})),
            ("other bytes", lambda members: members.update({"Qt6Core.dll": b"MZ other"})),
            ("other order", reverse),
        )
        for label, mutate in changes:
            with self.subTest(label):
                rewrite(mutate)
                self.assertRejected(PORTABLE)
        self.portable.write_bytes(b"not an archive")
        self.assertRejected("not a readable archive")

    def test_portable_archive_with_a_damaged_member_is_rejected(self):
        raw = bytearray(self.portable.read_bytes())
        with zipfile.ZipFile(self.portable) as archive:
            info = archive.getinfo("QEGTRAIN.exe")
        extra = int.from_bytes(raw[info.header_offset + 28:info.header_offset + 30], "little")
        raw[info.header_offset + 30 + len(info.filename) + extra] ^= 0xFF
        self.portable.write_bytes(bytes(raw))
        self.assertRejected(PORTABLE)

    def test_portable_archive_alone_is_written_from_the_payload(self):
        output = self.root / "portable"
        archive = build_release_assets.build_portable_archive(self.payload, output, "2.0.1")
        self.assertEqual(archive, output / "EGTRAIN-Portable-2.0.1-x64.zip")
        self.assertEqual(os.listdir(output), [archive.name])
        with zipfile.ZipFile(archive) as zip_file:
            self.assertEqual(zip_file.namelist(), sorted(PAYLOAD))
            self.assertIsNone(zip_file.testzip())

    def test_portable_archive_alone_checks_its_input_and_output(self):
        output = self.root / "portable"
        with self.assertRaises(SystemExit) as raised:
            build_release_assets.build_portable_archive(self.payload, output, "1.0")
        self.assertIn("'1.0'", str(raised.exception))
        (self.payload / "QEGTRAIN.exe").rename(self.payload / "platforms" / "QEGTRAIN.exe")
        with self.assertRaises(SystemExit) as raised:
            build_release_assets.build_portable_archive(self.payload, output, "1.2.3")
        self.assertIn("no QEGTRAIN.exe", str(raised.exception))
        (self.payload / "platforms" / "QEGTRAIN.exe").rename(self.payload / "QEGTRAIN.exe")
        self.assertFalse(output.exists())
        write(output / "keep.txt", b"x")
        with self.assertRaises(SystemExit) as raised:
            build_release_assets.build_portable_archive(self.payload, output, "1.2.3")
        self.assertIn(str(output), str(raised.exception))
        self.assertEqual(os.listdir(output), ["keep.txt"])
        shutil.rmtree(output)
        with mock.patch.object(build_release_assets, "MAX_PACKAGE_BYTES", 100):
            with self.assertRaises(SystemExit) as raised:
                build_release_assets.build_portable_archive(self.payload, output, "1.2.3")
        self.assertIn(PORTABLE, str(raised.exception))

    def test_runs_as_a_program_from_another_directory(self):
        result = subprocess.run(
            [sys.executable, str(SCRIPT), "--version", "1.2.3", "--artifacts", str(self.artifacts), "--output", str(self.output)],
            cwd=self.root, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, universal_newlines=True, timeout=120)
        self.assertEqual(result.returncode, 0, result.stdout)
        lines = result.stdout.splitlines()
        self.assertTrue(lines[-1].startswith("OK: 11 release assets"), lines[-1])
        self.assertEqual(len(lines), len(EXPECTED_NAMES) + 1)
        self.assertIsNone(re.search(r"[0-9a-f]{64}", result.stdout))

    def test_portable_archive_runs_as_a_program_and_the_two_sources_exclude_each_other(self):
        output = self.root / "portable"
        result = subprocess.run(
            [sys.executable, str(SCRIPT), "--version", "1.2.3", "--payload", str(self.payload), "--output", str(output)],
            cwd=self.root, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, universal_newlines=True, timeout=120)
        self.assertEqual(result.returncode, 0, result.stdout)
        self.assertEqual(os.listdir(output), [PORTABLE])
        self.assertTrue(result.stdout.splitlines()[-1].startswith("OK: portable archive"), result.stdout)
        self.assertIsNone(re.search(r"[0-9a-f]{64}", result.stdout))
        for sources in ([], ["--artifacts", str(self.artifacts), "--payload", str(self.payload)]):
            with self.subTest(sources=sources):
                result = subprocess.run(
                    [sys.executable, str(SCRIPT), "--version", "1.2.3", "--output", str(self.output)] + sources,
                    cwd=self.root, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, universal_newlines=True, timeout=120)
                self.assertEqual(result.returncode, 2, result.stdout)
                self.assertFalse(self.output.exists())

    @unittest.skipUnless(sys.platform == "win32", "extracts the archive with PowerShell as the application does")
    def test_powershell_extracts_the_archive_to_the_payload(self):
        self.build()
        extract = self.root / "extract"
        extract.mkdir()
        environment = dict(os.environ, EGTRAIN_UPDATE_ZIP=str(self.output / PORTABLE), EGTRAIN_UPDATE_DEST=str(extract))
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
            build_release_assets.check_package_size(PORTABLE, size)
        for size in (0, 2 * 1024 ** 3 + 1):
            self.assertIn(PORTABLE, exit_message(build_release_assets.check_package_size, PORTABLE, size))


if __name__ == "__main__":
    unittest.main()
