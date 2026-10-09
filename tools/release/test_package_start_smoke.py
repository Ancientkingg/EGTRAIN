#!/usr/bin/env python3
import contextlib
import io
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parent))
import package_start_smoke


def check(package):
    with contextlib.redirect_stdout(io.StringIO()):
        package_start_smoke.check_no_openmp_runtime(package)


class OpenMpRuntimeTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.package = Path(self.temp.name)
        (self.package / "plugins").mkdir()
        (self.package / "QEGTRAIN.exe").write_bytes(b"MZ\0Qt5Core.dll\0libzmq-mt-4_3_5.dll\0compatibility\0")
        (self.package / "plugins" / "qwindows.dll").write_bytes(b"MZ\0Qt5Gui.dll\0")

    def tearDown(self):
        self.temp.cleanup()

    def test_package_without_openmp_passes(self):
        check(self.package)

    def test_executable_that_names_the_runtime_fails(self):
        for name, reported in ((b"VCOMP140.DLL", "VCOMP140.DLL"), (b"vcomp140d.dll", "vcomp140d.dll"),
                               (b"libomp.dylib", "libomp.dylib"), (b"libgomp.so.1", "libgomp.so"),
                               (b"libomp140.x86_64.dll", "libomp140.x86_64.dll")):
            (self.package / "QEGTRAIN.exe").write_bytes(b"MZ\0Qt5Core.dll\0" + name + b"\0")
            with self.assertRaises(SystemExit) as raised:
                check(self.package)
            self.assertIn("names the OpenMP runtime library " + reported, str(raised.exception))

    def test_runtime_library_in_a_subdirectory_fails(self):
        (self.package / "plugins" / "vcomp140.dll").write_bytes(b"MZ")
        with self.assertRaises(SystemExit) as raised:
            check(self.package)
        self.assertIn("contains an OpenMP runtime library", str(raised.exception))


OTOOL_TEXT = """Load command 3
          cmd LC_ID_DYLIB
      cmdsize 64
         name @executable_path/../Frameworks/QtCore.framework/Versions/5/QtCore (offset 24)
   time stamp 1 Thu Jan  1 01:00:01 1970
Load command 4
          cmd LC_LOAD_DYLINKER
      cmdsize 32
         name /usr/lib/dyld (offset 12)
Load command 9
          cmd LC_LOAD_DYLIB
      cmdsize 88
         name @executable_path/../Frameworks/QtDBus.framework/Versions/5/QtDBus (offset 24)
   time stamp 2 Thu Jan  1 01:00:02 1970
Load command 10
          cmd LC_LOAD_WEAK_DYLIB
      cmdsize 56
         name /System/Library/Frameworks/Metal.framework/Versions/A/Metal (offset 24)
Load command 11
          cmd LC_REEXPORT_DYLIB
      cmdsize 56
         name @rpath/libre.dylib (offset 24)
Load command 12
          cmd LC_LOAD_UPWARD_DYLIB
      cmdsize 56
         name /usr/lib/libupward.dylib (offset 24)
Load command 13
          cmd LC_RPATH
      cmdsize 32
         path @loader_path/../lib (offset 12)
Load command 14
          cmd LC_RPATH
      cmdsize 48
         path /opt/some where/lib (offset 12)
"""
MACHO_MAGIC = b"\xcf\xfa\xed\xfe"


class LoadCommandParserTests(unittest.TestCase):
    def test_dependencies_and_rpaths_without_the_install_name(self):
        dependencies, rpaths = package_start_smoke.parse_load_commands(OTOOL_TEXT)
        self.assertEqual(dependencies, [
            "@executable_path/../Frameworks/QtDBus.framework/Versions/5/QtDBus",
            "/System/Library/Frameworks/Metal.framework/Versions/A/Metal",
            "@rpath/libre.dylib",
            "/usr/lib/libupward.dylib",
        ])
        self.assertEqual(rpaths, ["@loader_path/../lib", "/opt/some where/lib"])


