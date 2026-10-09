#!/usr/bin/env python3
import contextlib
import io
import sys
import tempfile
import unittest
from pathlib import Path

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


if __name__ == "__main__":
    unittest.main()
