#!/usr/bin/env python3
"""Validate the actual RAM ELF and reject unsafe upload layouts."""
import struct
import sys
import unittest
from pathlib import Path
from mkram import package

ELF = Path(sys.argv.pop(1)).read_bytes()


class RamImageTests(unittest.TestCase):
    def test_vectors_and_payload(self):
        image, report = package(ELF)
        self.assertEqual(struct.unpack_from('>II', image), (0x10000, 0x100400))
        for segment in report['segments']:
            address, size, off = segment['address'], segment['file_bytes'], segment['offset']
            self.assertEqual(image[address - 0x100000:address - 0x100000 + size], ELF[off:off + size])

    def test_wrong_entry(self):
        data = bytearray(ELF)
        struct.pack_into('>I', data, 24, 0xffc10000)
        with self.assertRaises(ValueError):
            package(data)

    def test_flash_load_address(self):
        data = bytearray(ELF)
        phoff = struct.unpack_from('>I', data, 28)[0]
        struct.pack_into('>I', data, phoff + 12, 0xffc00000)
        with self.assertRaises(ValueError):
            package(data)

    def test_truncated(self):
        with self.assertRaises(ValueError):
            package(ELF[:52])


if __name__ == '__main__':
    unittest.main()
