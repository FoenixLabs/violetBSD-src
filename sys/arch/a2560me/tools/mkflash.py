#!/usr/bin/env python3
"""Validate and package a big-endian ELF32/m68k XIP kernel, without flashing it."""
import argparse
import json
import struct
from pathlib import Path

FLASH_BASE = 0xFFC00000
FLASH_SIZE = 0x400000
SRAM_SIZE = 0x800000
COPY_SIZE = 0x10000


def package(data):
    if len(data) < 52 or data[:7] != b'\x7fELF\x01\x02\x01':
        raise ValueError('expected big-endian ELF32')
    hdr = struct.unpack_from('>16sHHIIIIIHHHHHH', data)
    _, kind, machine, version, entry, phoff, _, _, _, phsize, phnum, _, _, _ = hdr
    if kind != 2 or machine != 4 or version != 1 or phsize != 32:
        raise ValueError('expected executable m68k ELF with standard program headers')
    if phnum == 0 or phoff + phnum * phsize > len(data):
        raise ValueError('missing or truncated program headers')
    image = bytearray(b'\xff' * FLASH_SIZE)
    flash_ranges = []
    ram_ranges = []
    segments = []
    executable = []
    for i in range(phnum):
        typ, off, va, pa, filesz, memsz, flags, align = struct.unpack_from(
            '>8I', data, phoff + i * phsize)
        if typ != 1:
            continue
        if filesz > memsz or off + filesz > len(data):
            raise ValueError('truncated or inconsistent LOAD segment')
        if not FLASH_BASE <= pa < FLASH_BASE + FLASH_SIZE or pa + filesz > 2**32:
            raise ValueError('LOAD image falls outside flash')
        for start, end in flash_ranges:
            if filesz and pa < end and start < pa + filesz:
                raise ValueError('overlapping flash LOAD segments')
        if filesz:
            flash_ranges.append((pa, pa + filesz))
            image[pa - FLASH_BASE:pa - FLASH_BASE + filesz] = data[off:off + filesz]
        if flags & 2:
            if flags & 1 or va < COPY_SIZE or va + memsz > SRAM_SIZE:
                raise ValueError('writable segment outside reserved SRAM or executable')
            if not filesz or va & 3 or filesz & 3:
                raise ValueError('reset copy requires aligned initialized SRAM data')
            ram_ranges.append((va, va + memsz))
        else:
            if va != pa or not FLASH_BASE <= va < 2**32 or va + memsz > 2**32:
                raise ValueError('read-only segment is not XIP in flash')
            if memsz != filesz:
                raise ValueError('uninitialized read-only flash segment')
        if flags & 1:
            executable.append((va, va + filesz))
        segments.append(dict(virtual=hex(va), load=hex(pa), file_bytes=filesz,
                             memory_bytes=memsz, flags=flags))
    if not executable or not ram_ranges:
        raise ValueError('missing executable flash or writable SRAM segment')
    for ranges in (ram_ranges,):
        ordered = sorted(ranges)
        if any(b[0] < a[1] for a, b in zip(ordered, ordered[1:])):
            raise ValueError('overlapping SRAM segments')
    if len(ram_ranges) != 1 or ram_ranges[0][0] != COPY_SIZE:
        raise ValueError('reset loader expects one SRAM segment starting at 64 KiB')
    sp, pc = struct.unpack_from('>II', image)
    if sp != COPY_SIZE or pc != entry:
        raise ValueError('reset SSP/PC do not match firmware contract and ELF entry')
    if not any(a <= pc < b for a, b in executable):
        raise ValueError('reset PC is not executable flash')
    vectors = struct.unpack_from('>256I', image)
    if any(not any(a <= pc < b for a, b in executable) for pc in vectors[2:]):
        raise ValueError('early exception vectors must target executable flash')
    used = max(b for a, b in flash_ranges) - FLASH_BASE
    return bytes(image), dict(flash_base=hex(FLASH_BASE), programmer_offset=0,
                             image_bytes=FLASH_SIZE, flash_used_bytes=used,
                             initial_ssp=hex(sp), reset_pc=hex(entry),
                             static_sram_end=hex(max(b for a, b in ram_ranges)),
                             segments=segments)


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('kernel', type=Path)
    ap.add_argument('output', type=Path)
    args = ap.parse_args()
    try:
        image, report = package(args.kernel.read_bytes())
    except (ValueError, struct.error) as exc:
        ap.error(str(exc))
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(image)
    args.output.with_suffix('.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()
