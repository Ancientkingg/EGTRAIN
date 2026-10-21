#!/usr/bin/env python3
import contextlib
import io
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import macos_build_paths

MACHO_MAGIC = b"\xcf\xfa\xed\xfe"

CLEAN_DEPENDENCIES = """\t@loader_path/libqjpeg.dylib (compatibility version 0.0.0, current version 0.0.0)
\t@executable_path/../Frameworks/QtCore.framework/Versions/A/QtCore (compatibility version 6.0.0, current version 6.8.3)
\t/usr/lib/libSystem.B.dylib (compatibility version 1.0.0, current version 1351.0.0)"""
CLEAN_LOAD_COMMANDS = """Load command 5
          cmd LC_ID_DYLIB
      cmdsize 56
         name @loader_path/libqjpeg.dylib (offset 24)
Load command 6
          cmd LC_RPATH
      cmdsize 48
         path @loader_path/../../Frameworks (offset 12)"""
HOMEBREW_INSTALL_NAME = "/opt/homebrew/opt/qtbase/share/qt/plugins/imageformats/libqjpeg.dylib"
HOMEBREW_DEPENDENCIES = f"""\t{HOMEBREW_INSTALL_NAME} (compatibility version 0.0.0, current version 0.0.0)
\t/usr/lib/libSystem.B.dylib (compatibility version 1.0.0, current version 1351.0.0)"""
HOMEBREW_LOAD_COMMANDS = """Load command 6
          cmd LC_RPATH
      cmdsize 80
         path /opt/homebrew/Cellar/jpeg-turbo/3.2.0/lib (offset 12)
Load command 7
          cmd LC_RPATH
      cmdsize 48
         path /usr/local/lib (offset 12)
Load command 8
          cmd LC_RPATH
      cmdsize 48
         path @loader_path/../lib (offset 12)"""


class BuildPathTests(unittest.TestCase):
    def test_output_without_a_build_path_gives_nothing(self):
        for text in (CLEAN_DEPENDENCIES, CLEAN_LOAD_COMMANDS, "", "/usr/lib/libz.1.dylib\n", "/usr/localized/lib\n", "/opt/homebrewed\n"):
            with self.subTest(text=text):
                self.assertEqual(macos_build_paths.build_paths(text), [])

    def test_lines_with_a_build_path_are_returned(self):
        self.assertEqual(macos_build_paths.build_paths(HOMEBREW_DEPENDENCIES), [
            f"{HOMEBREW_INSTALL_NAME} (compatibility version 0.0.0, current version 0.0.0)"])
        self.assertEqual(macos_build_paths.build_paths(HOMEBREW_LOAD_COMMANDS), [
            "path /opt/homebrew/Cellar/jpeg-turbo/3.2.0/lib (offset 12)", "path /usr/local/lib (offset 12)"])
        self.assertEqual(macos_build_paths.build_paths("/usr/local\n"), ["/usr/local"])

    def test_install_name(self):
        self.assertEqual(macos_build_paths.install_name(HOMEBREW_INSTALL_NAME + "\n"), HOMEBREW_INSTALL_NAME)
        self.assertEqual(macos_build_paths.install_name(""), "")


class FakeBundle(unittest.TestCase):
    """A bundle of files that start with the Mach-O magic, whose otool output comes from a table keyed by file name."""

    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.app = Path(self.temp.name) / "QEGTRAIN.app"
        self.output = {}
        self.calls = []
        self.add("Contents/MacOS/QEGTRAIN", dependencies=CLEAN_DEPENDENCIES, load_commands=CLEAN_LOAD_COMMANDS)
        self.add("Contents/PlugIns/imageformats/libqjpeg.dylib", dependencies=CLEAN_DEPENDENCIES, load_commands=CLEAN_LOAD_COMMANDS,
                 install_name="@loader_path/libqjpeg.dylib")
        (self.app / "Contents/Resources").mkdir(parents=True)
        (self.app / "Contents/Resources/scene.json").write_text("{}", encoding="utf-8")

    def tearDown(self):
        self.temp.cleanup()

    def add(self, relative, dependencies="", load_commands="", install_name=""):
        path = self.app / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(MACHO_MAGIC + bytes(28))
        self.output[path.name] = {"-L": dependencies, "-l": load_commands, "-D": install_name}

    def read(self, option, path):
        return self.output[path.name][option]

    def tool(self, command):
        self.calls.append(command[1:])
        return ""

    def check(self):
        with contextlib.redirect_stdout(io.StringIO()) as printed:
            macos_build_paths.check_app(self.app, self.read)
        return printed.getvalue()

    def failure(self):
        with self.assertRaises(SystemExit) as raised:
            self.check()
        return str(raised.exception)

    def fix(self):
        with contextlib.redirect_stdout(io.StringIO()):
            macos_build_paths.fix_app(self.app, self.read, self.tool)


