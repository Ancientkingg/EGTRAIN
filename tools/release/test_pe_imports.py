#!/usr/bin/env python3
import contextlib
import io
import struct
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import pe_imports

SECTION_RVA = 0x1000
SECTION_FILE_OFFSET = 0x200


def make_pe(path, imports=(), delay_imports=(), plus=True):
    """Write a small PE file: one section that holds the import and delay-load tables and the DLL names."""
    optional_size = 240 if plus else 224
    directories = 112 if plus else 96
    optional = bytearray(optional_size)
    struct.pack_into("<H", optional, 0, 0x20B if plus else 0x10B)
    struct.pack_into("<I", optional, directories - 4, 16)
    # The section starts with the import descriptors (20 bytes each, then a zero one), then the
    # delay-load descriptors (32 bytes each, then a zero one), then the names.
    import_size = 20 * (len(imports) + 1)
    delay_size = 32 * (len(delay_imports) + 1)
    section = bytearray(import_size + delay_size)
    for index, name in enumerate(imports):
        struct.pack_into("<I", section, 20 * index + 12, SECTION_RVA + len(section))
        section += name.encode("ascii") + b"\0"
    for index, name in enumerate(delay_imports):
        struct.pack_into("<I", section, import_size + 32 * index + 4, SECTION_RVA + len(section))
        section += name.encode("ascii") + b"\0"
    if imports:
        struct.pack_into("<II", optional, directories + 8 * 1, SECTION_RVA, import_size)
    if delay_imports:
        struct.pack_into("<II", optional, directories + 8 * 13, SECTION_RVA + import_size, delay_size)
    dos = bytearray(64)
    dos[:2] = b"MZ"
    struct.pack_into("<I", dos, 0x3C, 64)
    coff = b"PE\0\0" + struct.pack("<HHIIIHH", 0x8664 if plus else 0x14C, 1, 0, 0, 0, optional_size, 0x22)
    header = struct.pack("<8sIIII12x", b".idata", len(section), SECTION_RVA, len(section), SECTION_FILE_OFFSET)
    head = bytes(dos) + coff + bytes(optional) + header
    path.write_bytes(head + bytes(SECTION_FILE_OFFSET - len(head)) + bytes(section))


def run(directory, *args):
    out, err = io.StringIO(), io.StringIO()
    with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
        code = pe_imports.main([str(directory), *args])
    return code, out.getvalue(), err.getvalue()


class PeImportsTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.package = Path(self.temp.name)

    def test_reads_the_imports_of_pe32_plus_and_pe32(self):
        make_pe(self.package / "a.exe", ["KERNEL32.dll", "VCRUNTIME140.dll"])
        self.assertEqual(pe_imports.read_imports(self.package / "a.exe"), ["KERNEL32.dll", "VCRUNTIME140.dll"])
        make_pe(self.package / "b.dll", ["MSVCP140.dll"], plus=False)
        self.assertEqual(pe_imports.read_imports(self.package / "b.dll"), ["MSVCP140.dll"])

    def test_reads_delay_load_imports(self):
        make_pe(self.package / "a.exe", ["kernel32.dll"], ["winhttp.dll", "vcruntime140_1.dll"])
        self.assertEqual(pe_imports.read_imports(self.package / "a.exe"),
                         ["kernel32.dll", "winhttp.dll", "vcruntime140_1.dll"])

    def test_a_file_without_imports_has_none(self):
        make_pe(self.package / "a.dll")
        self.assertEqual(pe_imports.read_imports(self.package / "a.dll"), [])

    def test_system_dlls_and_shipped_dlls_pass(self):
        make_pe(self.package / "QEGTRAIN.exe", ["KERNEL32.dll", "api-ms-win-crt-runtime-l1-1-0.dll", "Qt6Core.dll", "ucrtbase.dll"])
        make_pe(self.package / "Qt6Core.dll", ["kernel32.dll"])
        code, out, err = run(self.package)
        self.assertEqual((code, err), (0, ""))
        self.assertIn("OK", out)

    def test_runtime_dlls_that_are_not_shipped_fail(self):
        make_pe(self.package / "QEGTRAIN.exe", ["KERNEL32.dll", "VCRUNTIME140.dll", "MSVCP140.dll"])
        (self.package / "plugins").mkdir()
        make_pe(self.package / "plugins" / "q.dll", ["VCRUNTIME140.dll"])
        code, _, err = run(self.package)
        self.assertEqual(code, 1)
        self.assertIn("vcruntime140.dll is imported by QEGTRAIN.exe, plugins/q.dll", err)
        self.assertIn("msvcp140.dll is imported by QEGTRAIN.exe", err)
        self.assertEqual(run(self.package, "--missing")[1], "msvcp140.dll\nvcruntime140.dll\n")

    def test_shipping_the_runtime_dlls_passes(self):
        make_pe(self.package / "QEGTRAIN.exe", ["VCRUNTIME140.dll", "MSVCP140.dll"])
        for name in ("vcruntime140.dll", "msvcp140.dll"):
            make_pe(self.package / name, ["kernel32.dll"])
        self.assertEqual(run(self.package)[0], 0)

    def test_a_missing_delay_load_dll_fails(self):
        make_pe(self.package / "a.exe", ["kernel32.dll"], ["libmissing.dll"])
        self.assertEqual(run(self.package, "--missing")[1], "libmissing.dll\n")

    def test_files_that_are_not_programs_are_not_read(self):
        (self.package / "notes.txt").write_text("VCRUNTIME140.dll")
        self.assertEqual(run(self.package)[0], 0)

    def test_damaged_and_missing_input_exits_2(self):
        (self.package / "bad.dll").write_bytes(b"\x7fELF" + bytes(100))
        self.assertEqual(run(self.package)[0], 2)
        make_pe(self.package / "bad.dll", ["a.dll"])
        data = (self.package / "bad.dll").read_bytes()
        # Cut inside the name of the DLL, which follows the 40 and 32 byte tables.
        (self.package / "bad.dll").write_bytes(data[:SECTION_FILE_OFFSET + 72 + 3])
        self.assertEqual(run(self.package)[0], 2)
        # Cut right after the COFF header, before the optional header.
        (self.package / "bad.dll").write_bytes(data[:64 + 24])
        self.assertEqual(run(self.package)[0], 2)
        self.assertEqual(run(self.package / "missing")[0], 2)


if __name__ == "__main__":
    unittest.main()
