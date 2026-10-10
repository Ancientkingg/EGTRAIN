#!/usr/bin/env python3
import contextlib
import io
import os
import subprocess
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


ELF_MAGIC = b"\x7fELF"
LDD_TEXT = """\tlinux-vdso.so.1 (0x00007ffd5a7f2000)
\tlibQt5Widgets.so.5 => /tmp/x/squashfs-root/usr/lib/libQt5Widgets.so.5 (0x00007f3e4c000000)
\tlibQt5Charts.so.5 => not found
\tlibc.so.6 => /lib/x86_64-linux-gnu/libc.so.6 (0x00007f3e4bc00000)
\t/lib64/ld-linux-x86-64.so.2 (0x00007f3e4d000000)
"""


class LddParserTests(unittest.TestCase):
    def test_libraries_with_a_target_and_without(self):
        self.assertEqual(package_start_smoke.parse_ldd(LDD_TEXT), [
            ("libQt5Widgets.so.5", "/tmp/x/squashfs-root/usr/lib/libQt5Widgets.so.5"),
            ("libQt5Charts.so.5", "not found"),
            ("libc.so.6", "/lib/x86_64-linux-gnu/libc.so.6"),
        ])

    def test_lines_without_an_arrow_give_nothing(self):
        for text in ("\tlinux-vdso.so.1 (0x00007ffd5a7f2000)\n", "\t/lib64/ld-linux-x86-64.so.2 (0x00007f3e4d000000)\n",
                     "\tnot a dynamic executable\n", "\tstatically linked\n", ""):
            with self.subTest(text=text):
                self.assertEqual(package_start_smoke.parse_ldd(text), [])

    def test_target_with_a_space(self):
        self.assertEqual(package_start_smoke.parse_ldd("\tlibQt5Core.so.5 => /opt/my app/lib/libQt5Core.so.5 (0x00007f3e4c000000)\n"),
                         [("libQt5Core.so.5", "/opt/my app/lib/libQt5Core.so.5")])


class LddOutputTests(unittest.TestCase):
    PLUGIN = Path("usr/plugins/platforms/libqxcb.so")

    def run_ldd(self, returncode, stdout="", stderr=""):
        completed = subprocess.CompletedProcess(["/usr/bin/ldd"], returncode, stdout.encode(), stderr.encode())
        with mock.patch.object(subprocess, "run", return_value=completed) as run:
            text = package_start_smoke.ldd_output(self.PLUGIN)
        return text, run

    def raised(self, returncode, stdout="", stderr=""):
        with self.assertRaises(SystemExit) as raised:
            self.run_ldd(returncode, stdout, stderr)
        return str(raised.exception)

    def test_standard_output_is_returned_and_a_warning_on_standard_error_is_not_a_failure(self):
        text, _ = self.run_ldd(0, LDD_TEXT, "ldd: warning: you do not have execution permission for `usr/plugins/platforms/libqxcb.so'\n")
        self.assertEqual(text, LDD_TEXT)

    def test_file_that_is_not_a_dynamic_executable_is_returned(self):
        text, _ = self.run_ldd(1, "\tnot a dynamic executable\n")
        self.assertIn("not a dynamic executable", text)

    def test_listing_with_a_missing_library_is_returned_whatever_the_status(self):
        text, _ = self.run_ldd(1, LDD_TEXT)
        self.assertIn("libQt5Charts.so.5 => not found", text)

    def test_failure_without_a_listing_names_the_file_and_shows_standard_error(self):
        message = self.raised(2, "", "ldd: error while loading shared libraries\n")
        self.assertIn(str(self.PLUGIN), message)
        self.assertIn("exited with 2", message)
        self.assertIn("error while loading shared libraries", message)

    def test_missing_tool_names_the_file(self):
        with mock.patch.object(subprocess, "run", side_effect=FileNotFoundError("no ldd")):
            with self.assertRaises(SystemExit) as raised:
                package_start_smoke.ldd_output(self.PLUGIN)
        self.assertIn(str(self.PLUGIN), str(raised.exception))

    def test_command_and_environment(self):
        with mock.patch.dict(os.environ, {"LD_LIBRARY_PATH": "/opt/qt/lib", "QT_PLUGIN_PATH": "/opt/qt/plugins"}):
            _, run = self.run_ldd(0, LDD_TEXT)
            expected = package_start_smoke.clean_environment()
        self.assertEqual(run.call_args.args[0], ["/usr/bin/ldd", str(self.PLUGIN)])
        self.assertTrue(run.call_args.kwargs["env"] == expected, "ldd does not get the environment of clean_environment()")
        self.assertFalse("LD_LIBRARY_PATH" in run.call_args.kwargs["env"])


