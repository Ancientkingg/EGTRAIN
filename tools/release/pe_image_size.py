#!/usr/bin/env python3
"""Fail when a Windows executable's in-memory image is larger than a limit.

Reads SizeOfImage from the PE optional header. Windows maps an EXE image as one
view and refuses to start one near 2 GiB ("not a valid Win32 application"), so
this is checked after every Windows build. The stack reserve of the header can
be checked too. No third-party modules.

Usage: pe_image_size.py FILE --max-bytes N [--machine x64] [--subsystem windows]
           [--stack-reserve BYTES]
Exit status: 0 within limits, 1 a limit or expectation failed, 2 not a PE file.
"""
import argparse
import struct
import sys
from pathlib import Path

MACHINES = {0x014C: "x86", 0x8664: "x64", 0xAA64: "arm64"}
SUBSYSTEMS = {2: "windows", 3: "console"}
GIB = 1024 ** 3
# The optional header must reach the end of SizeOfStackReserve (offset 72, 4 or 8 bytes).
MIN_OPTIONAL_HEADER = {0x10B: 76, 0x20B: 80}


class NotPortableExecutable(ValueError):
    pass


def read_pe_header(path):
    """Return machine, magic, SizeOfImage, subsystem and stack reserve."""
    with open(path, "rb") as stream:
        head = stream.read(64)
        if len(head) < 64 or head[:2] != b"MZ":
            raise NotPortableExecutable("missing MZ header")
        (pe_offset,) = struct.unpack_from("<I", head, 0x3C)
        stream.seek(pe_offset)
        coff = stream.read(24)
        if len(coff) < 24 or coff[:4] != b"PE\0\0":
            raise NotPortableExecutable("missing PE signature")
        machine, _sections, _stamp, _symtab, _symbols, optional_size, _flags = struct.unpack_from(
            "<HHIIIHH", coff, 4)
        optional = stream.read(optional_size)
    if len(optional) < 2:
        raise NotPortableExecutable("optional header is missing")
    (magic,) = struct.unpack_from("<H", optional, 0)
    if magic not in MIN_OPTIONAL_HEADER:
        raise NotPortableExecutable(f"unknown optional header magic {magic:#x}")
    if len(optional) < MIN_OPTIONAL_HEADER[magic]:
        raise NotPortableExecutable("optional header is too small")
    # SizeOfImage and Subsystem have the same offsets in PE32 and PE32+.
    (size_of_image,) = struct.unpack_from("<I", optional, 56)
    (subsystem,) = struct.unpack_from("<H", optional, 68)
    stack_format = "<Q" if magic == 0x20B else "<I"
    (stack_reserve,) = struct.unpack_from(stack_format, optional, 72)
    return {
        "machine": MACHINES.get(machine, f"{machine:#06x}"),
        "pe32_plus": magic == 0x20B,
        "size_of_image": size_of_image,
        "subsystem": SUBSYSTEMS.get(subsystem, str(subsystem)),
        "stack_reserve": stack_reserve,
    }


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("exe", type=Path)
    parser.add_argument("--max-bytes", type=int, required=True)
    parser.add_argument("--machine", choices=sorted(MACHINES.values()))
    parser.add_argument("--subsystem", choices=sorted(SUBSYSTEMS.values()))
    parser.add_argument("--stack-reserve", type=int, help="expected SizeOfStackReserve in bytes")
    args = parser.parse_args(argv)
    try:
        info = read_pe_header(args.exe)
    except (OSError, NotPortableExecutable) as error:
        print(f"{args.exe}: not a readable PE file: {error}", file=sys.stderr)
        return 2
    size = info["size_of_image"]
    print(f"{args.exe.name}: machine={info['machine']} subsystem={info['subsystem']} "
          f"SizeOfImage={size} bytes ({size / GIB:.2f} GiB) limit={args.max_bytes} "
          f"stack_reserve={info['stack_reserve']}")
    failures = []
    if size > args.max_bytes:
        failures.append(f"SizeOfImage {size} exceeds {args.max_bytes}")
    if args.machine and info["machine"] != args.machine:
        failures.append(f"machine {info['machine']} is not {args.machine}")
    if args.subsystem and info["subsystem"] != args.subsystem:
        failures.append(f"subsystem {info['subsystem']} is not {args.subsystem}")
    if args.stack_reserve is not None and info["stack_reserve"] != args.stack_reserve:
        failures.append(f"stack reserve {info['stack_reserve']} is not {args.stack_reserve}")
    for failure in failures:
        print(f"FAIL: {failure}", file=sys.stderr)
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