class BundleClosureTests(unittest.TestCase):
    """The bundle is fabricated: a file that starts with the Mach-O magic stands for a library, and the load
    commands of each file come from a table that is keyed by the file name."""

    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
        self.app = self.root / "QEGTRAIN.app"
        self.loads = {}

    def tearDown(self):
        self.temp.cleanup()

    def add(self, relative, dependencies=(), rpaths=()):
        path = self.app / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(MACHO_MAGIC + bytes(28))
        self.loads[path.name] = (list(dependencies), list(rpaths))

    def add_other(self, relative, content):
        path = self.app / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(content)

    def run_check(self):
        output = io.StringIO()
        with contextlib.redirect_stdout(output):
            package_start_smoke.check_bundle_closure(self.app, lambda path: self.loads.get(path.name, ([], [])))
        return output.getvalue()

    def failure(self):
        with self.assertRaises(SystemExit) as raised:
            self.run_check()
        return str(raised.exception)

    def add_closed_bundle(self):
        self.add("Contents/MacOS/QEGTRAIN", [
            "@executable_path/../Frameworks/QtCore.framework/Versions/5/QtCore",
            "/usr/lib/libSystem.B.dylib",
            "/System/Library/Frameworks/AppKit.framework/Versions/C/AppKit",
        ])
        self.add("Contents/Frameworks/QtCore.framework/Versions/5/QtCore", ["/usr/lib/libc++.1.dylib"])
        self.add("Contents/PlugIns/platforms/libqcocoa.dylib", [
            "@executable_path/../Frameworks/QtCore.framework/Versions/5/QtCore",
            "@executable_path/../Frameworks/libfirst.dylib",
        ])
        self.add("Contents/Frameworks/libfirst.dylib", ["@loader_path/libsecond.dylib"])
        self.add("Contents/Frameworks/libsecond.dylib", ["@rpath/libthird.dylib"], ["@loader_path/missing", "@loader_path/lib"])
        self.add("Contents/Frameworks/lib/libthird.dylib", ["/usr/lib/libz.1.dylib"])

    def test_closed_bundle_passes(self):
        self.add_closed_bundle()
        self.assertIn("6 Mach-O files", self.run_check())

    def test_dependency_on_a_missing_file_fails(self):
        self.add_closed_bundle()
        self.loads["libqcocoa.dylib"][0].append("@executable_path/../Frameworks/QtDBus.framework/Versions/5/QtDBus")
        self.assertIn(
            "Contents/PlugIns/platforms/libqcocoa.dylib: @executable_path/../Frameworks/QtDBus.framework/Versions/5/QtDBus"
            " does not exist inside the app", self.failure())

    def test_absolute_path_that_is_not_a_system_library_fails(self):
        self.add_closed_bundle()
        self.loads["libfirst.dylib"][0].append("/opt/homebrew/opt/zeromq/lib/libzmq.5.dylib")
        message = self.failure()
        self.assertIn("Contents/Frameworks/libfirst.dylib: /opt/homebrew/opt/zeromq/lib/libzmq.5.dylib", message)
        self.assertNotIn("libSystem", message)
        self.assertNotIn("AppKit", message)

    def test_rpath_that_no_entry_resolves_fails(self):
        for rpaths in ([], ["@loader_path/missing", "@executable_path/../Frameworks"]):
            with self.subTest(rpaths=rpaths):
                self.add_closed_bundle()
                self.loads["libsecond.dylib"] = (["@rpath/libthird.dylib"], rpaths)
                self.assertIn("Contents/Frameworks/libsecond.dylib: @rpath/libthird.dylib does not exist inside the app", self.failure())

    def test_path_that_leaves_the_app_fails(self):
        (self.root / "outside.dylib").write_bytes(MACHO_MAGIC + bytes(28))
        for name in ("@executable_path/../../../outside.dylib", "@loader_path/../../../../outside.dylib"):
            with self.subTest(name=name):
                self.add_closed_bundle()
                self.loads["libqcocoa.dylib"][0].append(name)
                self.assertIn(f"Contents/PlugIns/platforms/libqcocoa.dylib: {name} is outside the app", self.failure())

    def test_library_that_nothing_loads_fails(self):
        self.add_closed_bundle()
        self.add("Contents/Frameworks/libunused.dylib", ["/usr/lib/libz.1.dylib"])
        self.assertIn("Contents/Frameworks/libunused.dylib is loaded by no file of the app", self.failure())

    def test_library_that_only_an_unused_library_loads_fails(self):
        self.add_closed_bundle()
        self.add("Contents/Frameworks/libunused.dylib", ["@loader_path/libonlyunused.dylib"])
        self.add("Contents/Frameworks/libonlyunused.dylib")
        message = self.failure()
        self.assertIn("Contents/Frameworks/libunused.dylib is loaded by no file of the app", message)
        self.assertIn("Contents/Frameworks/libonlyunused.dylib is loaded by no file of the app", message)

    def test_plugin_that_nothing_loads_passes(self):
        self.add_closed_bundle()
        self.add("Contents/PlugIns/imageformats/libqsvg.dylib", ["/usr/lib/libz.1.dylib"])
        self.assertIn("7 Mach-O files", self.run_check())

    def test_file_that_is_not_a_mach_o_file_is_ignored(self):
        self.add_closed_bundle()
        self.add_other("Contents/Resources/Scenes/Paimpol/scene.json", b'{"name": "Paimpol"}')
        self.add_other("Contents/Frameworks/QtCore.framework/Versions/5/_CodeSignature/CodeResources", b"<?xml version=\"1.0\"?>")
        self.assertIn("6 Mach-O files", self.run_check())