class CleanEnvironmentTests(unittest.TestCase):
    def test_library_and_qt_variables_are_dropped_and_the_display_is_kept(self):
        variables = {"LD_LIBRARY_PATH": "/opt/qt/lib", "DYLD_LIBRARY_PATH": "/opt/qt/lib", "DYLD_FRAMEWORK_PATH": "/opt/qt",
                     "QT_PLUGIN_PATH": "/opt/qt/plugins", "VCPKG_ROOT": "/opt/vcpkg", "DISPLAY": ":99",
                     "XAUTHORITY": "/tmp/xvfb-run.auth", "EGTRAIN_UNRELATED": "1"}
        with mock.patch.dict(os.environ, variables):
            env = package_start_smoke.clean_environment()
        for name in ("LD_LIBRARY_PATH", "DYLD_LIBRARY_PATH", "DYLD_FRAMEWORK_PATH", "QT_PLUGIN_PATH", "VCPKG_ROOT"):
            self.assertFalse(name in env, name)
        self.assertEqual((env["DISPLAY"], env["XAUTHORITY"], env["EGTRAIN_UNRELATED"]), (":99", "/tmp/xvfb-run.auth", "1"))


class LinuxLibrariesTests(unittest.TestCase):
    """The AppDir is fabricated: a file that starts with the ELF magic stands for a program or a plugin, and what ldd
    prints for each file comes from a table that is keyed by the file name."""

    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name).resolve()
        self.app_dir = self.root / "AppDir"
        self.libraries = {}
        self.asked = []

    def tearDown(self):
        self.temp.cleanup()

    def inside(self, relative):
        return str(self.app_dir / relative)

    def add(self, relative, libraries=()):
        path = self.app_dir / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(ELF_MAGIC + bytes(28))
        self.libraries[path.name] = list(libraries)

    def add_other(self, relative, content):
        path = self.app_dir / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(content)

    def ldd(self, path):
        self.asked.append(path.relative_to(self.app_dir.resolve()).as_posix())
        lines = ["\tlinux-vdso.so.1 (0x00007ffd5a7f2000)"]
        for name, target in self.libraries.get(path.name, []):
            lines.append(f"\t{name} => not found" if target is None else f"\t{name} => {target} (0x00007f3e4c000000)")
        lines.append("\t/lib64/ld-linux-x86-64.so.2 (0x00007f3e4d000000)")
        return "\n".join(lines) + "\n"

    def run_check(self):
        output = io.StringIO()
        with contextlib.redirect_stdout(output):
            package_start_smoke.check_linux_libraries(self.app_dir, self.ldd)
        return output.getvalue()

    def failure(self):
        with self.assertRaises(SystemExit) as raised:
            self.run_check()
        return str(raised.exception)

    def add_closed_app_dir(self):
        self.add("usr/bin/QEGTRAIN", [
            ("libQt5Core.so.5", self.inside("usr/bin/../lib/libQt5Core.so.5")),
            ("libzmq.so.5", self.inside("usr/lib/libzmq.so.5")),
            ("libc.so.6", "/lib/x86_64-linux-gnu/libc.so.6"),
            ("libicuuc.so.70", "/usr/lib/x86_64-linux-gnu/libicuuc.so.70"),
        ])
        self.add("usr/bin/scene_tool", [("libstdc++.so.6", "/lib/x86_64-linux-gnu/libstdc++.so.6")])
        self.add("usr/plugins/platforms/libqxcb.so", [
            ("libQt5XcbQpa.so.5", self.inside("usr/lib/libQt5XcbQpa.so.5")),
            ("libxcb.so.1", "/lib/x86_64-linux-gnu/libxcb.so.1"),
        ])

    def test_closed_app_dir_passes(self):
        self.add_closed_app_dir()
        self.assertIn("3 ELF files", self.run_check())
        self.assertEqual(self.asked, ["usr/bin/QEGTRAIN", "usr/bin/scene_tool", "usr/plugins/platforms/libqxcb.so"])

    def test_qt_library_outside_the_package_fails(self):
        self.add_closed_app_dir()
        outside = str(self.root / "qt" / "libQt5Core.so.5")
        self.libraries["QEGTRAIN"][0] = ("libQt5Core.so.5", outside)
        message = self.failure()
        self.assertIn(f"usr/bin/QEGTRAIN: libQt5Core.so.5 resolves to {outside}, outside the package", message)
        self.assertNotIn("libc.so.6", message)
        self.assertNotIn("libicuuc", message)

    def test_directory_with_the_name_of_the_package_as_a_prefix_is_outside(self):
        self.add_closed_app_dir()
        self.libraries["QEGTRAIN"][0] = ("libQt5Core.so.5", str(self.root / "AppDir2" / "usr" / "lib" / "libQt5Core.so.5"))
        self.assertIn("usr/bin/QEGTRAIN: libQt5Core.so.5 resolves to", self.failure())

    def test_path_that_leaves_the_package_with_dot_dot_fails(self):
        self.add_closed_app_dir()
        self.libraries["QEGTRAIN"][0] = ("libQt5Core.so.5", self.inside("usr/bin/../../../libQt5Core.so.5"))
        self.assertIn("usr/bin/QEGTRAIN: libQt5Core.so.5 resolves to", self.failure())

    def test_library_that_is_not_found_fails(self):
        self.add_closed_app_dir()
        self.libraries["libqxcb.so"][0] = ("libQt5XcbQpa.so.5", None)
        self.assertIn("usr/plugins/platforms/libqxcb.so: libQt5XcbQpa.so.5 is not found", self.failure())

    def test_zeromq_outside_the_package_fails(self):
        self.add_closed_app_dir()
        self.libraries["QEGTRAIN"][1] = ("libzmq.so.5", "/usr/lib/x86_64-linux-gnu/libzmq.so.5")
        self.assertIn("usr/bin/QEGTRAIN: libzmq.so.5 resolves to /usr/lib/x86_64-linux-gnu/libzmq.so.5, outside the package", self.failure())

    def test_plugin_is_checked(self):
        self.add_closed_app_dir()
        self.libraries["libqxcb.so"][0] = ("libQt5XcbQpa.so.5", "/usr/lib/x86_64-linux-gnu/libQt5XcbQpa.so.5")
        message = self.failure()
        self.assertIn("usr/plugins/platforms/libqxcb.so: libQt5XcbQpa.so.5 resolves to", message)
        self.assertNotIn("usr/bin/QEGTRAIN", message)

    def test_file_that_is_not_an_elf_file_is_ignored(self):
        self.add_closed_app_dir()
        self.add_other("usr/bin/qt.conf", b"[Paths]\nPrefix = ..\n")
        self.add_other("usr/bin/Scenes/Paimpol/scene.json", b'{"name": "Paimpol"}')
        self.add_other("usr/plugins/platforms/README", b"")
        self.assertIn("3 ELF files", self.run_check())
        self.assertEqual(len(self.asked), 3)

    def test_link_is_not_an_elf_file(self):
        self.add("usr/bin/QEGTRAIN")
        path = self.app_dir / "usr/bin/QEGTRAIN"
        self.assertTrue(package_start_smoke.is_elf_file(path))
        with mock.patch.object(Path, "is_symlink", return_value=True):
            self.assertFalse(package_start_smoke.is_elf_file(path))

    def test_app_dir_without_an_elf_file_fails(self):
        self.add_other("usr/bin/qt.conf", b"[Paths]\n")
        self.assertIn("no ELF file below usr/bin or usr/plugins", self.failure())
        self.assertEqual(self.asked, [])

    def test_elf_files_that_list_no_qt_or_zeromq_library_fail(self):
        self.add("usr/bin/QEGTRAIN", [("libc.so.6", "/lib/x86_64-linux-gnu/libc.so.6")])
        self.assertIn("ldd lists no Qt or ZeroMQ library for any ELF file", self.failure())

    def test_elf_file_that_ldd_does_not_list_fails(self):
        self.add("usr/bin/QEGTRAIN")
        with contextlib.redirect_stdout(io.StringIO()), self.assertRaises(SystemExit) as raised:
            package_start_smoke.check_linux_libraries(self.app_dir, lambda path: "\tnot a dynamic executable\n")
        self.assertIn("ldd lists no Qt or ZeroMQ library", str(raised.exception))

    def test_all_problems_are_listed_in_one_message(self):
        self.add_closed_app_dir()
        self.libraries["QEGTRAIN"][0] = ("libQt5Core.so.5", "/usr/lib/x86_64-linux-gnu/libQt5Core.so.5")
        self.libraries["QEGTRAIN"][1] = ("libzmq.so.5", None)
        self.libraries["libqxcb.so"][0] = ("libQt5XcbQpa.so.5", None)
        message = self.failure()
        for line in ("usr/bin/QEGTRAIN: libQt5Core.so.5 resolves to", "usr/bin/QEGTRAIN: libzmq.so.5 is not found",
                     "usr/plugins/platforms/libqxcb.so: libQt5XcbQpa.so.5 is not found"):
            self.assertIn(line, message)