class CheckTests(FakeBundle):
    def test_clean_bundle_passes(self):
        self.assertIn("2 Mach-O files", self.check())

    def test_dependency_under_homebrew_fails(self):
        self.output["QEGTRAIN"]["-L"] = HOMEBREW_DEPENDENCIES
        self.assertIn(f"Contents/MacOS/QEGTRAIN: otool -L: {HOMEBREW_INSTALL_NAME}", self.failure())

    def test_install_name_under_homebrew_fails(self):
        self.output["libqjpeg.dylib"]["-D"] = HOMEBREW_INSTALL_NAME
        self.assertIn(f"Contents/PlugIns/imageformats/libqjpeg.dylib: otool -D: {HOMEBREW_INSTALL_NAME}", self.failure())

    def test_run_path_under_homebrew_or_usr_local_fails(self):
        self.output["libqjpeg.dylib"]["-l"] = HOMEBREW_LOAD_COMMANDS
        message = self.failure()
        self.assertIn("Contents/PlugIns/imageformats/libqjpeg.dylib: otool -l: path /opt/homebrew/Cellar/jpeg-turbo/3.2.0/lib", message)
        self.assertIn("Contents/PlugIns/imageformats/libqjpeg.dylib: otool -l: path /usr/local/lib", message)
        self.assertNotIn("@loader_path/../lib", message)

    def test_bundle_without_a_mach_o_file_fails(self):
        shutil.rmtree(self.app / "Contents/MacOS")
        shutil.rmtree(self.app / "Contents/PlugIns")
        self.assertIn("no Mach-O file", self.failure())


class FixTests(FakeBundle):
    def test_clean_bundle_runs_no_tool(self):
        self.fix()
        self.assertEqual(self.calls, [])

    def test_run_paths_are_deleted_and_the_install_name_is_renamed(self):
        self.output["libqjpeg.dylib"]["-l"] = HOMEBREW_LOAD_COMMANDS
        self.output["libqjpeg.dylib"]["-D"] = HOMEBREW_INSTALL_NAME
        self.fix()
        path = str(self.app / "Contents/PlugIns/imageformats/libqjpeg.dylib")
        self.assertEqual(self.calls, [
            ["-delete_rpath", "/opt/homebrew/Cellar/jpeg-turbo/3.2.0/lib", path],
            ["-delete_rpath", "/usr/local/lib", path],
            ["-id", "@loader_path/libqjpeg.dylib", path],
        ])


@unittest.skipUnless(sys.platform == "darwin" and shutil.which("cc"), "needs macOS with a compiler")
class RealToolsTests(unittest.TestCase):
    """Builds a plugin with the paths of the Homebrew build and runs the real otool and install_name_tool."""

    def test_fix_leaves_a_bundle_that_passes_the_check(self):
        with tempfile.TemporaryDirectory() as temp:
            app = Path(temp) / "QEGTRAIN.app"
            plugin = app / "Contents/PlugIns/imageformats/libqjpeg.dylib"
            plugin.parent.mkdir(parents=True)
            source = Path(temp) / "plugin.c"
            source.write_text("int plugin_value(void) { return 1; }\n", encoding="utf-8")
            subprocess.run(["cc", "-dynamiclib", str(source), "-o", str(plugin), "-install_name", HOMEBREW_INSTALL_NAME,
                            "-Wl,-rpath,/opt/homebrew/Cellar/jpeg-turbo/3.2.0/lib", "-Wl,-rpath,@loader_path/../lib"], check=True)
            with contextlib.redirect_stdout(io.StringIO()):
                with self.assertRaises(SystemExit) as raised:
                    macos_build_paths.check_app(app)
                self.assertIn("otool -D", str(raised.exception))
                self.assertIn("otool -l", str(raised.exception))
                macos_build_paths.fix_app(app)
                macos_build_paths.check_app(app)
            self.assertEqual(macos_build_paths.install_name(macos_build_paths.otool_text("-D", plugin)), "@loader_path/libqjpeg.dylib")
            self.assertIn("@loader_path/../lib", macos_build_paths.otool_text("-l", plugin))


if __name__ == "__main__":
    unittest.main()
