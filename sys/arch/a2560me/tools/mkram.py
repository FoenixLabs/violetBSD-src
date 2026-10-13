#!/usr/bin/env python3
"""Package a big-endian m68k ELF for FoenixMgr --run-m68k-bin."""
import json
from pathlib import Path
import struct
import sys

BASE = 0x100000
LIMIT = 0x600000  # Leave at least 2 MiB for bootstrap and VM.


def package(data):
    if len(data) < 52 or data[:7] != b'\x7fELF\x01\x02\x01':
        raise ValueError('expected big-endian ELF32')
    h = struct.unpack_from('>16sHHIIIIIHHHHHH', data)
    if h[1:4] != (2, 4, 1) or h[9] != 32 or not h[10]:
        raise ValueError('expected executable m68k ELF with program headers')
    entry, phoff, phnum = h[4], h[5], h[10]
    if phoff + phnum * 32 > len(data):
        raise ValueError('truncated program headers')
    segments = []
    for i in range(phnum):
        typ, off, va, pa, size, mem, flags, align = struct.unpack_from('>8I', data, phoff + i * 32)
        if typ != 1:
            continue
        if va != pa or va < BASE or va + mem >= LIMIT or size > mem or off + size > len(data):
            raise ValueError('invalid or out-of-range RAM LOAD segment')
        if any(va < s['address'] + s['memory_bytes'] and s['address'] < va + mem for s in segments):
            raise ValueError('overlapping RAM segments')
        segments.append(dict(address=va, offset=off, file_bytes=size, memory_bytes=mem, flags=flags))
    if not segments or min(s['address'] for s in segments) != BASE:
        raise ValueError('missing RAM vectors')
    if entry != BASE + 0x400 or not any(s['flags'] & 1 and s['address'] <= entry < s['address'] + s['file_bytes'] for s in segments):
        raise ValueError('invalid RAM entry')
    image = bytearray(max(s['address'] + s['file_bytes'] for s in segments) - BASE)
    for s in segments:
        n, off, address = s['file_bytes'], s['offset'], s['address'] - BASE
        image[address:address + n] = data[off:off + n]
    if struct.unpack_from('>II', image) != (0x10000, entry):
        raise ValueError('invalid initial stack/reset vectors')
    return image, dict(load_address=BASE, entry=entry, bytes=len(image), segments=segments)


if __name__ == '__main__':
    try:
        image, report = package(Path(sys.argv[1]).read_bytes())
        out = Path(sys.argv[2])
        out.write_bytes(image)
        out.with_suffix(out.suffix + '.json').write_text(json.dumps(report, indent=2) + '\n')
        print(f'{out}: {len(image)} bytes, load {BASE:#x}, entry {report["entry"]:#x}')
    except (ValueError, OSError) as exc:
        sys.exit(str(exc))