class ExportFilesTests(unittest.TestCase):
    def test_files_that_the_export_hooks_write(self):
        self.assertEqual(package_start_smoke.CSV_EXPORTS, ("trajectory.csv", "timetable.csv", "run_summary.csv"))
        self.assertEqual(package_start_smoke.PNG_EXPORTS, ("timetable_graph.png", "train_path_graph.png", "route_reference_chooser.png"))
        self.assertEqual(package_start_smoke.PNG_SIGNATURE, bytes([0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A]))


class CsvExportTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.directory = Path(self.temp.name)
        for name in package_start_smoke.CSV_EXPORTS:
            (self.directory / name).write_text("time,position\n0,1\n", encoding="utf-8")

    def tearDown(self):
        self.temp.cleanup()

    def failure(self):
        with self.assertRaises(SystemExit) as raised:
            package_start_smoke.check_csv_export(self.directory)
        return str(raised.exception)

    def test_files_with_a_header_and_a_data_line_pass(self):
        package_start_smoke.check_csv_export(self.directory)

    def test_files_with_crlf_line_ends_pass(self):
        for name in package_start_smoke.CSV_EXPORTS:
            (self.directory / name).write_bytes(b"time,position\r\n0,1\r\n")
        package_start_smoke.check_csv_export(self.directory)

    def test_missing_file_fails(self):
        (self.directory / "timetable.csv").unlink()
        self.assertIn("timetable.csv was not written", self.failure())

    def test_empty_file_fails(self):
        (self.directory / "run_summary.csv").write_bytes(b"")
        self.assertIn("run_summary.csv is empty", self.failure())

    def test_file_with_a_header_only_fails(self):
        for content in (b"time,position\n", b"time,position\r\n\r\n", b"time,position"):
            with self.subTest(content=content):
                (self.directory / "trajectory.csv").write_bytes(content)
                self.assertIn("trajectory.csv has a header line and no data line", self.failure())

    def test_header_without_a_comma_fails(self):
        (self.directory / "trajectory.csv").write_text("time\n0\n", encoding="utf-8")
        message = self.failure()
        self.assertIn("first line of", message)
        self.assertIn("trajectory.csv has no comma", message)

    def test_file_that_is_not_utf8_fails(self):
        (self.directory / "trajectory.csv").write_bytes(b"time,position\n\xff\xfe,1\n")
        self.assertIn("trajectory.csv is not UTF-8 text", self.failure())


