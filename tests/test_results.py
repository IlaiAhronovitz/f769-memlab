"""Host tests for tools/results.py: build a results block by hand, parse it,
and check the numbers that end up in the tables."""
import os
import struct
import sys
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "tools"))
import results  # noqa: E402


def make_blob(rows, status=results.ST_DONE, magic=results.MAGIC, version=1):
    header = [0] * results.HEADER_WORDS
    header[0], header[1], header[2], header[3] = magic, version, status, len(rows)
    header[4] = 0x10010451                 # REV_ID 0x1001, DEV_ID 0x451
    header[5] = 0x411FC271                 # Cortex-M7 r1p1
    # CCSIDR for 16 KiB, 4-way, 32-byte lines: sets-1 = 127, ways-1 = 3, line code 1
    header[7] = (127 << 13) | (3 << 3) | 1
    header[8] = (255 << 13) | (1 << 3) | 1   # 16 KiB, 2-way, 32-byte lines
    header[10] = 216000000
    blob = struct.pack(f"<{results.HEADER_WORDS}I", *header)
    for r in rows:
        blob += results.ROW.pack(*r)
    return blob


class ParseTest(unittest.TestCase):
    def test_header_and_rows(self):
        blob = make_blob([(1, 1, 0, 1024, 16384, 16384, 0x20008000)])
        res = results.parse(blob)
        self.assertEqual(res.status, results.ST_DONE)
        self.assertEqual(len(res.rows), 1)
        self.assertAlmostEqual(res.rows[0].per_op(), 1.0)

    def test_bad_magic(self):
        with self.assertRaises(ValueError):
            results.parse(make_blob([], magic=0))

    def test_version_mismatch(self):
        with self.assertRaises(ValueError):
            results.parse(make_blob([], version=2))

    def test_truncated_rows_are_dropped(self):
        blob = make_blob([(2, 2, 2, 8192, 65536, 65536, 0)])
        blob = blob[:-4]                     # the only row is now incomplete
        self.assertEqual(results.parse(blob).rows, [])

    def test_cache_geometry(self):
        self.assertEqual(results.cache_geometry((127 << 13) | (3 << 3) | 1), (16384, 4, 32))


class FormatTest(unittest.TestCase):
    def test_bytes_per_cycle(self):
        row = results.Row(2, 2, 2, 8192, 65536, 65536, 0)   # 4 bytes per cycle
        self.assertAlmostEqual(row.bytes_per_cycle(), 4.0)

    def test_markdown_has_every_section(self):
        rows = [(1, 2, 0, 1024, 16384, 98304, 0), (1, 2, 2, 1024, 16384, 16384, 0),
                (2, 2, 2, 8192, 65536, 32768, 0), (3, 2, 2, 8192, 65536, 65536, 0),
                (10, 7, 2, 8192, 2048, 900, 2048), (11, 7, 2, 8192, 2048, 1200, 0)]
        text = results.to_markdown(results.parse(make_blob(rows)))
        self.assertIn("Pointer chase, SRAM1", text)
        self.assertIn("| 1 KiB | 6.00 | 1.00 |", text)
        self.assertIn("| SRAM1 | 8 KiB | I+D, WB | 8.00 | 4.00 |", text)
        self.assertIn("| DMA, no maintenance | I+D, WB | 900 | 2048 |", text)
        self.assertIn("16 KiB, 4-way, 32-byte lines", text)
        self.assertIn("core r1p1", text)


if __name__ == "__main__":
    unittest.main()
