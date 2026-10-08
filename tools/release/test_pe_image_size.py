#!/usr/bin/env python3
import contextlib
import io
import struct
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import pe_image_size


def make_pe(path, *, size_of_image, machine=0x8664, plus=True, subsystem=2, stack=10_000_000_000,
            optional_size=None):
    """Write a minimal PE header: DOS stub pointer, COFF header, optional header."""
    optional = bytearray(240 if plus else 224)
    struct.pack_into("<H", optional, 0, 0x20B if plus else 0x10B)
    struct.pack_into("<I", optional, 56, size_of_image)
    struct.pack_into("<H", optional, 68, subsystem)
    struct.pack_into("<Q" if plus else "<I", optional, 72, stack if plus else min(stack, 2**31))
    if optional_size is not None:
        del optional[optional_size:]
    dos = bytearray(64)
    dos[:2] = b"MZ"
    struct.pack_into("<I", dos, 0x3C, 64)
    coff = b"PE\0\0" + struct.pack("<HHIIIHH", machine, 0, 0, 0, 0, len(optional), 0x22)
    path.write_bytes(bytes(dos) + coff + bytes(optional))


def run(path, *args):
    out, err = io.StringIO(), io.StringIO()
    with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
        code = pe_image_size.main([str(path), *args])
    return code, out.getvalue(), err.getvalue()


class PeImageSizeTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.exe = Path(self.temp.name) / "QEGTRAIN.exe"

    def test_reads_current_release_size_from_pe32_plus(self):
        make_pe(self.exe, size_of_image=1_797_931_008)
        info = pe_image_size.read_pe_header(self.exe)
        self.assertEqual(info["size_of_image"], 1_797_931_008)
        self.assertEqual((info["machine"], info["subsystem"]), ("x64", "windows"))
        self.assertEqual(info["stack_reserve"], 10_000_000_000)

    def test_reads_pe32(self):
        make_pe(self.exe, size_of_image=123_456, machine=0x14C, plus=False)
        self.assertEqual(pe_image_size.read_pe_header(self.exe)["size_of_image"], 123_456)

    def test_limit_is_inclusive(self):
        make_pe(self.exe, size_of_image=1000)
        self.assertEqual(run(self.exe, "--max-bytes", "1000")[0], 0)
        code, _, err = run(self.exe, "--max-bytes", "999")
        self.assertEqual(code, 1)
        self.assertIn("exceeds", err)

    def test_machine_and_subsystem_expectations(self):
        make_pe(self.exe, size_of_image=1, machine=0x14C, plus=False, subsystem=3)
        code, _, err = run(self.exe, "--max-bytes", "10", "--machine", "x64", "--subsystem", "windows")
        self.assertEqual(code, 1)
        self.assertIn("machine x86", err)
        self.assertIn("subsystem console", err)

    def test_non_pe_and_missing_files_exit_2(self):
        self.exe.write_bytes(b"\x7fELF" + bytes(100))
        self.assertEqual(run(self.exe, "--max-bytes", "1")[0], 2)
        self.assertEqual(run(self.exe.with_name("missing.exe"), "--max-bytes", "1")[0], 2)

    def test_truncated_optional_header_exits_2(self):
        make_pe(self.exe, size_of_image=1, optional_size=60)
        self.assertEqual(run(self.exe, "--max-bytes", "10")[0], 2)
        make_pe(self.exe, size_of_image=1, optional_size=0)
        self.assertEqual(run(self.exe, "--max-bytes", "10")[0], 2)


if __name__ == "__main__":
    unittest.main()