class PngExportTests(unittest.TestCase):
    HEADER = package_start_smoke.PNG_SIGNATURE + b"\x00\x00\x00\rIHDR" + bytes(8)

    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.directory = Path(self.temp.name)
        for name in package_start_smoke.PNG_EXPORTS:
            (self.directory / name).write_bytes(self.HEADER)

    def tearDown(self):
        self.temp.cleanup()

    def failure(self):
        with self.assertRaises(SystemExit) as raised:
            package_start_smoke.check_png_export(self.directory)
        return str(raised.exception)

    def test_files_with_a_signature_and_a_header_chunk_pass(self):
        package_start_smoke.check_png_export(self.directory)

    def test_missing_file_fails(self):
        (self.directory / "train_path_graph.png").unlink()
        self.assertIn("train_path_graph.png was not written", self.failure())

    def test_empty_file_fails(self):
        (self.directory / "timetable_graph.png").write_bytes(b"")
        self.assertIn("timetable_graph.png does not start with the PNG signature", self.failure())

    def test_wrong_signature_fails(self):
        (self.directory / "route_reference_chooser.png").write_bytes(b"\xff\xd8\xff\xe0" + self.HEADER[4:])
        self.assertIn("route_reference_chooser.png does not start with the PNG signature", self.failure())

    def test_signature_only_fails(self):
        (self.directory / "timetable_graph.png").write_bytes(package_start_smoke.PNG_SIGNATURE)
        self.assertIn("timetable_graph.png has no IHDR chunk", self.failure())

    def test_other_first_chunk_fails(self):
        (self.directory / "timetable_graph.png").write_bytes(package_start_smoke.PNG_SIGNATURE + b"\x00\x00\x00\rIDAT" + bytes(8))
        self.assertIn("timetable_graph.png has no IHDR chunk", self.failure())


class AppDirTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.package = Path(self.temp.name)

    def tearDown(self):
        self.temp.cleanup()

    def add(self, relative):
        (self.package / relative).parent.mkdir(parents=True, exist_ok=True)
        (self.package / relative).write_bytes(b"program")

    def test_layout_of_an_extracted_appimage(self):
        self.add("AppRun")
        self.add("usr/bin/QEGTRAIN")
        self.assertEqual(package_start_smoke.find_package_layout(self.package),
                         (self.package / "AppRun", self.package / "usr/bin/Scenes/Paimpol"))

    def test_package_with_an_app_dir(self):
        self.add("AppRun")
        self.add("usr/bin/QEGTRAIN")
        self.assertEqual(package_start_smoke.find_app_dir(self.package), self.package)

    def test_package_without_apprun(self):
        self.add("usr/bin/QEGTRAIN")
        self.assertIsNone(package_start_smoke.find_app_dir(self.package))

    def test_package_without_the_program_below_usr_bin(self):
        self.add("AppRun")
        self.assertIsNone(package_start_smoke.find_app_dir(self.package))

    def test_package_of_another_kind(self):
        self.add("QEGTRAIN.exe")
        self.assertIsNone(package_start_smoke.find_app_dir(self.package))


class LaunchTests(unittest.TestCase):
    def launch(self, **keywords):
        completed = subprocess.CompletedProcess(["program"], 0, b"output\n")
        with mock.patch.object(subprocess, "run", return_value=completed) as run:
            text = package_start_smoke.launch(["program"], Path("."), {}, "window start", **keywords)
        return text, run

    def test_time_limit_is_the_run_limit_unless_one_is_given(self):
        text, run = self.launch()
        self.assertEqual(text, "output\n")
        self.assertEqual(run.call_args.kwargs["timeout"], package_start_smoke.RUN_TIMEOUT_SECONDS)
        _, run = self.launch(timeout=package_start_smoke.EXPORT_TIMEOUT_SECONDS)
        self.assertEqual(run.call_args.kwargs["timeout"], 240)

    def test_timeout_names_the_limit_that_was_used_and_shows_the_output(self):
        expired = subprocess.TimeoutExpired(["program"], 240, output=b"last output\n")
        with mock.patch.object(subprocess, "run", side_effect=expired):
            with self.assertRaises(SystemExit) as raised:
                package_start_smoke.launch(["program"], Path("."), {}, "CSV export", timeout=240)
        self.assertIn("CSV export timed out after 240s", str(raised.exception))
        self.assertIn("last output", str(raised.exception))