class AppBundleTests(unittest.TestCase):
    def test_package_with_an_app_bundle(self):
        with tempfile.TemporaryDirectory() as temp:
            package = Path(temp)
            (package / "QEGTRAIN.app" / "Contents" / "MacOS").mkdir(parents=True)
            self.assertEqual(package_start_smoke.find_app_bundle(package), package / "QEGTRAIN.app")

    def test_package_without_an_app_bundle(self):
        with tempfile.TemporaryDirectory() as temp:
            package = Path(temp)
            (package / "QEGTRAIN.exe").write_bytes(b"MZ")
            self.assertIsNone(package_start_smoke.find_app_bundle(package))


class MainTests(unittest.TestCase):
    """main() runs with the two launches and the closure check replaced, so no application starts."""

    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.package = Path(self.temp.name).resolve()
        self.calls = []

    def tearDown(self):
        self.temp.cleanup()

    def add_package(self, executable, scenes):
        (self.package / executable).parent.mkdir(parents=True, exist_ok=True)
        (self.package / executable).write_bytes(b"program")
        (self.package / scenes / "Paimpol").mkdir(parents=True)
        (self.package / scenes / "Paimpol" / "scene.json").write_text("{}")

    def launch(self, command, cwd, env, what):
        self.calls.append(what)
        if what != "headless run":
            return "first_runtime_paint"
        output = Path(env["QEGTRAIN_OUTPUT_DIR"]) / "Output" / "Paimpol"
        output.mkdir(parents=True)
        (output / "EnergyConsumptionPerTrain.txt").write_text("")
        return "End of Simulation"

    def run_main(self):
        with mock.patch.object(package_start_smoke, "launch", self.launch), \
                mock.patch.object(package_start_smoke, "check_bundle_closure", lambda app: self.calls.append(app)), \
                mock.patch.object(sys, "argv", ["package_start_smoke.py", str(self.package)]), \
                contextlib.redirect_stdout(io.StringIO()):
            package_start_smoke.main()

    def test_app_bundle_is_checked_before_the_launches(self):
        self.add_package("QEGTRAIN.app/Contents/MacOS/QEGTRAIN", "QEGTRAIN.app/Contents/Resources/Scenes")
        self.run_main()
        self.assertEqual(self.calls, [self.package / "QEGTRAIN.app", "headless run", "window start"])

    def test_package_without_an_app_bundle_is_only_launched(self):
        self.add_package("QEGTRAIN.exe", "Scenes")
        self.run_main()
        self.assertEqual(self.calls, ["headless run", "window start"])


if __name__ == "__main__":
    unittest.main()
