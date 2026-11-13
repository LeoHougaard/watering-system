"""Check partition/image rejection using ESP-IDF's real partition parser."""
import os
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
IDF = Path(os.environ.get('IDF_PATH', str(Path.home()/'esp/v5.5.1/esp-idf')))
sys.path.insert(0, str(IDF/'components/partition_table'))
sys.path.insert(0, str(ROOT/'tools'))
from gen_esp32part import PartitionTable
from verify_firmware_size import APP_BYTES, verify_sizes


class FirmwareSizeTests(unittest.TestCase):
    def table(self):
        return PartitionTable.from_csv((ROOT/'partitions.csv').read_text())

    def test_exact_partition_capacity_and_headroom(self):
        rows = verify_sizes(self.table(), APP_BYTES, 0x7000, 0xC00)
        self.assertEqual([r['free_bytes'] for r in rows], [0, 0, 0])
        rows = verify_sizes(self.table(), APP_BYTES-1, 0x7000, 0xC00)
        self.assertEqual([r['free_bytes'] for r in rows], [1, 1, 1])

    def test_overflow_and_missing_image_fail(self):
        for size in (0, APP_BYTES+1):
            with self.subTest(size=size), self.assertRaises(ValueError):
                verify_sizes(self.table(), size, 0x7000, 0xC00)

    def test_bootloader_and_partition_overlap_fail(self):
        for boot, partition in ((0x8001, 0xC00), (0x7000, 0x1001)):
            with self.subTest(boot=boot, partition=partition), self.assertRaises(ValueError):
                verify_sizes(self.table(), 1000, boot, partition)

    def test_changed_layout_fails(self):
        table = self.table()
        next(p for p in table if p.name == 'ota_1').size -= 0x1000
        with self.assertRaises(ValueError):
            verify_sizes(table, 1000, 0x7000, 0xC00)


if __name__ == '__main__':
    unittest.main()
