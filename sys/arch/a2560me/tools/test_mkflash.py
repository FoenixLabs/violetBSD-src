#!/usr/bin/env python3
"""Exercise rejection of corrupted flash images using the actual linked kernel."""
import struct
import sys
import unittest
from pathlib import Path
from mkflash import package, FLASH_BASE, FLASH_SIZE

KERNEL = Path(sys.argv.pop(1)).read_bytes()


class FlashTests(unittest.TestCase):
    def test_actual_image(self):
        image, report = package(KERNEL)
        self.assertEqual(len(image), FLASH_SIZE)
        self.assertEqual(struct.unpack_from('>II', image), (0x10000, 0xffc10000))
        self.assertLess(report['flash_used_bytes'], FLASH_SIZE)

    def reject(self, change):
        data = bytearray(KERNEL)
        change(data)
        with self.assertRaises(ValueError):
            package(data)

    def test_wrong_endian(self):
        self.reject(lambda d: d.__setitem__(5, 1))

    def test_truncated(self):
        self.reject(lambda d: d.__delitem__(slice(100, None)))

    def test_flash_offset_is_not_a_cpu_address(self):
        ph = struct.unpack_from('>I', KERNEL, 28)[0]
        self.reject(lambda d: struct.pack_into('>I', d, ph + 12, 0))

    def test_flash_overlap(self):
        ph = struct.unpack_from('>I', KERNEL, 28)[0]
        self.reject(lambda d: struct.pack_into('>I', d, ph + 32 + 12, FLASH_BASE))

    def test_sram_limit(self):
        ph = struct.unpack_from('>I', KERNEL, 28)[0]
        num = struct.unpack_from('>H', KERNEL, 44)[0]
        rw = next(ph + 32*i for i in range(num)
                  if struct.unpack_from('>I', KERNEL, ph+32*i+24)[0] & 2)
        self.reject(lambda d: struct.pack_into('>I', d, rw + 8, 0x800000))

    def test_reset_vector(self):
        ph = struct.unpack_from('>I', KERNEL, 28)[0]
        off = struct.unpack_from('>I', KERNEL, ph + 4)[0]
        self.reject(lambda d: struct.pack_into('>I', d, off + 4, 0))


unittest.main()