class MainTests(unittest.TestCase):
    """main() runs with the launches and the closure and library checks replaced, so no application starts. A launch
    that stands for an export writes the files that the hook writes, unless a test turns that off."""

    CSV_TEXT = "time,position\n0,1\n"
    PNG_BYTES = package_start_smoke.PNG_SIGNATURE + b"\x00\x00\x00\rIHDR" + bytes(8)

    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.package = Path(self.temp.name).resolve()
        self.calls = []
        self.launches = {}
        self.output = {"CSV export": "E2E_CSV_EXPORT_OK\n", "PNG export": "E2E_ROUTE_DIAGRAM_OK\n"}
        self.write_exports = True

    def tearDown(self):
        self.temp.cleanup()

    def add_package(self, executable, scenes):
        (self.package / executable).parent.mkdir(parents=True, exist_ok=True)
        (self.package / executable).write_bytes(b"program")
        (self.package / scenes / "Paimpol").mkdir(parents=True)
        (self.package / scenes / "Paimpol" / "scene.json").write_text("{}")

    def add_app_dir(self):
        self.add_package("AppRun", "usr/bin/Scenes")
        (self.package / "usr/bin/QEGTRAIN").write_bytes(b"program")

    def launch(self, command, cwd, env, what, timeout=package_start_smoke.RUN_TIMEOUT_SECONDS):
        self.calls.append(what)
        hooks = [name for name in ("QEGTRAIN_E2E_EXPORT_DIR", "QEGTRAIN_E2E_ROUTE_DIAGRAM") if name in env]
        self.launches[what] = {"command": command, "cwd": cwd, "env": dict(env), "timeout": timeout, "hooks": hooks,
                               "hook_exists": [Path(env[name]).is_dir() for name in hooks]}
        if what == "headless run":
            output = Path(env["QEGTRAIN_OUTPUT_DIR"]) / "Output" / "Paimpol"
            output.mkdir(parents=True)
            (output / "EnergyConsumptionPerTrain.txt").write_text("")
            return "End of Simulation"
        if what == "window start":
            return "first_runtime_paint"
        if self.write_exports and hooks == ["QEGTRAIN_E2E_EXPORT_DIR"]:
            for name in package_start_smoke.CSV_EXPORTS:
                (Path(env[hooks[0]]) / name).write_text(self.CSV_TEXT)
        if self.write_exports and hooks == ["QEGTRAIN_E2E_ROUTE_DIAGRAM"]:
            for name in package_start_smoke.PNG_EXPORTS:
                (Path(env[hooks[0]]) / name).write_bytes(self.PNG_BYTES)
        return self.output[what]

    def run_main(self, *arguments):
        with mock.patch.object(package_start_smoke, "launch", self.launch), \
                mock.patch.object(package_start_smoke, "check_bundle_closure", lambda app: self.calls.append(app)), \
                mock.patch.object(package_start_smoke, "check_linux_libraries", lambda app_dir: self.calls.append(app_dir)), \
                mock.patch.object(sys, "argv", ["package_start_smoke.py", str(self.package), *arguments]), \
                contextlib.redirect_stdout(io.StringIO()):
            package_start_smoke.main()

    def failure(self, *arguments):
        with self.assertRaises(SystemExit) as raised:
            self.run_main(*arguments)
        return str(raised.exception)

    def test_app_bundle_is_checked_before_the_launches(self):
        self.add_package("QEGTRAIN.app/Contents/MacOS/QEGTRAIN", "QEGTRAIN.app/Contents/Resources/Scenes")
        self.run_main()
        self.assertEqual(self.calls, [self.package / "QEGTRAIN.app", "headless run", "window start", "CSV export", "PNG export"])

    def test_package_without_an_app_bundle_is_only_launched(self):
        self.add_package("QEGTRAIN.exe", "Scenes")
        self.run_main()
        self.assertEqual(self.calls, ["headless run", "window start", "CSV export", "PNG export"])

    def test_app_dir_is_checked_before_the_launches_and_started_through_apprun(self):
        self.add_app_dir()
        self.run_main()
        self.assertEqual(self.calls, [self.package, "headless run", "window start", "CSV export", "PNG export"])
        for launch in self.launches.values():
            self.assertEqual(launch["command"][0], str(self.package / "AppRun"))
            self.assertEqual(launch["cwd"], self.package)
            self.assertEqual(launch["command"][launch["command"].index("--scene") + 1], str(self.package / "usr/bin/Scenes/Paimpol"))

    def test_export_launches_use_the_autostart_hook_and_one_export_hook(self):
        self.add_package("QEGTRAIN.exe", "Scenes")
        self.run_main()
        for what, hook in (("CSV export", "QEGTRAIN_E2E_EXPORT_DIR"), ("PNG export", "QEGTRAIN_E2E_ROUTE_DIAGRAM")):
            with self.subTest(what=what):
                launch = self.launches[what]
                command = launch["command"]
                self.assertEqual(launch["env"]["QEGTRAIN_AUTOSTART"], "1")
                self.assertEqual(launch["hooks"], [hook])
                self.assertEqual(launch["hook_exists"], [True])
                self.assertEqual(launch["timeout"], 240)
                self.assertEqual(command[0], str(self.package / "QEGTRAIN.exe"))
                self.assertEqual(command[command.index("--scene") + 1], str(self.package / "Scenes" / "Paimpol"))
                self.assertEqual(command[command.index("-h") + 1], "8000")
                self.assertEqual(command[command.index("-g") + 1], "1")
                for option in ("-pax", "-TSM", "-RC"):
                    self.assertEqual(command[command.index(option) + 1], "0")
                self.assertEqual(launch["cwd"], self.package)
                self.assertFalse("QT_PLUGIN_PATH" in launch["env"])
        directories = {launch["env"]["QEGTRAIN_OUTPUT_DIR"] for launch in self.launches.values()}
        self.assertEqual(len(directories), 4)
        self.assertNotEqual(self.launches["CSV export"]["env"]["QEGTRAIN_E2E_EXPORT_DIR"],
                            self.launches["PNG export"]["env"]["QEGTRAIN_E2E_ROUTE_DIAGRAM"])

    def test_headless_run_and_window_start_get_no_hook_and_keep_the_time_limit(self):
        self.add_package("QEGTRAIN.exe", "Scenes")
        self.run_main()
        for what in ("headless run", "window start"):
            with self.subTest(what=what):
                launch = self.launches[what]
                self.assertEqual(launch["hooks"], [])
                self.assertFalse("QEGTRAIN_AUTOSTART" in launch["env"])
                self.assertEqual(launch["timeout"], package_start_smoke.RUN_TIMEOUT_SECONDS)
        command = self.launches["headless run"]["command"]
        self.assertEqual(command[command.index("-g") + 1], "0")
        self.assertEqual(command[command.index("-h") + 1], package_start_smoke.HORIZON_SECONDS)
        self.assertEqual(self.launches["window start"]["env"]["QEGTRAIN_STARTUP_TIMING"], "1")

    def test_platform_reaches_the_launches_with_a_window_and_not_the_headless_run(self):
        self.add_package("QEGTRAIN.exe", "Scenes")
        self.run_main("--platform", "offscreen")
        self.assertFalse("QT_QPA_PLATFORM" in self.launches["headless run"]["env"])
        for what in ("window start", "CSV export", "PNG export"):
            self.assertEqual(self.launches[what]["env"]["QT_QPA_PLATFORM"], "offscreen")

    def test_no_platform_is_set_without_the_option(self):
        self.add_package("QEGTRAIN.exe", "Scenes")
        with mock.patch.dict(os.environ, {"QT_QPA_PLATFORM": "offscreen"}):
            self.run_main()
        for launch in self.launches.values():
            self.assertFalse("QT_QPA_PLATFORM" in launch["env"])

    def assert_export_without_the_marker_fails(self, what):
        self.add_package("QEGTRAIN.exe", "Scenes")
        self.output[what] = "warnings that were printed\nE2E_CSV_EXPORT_FAIL\n"
        message = self.failure()
        self.assertIn(f"{what} printed no", message)
        self.assertIn("warnings that were printed", message)

    def test_csv_export_without_the_marker_fails_with_the_tail_of_the_output(self):
        self.assert_export_without_the_marker_fails("CSV export")

    def test_png_export_without_the_marker_fails_with_the_tail_of_the_output(self):
        self.assert_export_without_the_marker_fails("PNG export")

    def test_export_that_wrote_no_file_fails(self):
        self.add_package("QEGTRAIN.exe", "Scenes")
        self.write_exports = False
        message = self.failure()
        self.assertIn("CSV export failed", message)
        self.assertIn("trajectory.csv was not written", message)
        self.assertIn("E2E_CSV_EXPORT_OK", message)

    def test_export_that_wrote_an_empty_file_fails(self):
        self.add_package("QEGTRAIN.exe", "Scenes")
        self.CSV_TEXT = ""
        self.assertIn("trajectory.csv is empty", self.failure())

    def test_png_export_does_not_run_when_the_csv_export_failed(self):
        self.add_package("QEGTRAIN.exe", "Scenes")
        self.output["CSV export"] = ""
        self.failure()
        self.assertEqual(self.calls, ["headless run", "window start", "CSV export"])

    def test_exports_do_not_run_when_the_window_start_failed(self):
        self.add_package("QEGTRAIN.exe", "Scenes")
        original = self.launch

        def failing(command, cwd, env, what, timeout=package_start_smoke.RUN_TIMEOUT_SECONDS):
            text = original(command, cwd, env, what, timeout)
            if what == "window start":
                raise SystemExit("window start exited with 1")
            return text

        self.launch = failing
        self.failure()
        self.assertEqual(self.calls, ["headless run", "window start"])


if __name__ == "__main__":
    unittest.main()
