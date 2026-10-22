#!/usr/bin/env python3
"""Fail when a Windows binary of a package imports a DLL that the package does not hold.

Reads the import table and the delay-load import table of every .exe and .dll
below a directory. A DLL passes when the package holds a file of that name or
when Windows provides it (SYSTEM_DLLS). The Visual C++ runtime is not one of
them: the installer installs per user and cannot run the redistributable, so
the package carries the runtime DLLs next to the program. No third-party modules.

Usage: pe_imports.py DIR [--missing]
Exit status: 0 nothing missing, 1 a DLL is missing, 2 not a directory or a PE file is damaged.
--missing prints only the names of the missing DLLs, one per line.
"""
import argparse
import struct
import sys
from pathlib import Path

# DLLs of Windows 10 and 11 that the program files import. The Universal CRT is part of the
# operating system, so ucrtbase.dll and the api-ms-win-* and ext-ms-win-* API sets are listed
# by prefix. Names are lower case.
SYSTEM_DLLS = frozenset(
    name.lower()
    for name in (
        "kernel32.dll", "kernelbase.dll", "ntdll.dll", "user32.dll", "gdi32.dll", "gdi32full.dll",
        "advapi32.dll", "shell32.dll", "ole32.dll", "oleaut32.dll", "ws2_32.dll", "mswsock.dll",
        "winmm.dll", "version.dll", "dwmapi.dll", "uxtheme.dll", "imm32.dll", "netapi32.dll",
        "userenv.dll", "wtsapi32.dll", "mpr.dll", "crypt32.dll", "winhttp.dll", "bcrypt.dll",
        "iphlpapi.dll", "setupapi.dll", "comdlg32.dll", "comctl32.dll", "shlwapi.dll", "rpcrt4.dll",
        "secur32.dll", "psapi.dll", "dnsapi.dll", "d3d9.dll", "d3d11.dll", "dxgi.dll", "opengl32.dll",
        "glu32.dll", "ucrtbase.dll", "msvcrt.dll",
        "authz.dll", "d3d12.dll", "dwrite.dll", "ncrypt.dll", "uiautomationcore.dll",
    )
)
SYSTEM_DLL_PREFIXES = ("api-ms-win-", "ext-ms-win-")

DIRECTORY_IMPORT = 1
DIRECTORY_DELAY_IMPORT = 13
# The import and delay-load descriptors are 20 and 32 bytes long.
IMPORT_DESCRIPTOR = 20
DELAY_DESCRIPTOR = 32
MAX_DESCRIPTORS = 4096


class NotPortableExecutable(ValueError):
    pass


def _sections(data, pe_offset, section_count, optional_size):
    table = pe_offset + 24 + optional_size
    sections = []
    for index in range(section_count):
        entry = data[table + 40 * index:table + 40 * (index + 1)]
        if len(entry) < 40:
            raise NotPortableExecutable("section table is cut off")
        virtual_size, virtual_address, raw_size, raw_pointer = struct.unpack_from("<IIII", entry, 8)
        sections.append((virtual_address, max(virtual_size, raw_size), raw_pointer))
    return sections


def _offset(sections, rva):
    for address, size, pointer in sections:
        if address <= rva < address + size:
            return pointer + rva - address
    raise NotPortableExecutable(f"address {rva:#x} is in no section")


def _name(data, sections, rva):
    start = _offset(sections, rva)
    end = data.find(b"\0", start)
    if end < 0:
        raise NotPortableExecutable("a DLL name is not terminated")
    return data[start:end].decode("ascii", "replace")


def read_imports(path):
    """Return the DLL names (as written) that the PE file imports, with delay-load imports."""
    data = Path(path).read_bytes()
    if len(data) < 64 or data[:2] != b"MZ":
        raise NotPortableExecutable("missing MZ header")
    (pe_offset,) = struct.unpack_from("<I", data, 0x3C)
    if data[pe_offset:pe_offset + 4] != b"PE\0\0" or len(data) < pe_offset + 24:
        raise NotPortableExecutable("missing PE signature")
    section_count, optional_size = struct.unpack_from("<H12xH", data, pe_offset + 6)
    optional = pe_offset + 24
    if optional_size < 2:
        raise NotPortableExecutable("optional header is missing")
    (magic,) = struct.unpack_from("<H", data, optional)
    # The data directories follow the fixed fields of the optional header.
    directories = optional + {0x10B: 96, 0x20B: 112}.get(magic, -1)
    if directories < optional:
        raise NotPortableExecutable(f"unknown optional header magic {magic:#x}")
    (directory_count,) = struct.unpack_from("<I", data, directories - 4)
    sections = _sections(data, pe_offset, section_count, optional_size)
    names = []
    for index, size in ((DIRECTORY_IMPORT, IMPORT_DESCRIPTOR), (DIRECTORY_DELAY_IMPORT, DELAY_DESCRIPTOR)):
        if index >= directory_count:
            continue
        (rva, _length) = struct.unpack_from("<II", data, directories + 8 * index)
        if rva == 0:
            continue
        descriptor = _offset(sections, rva)
        for _ in range(MAX_DESCRIPTORS):
            fields = data[descriptor:descriptor + size]
            if len(fields) < size or not any(fields):
                break
            # The name is the fourth field of an import descriptor and the second of a delay-load one.
            (name_rva,) = struct.unpack_from("<I", fields, 12 if index == DIRECTORY_IMPORT else 4)
            names.append(_name(data, sections, name_rva))
            descriptor += size
    return names


def is_system_dll(name):
    lower = name.lower()
    return lower in SYSTEM_DLLS or lower.startswith(SYSTEM_DLL_PREFIXES)


def find_missing(directory):
    """Return {lower case DLL name: [relative paths of the files that import it]} for the DLLs missing from the package."""
    root = Path(directory)
    # Sort by the text of the relative path: the order of Path objects differs between Windows and POSIX.
    binaries = sorted(
        (path for path in root.rglob("*") if path.is_file() and path.suffix.lower() in (".exe", ".dll")),
        key=lambda path: path.relative_to(root).as_posix(),
    )
    shipped = {path.name.lower() for path in binaries}
    missing = {}
    for path in binaries:
        for name in read_imports(path):
            lower = name.lower()
            if lower not in shipped and not is_system_dll(lower):
                importers = missing.setdefault(lower, [])
                relative = path.relative_to(root).as_posix()
                if relative not in importers:
                    importers.append(relative)
    return missing


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("directory", type=Path)
    parser.add_argument("--missing", action="store_true", help="print only the missing DLL names")
    args = parser.parse_args(argv)
    if not args.directory.is_dir():
        print(f"{args.directory}: not a directory", file=sys.stderr)
        return 2
    try:
        missing = find_missing(args.directory)
    except (OSError, NotPortableExecutable, struct.error) as error:
        print(f"cannot read the imports: {error}", file=sys.stderr)
        return 2
    if args.missing:
        for name in sorted(missing):
            print(name)
        return 1 if missing else 0
    for name in sorted(missing):
        print(f"FAIL: {name} is imported by {', '.join(missing[name])} and is neither in the package nor a Windows system DLL",
              file=sys.stderr)
    if not missing:
        print(f"OK: every DLL that a program file in {args.directory} imports is in the package or part of Windows")
    return 1 if missing else 0


if __name__ == "__main__":
    sys.exit(main())
