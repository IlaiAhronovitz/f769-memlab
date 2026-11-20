"""Host tests for tools/disasm_check.py: the check that keeps the timed code's
machine code and the image's layout unchanged (docs/CODING-STANDARD.md)."""
import os
import sys
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "tools"))
import disasm_check as dc  # noqa: E402

BASE = """
00000100 <bench_chase>:
 100:	b510      	push	{r4, lr}
 102:	d1fb      	bne.n	100 <bench_chase>

08000200 <main>:
 8000200:	f000 f8f0 	bl	80003e4 <res_add>
 8000204:	4b01      	ldr	r3, [pc, #4]	@ (800020c <main+0xc>)

08000300 <dma_copy.constprop.0>:
 8000300:	2300      	movs	r3, #0
"""


class NormalizeTest(unittest.TestCase):
    def test_moved_call_target_is_ignored(self):
        self.assertEqual(dc.normalize("bl\t80003e4 <res_add>"), "bl <res_add>")
        self.assertEqual(dc.normalize("bl\t80004e8 <res_add>"), "bl <res_add>")

    def test_literal_comment_address_is_ignored(self):
        self.assertEqual(dc.normalize("ldr\tr3, [pc, #4]\t@ (800020c <main+0xc>)"),
                         "ldr r3, [pc, #4] @ (<main+0xc>)")

    def test_clone_and_static_numbers_are_dropped(self):
        self.assertEqual(dc.base_name("dma_copy.constprop.0"), "dma_copy")
        self.assertEqual(dc.base_name("k_cfgs.2"), "k_cfgs")
        self.assertEqual(dc.base_name("main"), "main")


class CompareTest(unittest.TestCase):
    def test_same_code_at_new_addresses_passes(self):
        moved = BASE.replace("80003e4", "80004e8").replace("8000200:", "8000240:")
        rows = dc.compare(dc.parse(BASE), dc.parse(moved))
        self.assertEqual(dc.failures(rows), [])

    def test_changed_itcm_function_fails(self):
        changed = BASE.replace("push\t{r4, lr}", "push\t{r4, r5, lr}")
        bad = dc.failures(dc.compare(dc.parse(BASE), dc.parse(changed)))
        self.assertEqual([r[0] for r in bad], ["bench_chase"])

    def test_changed_unprotected_function_is_reported_not_failed(self):
        changed = BASE.replace("[pc, #4]", "[pc, #8]")
        rows = dc.compare(dc.parse(BASE), dc.parse(changed))
        self.assertIn(("main", "CHANGED", False), rows)
        self.assertEqual(dc.failures(rows), [])

    def test_removed_protected_function_fails(self):
        gone = BASE.split("08000300")[0]
        bad = dc.failures(dc.compare(dc.parse(BASE), dc.parse(gone)))
        self.assertEqual([(r[0], r[1]) for r in bad], [("dma_copy", "removed")])

    def test_same_code_moved_protected_function_fails(self):
        moved = BASE.replace("08000300 <dma_copy", "08000320 <dma_copy")
        bad = dc.failures(dc.compare(dc.parse(BASE), dc.parse(moved)))
        self.assertEqual([(r[0], r[1]) for r in bad], [("dma_copy", "MOVED")])

    def test_same_code_moved_unprotected_function_is_reported_not_failed(self):
        moved = BASE.replace("08000200 <main>", "08000220 <main>")
        rows = dc.compare(dc.parse(BASE), dc.parse(moved))
        self.assertIn(("main", "MOVED", False), rows)
        self.assertEqual(dc.failures(rows), [])

    def test_report_is_a_table(self):
        text = dc.report(dc.compare(dc.parse(BASE), dc.parse(BASE)))
        self.assertIn("| `bench_chase` | yes | identical |", text)


NM = """
08000400 T main
08000c00 r $d
08000c40 r k_regions
08000c80 r k_cfgs.1
20002000 b s_perm.0
20020000 B g_res
"""


class DataLayoutTest(unittest.TestCase):
    def test_parse_nm_keeps_data_and_drops_suffixes(self):
        syms = dc.parse_nm(NM)
        self.assertEqual(sorted(syms), ["g_res", "k_cfgs", "k_regions", "s_perm"])
        self.assertEqual(syms["s_perm"], [0x20002000])

    def test_renumbered_static_at_same_address_passes(self):
        renamed = NM.replace("s_perm.0", "s_perm.3")
        self.assertEqual(dc.compare_data(dc.parse_nm(NM), dc.parse_nm(renamed)), [])

    def test_moved_rodata_table_fails(self):
        moved = NM.replace("08000c40 r k_regions", "08000c60 r k_regions")
        self.assertEqual(dc.compare_data(dc.parse_nm(NM), dc.parse_nm(moved)), [("k_regions", "MOVED")])

    def test_added_and_removed_data_fail(self):
        changed = NM.replace("20020000 B g_res", "20020000 B g_other")
        self.assertEqual(dc.compare_data(dc.parse_nm(NM), dc.parse_nm(changed)),
                         [("g_other", "added"), ("g_res", "removed")])


MAP = """
Discarded input sections
 .data          0x00000000        0x0 build\\main.c.o

Linker script and memory map
 .text.memset   0x08000528       0x14 build\\libmin.c.o
 .rodata.k_cfgs.1
                0x08000bb8        0x5 build\\main.c.o
 *fill*         0x08000bbd        0x3
 .rodata.k_regions
                0x08000bc0       0x30 build\\main.c.o
 .bss.s_perm.0  0x20002000     0x2000 build\\bench_chain.c.o
 .sram2_buf     0x2007c000     0x4000 build\\main.c.o
"""


class MapLayoutTest(unittest.TestCase):
    def test_parse_map_takes_placed_non_code_sections(self):
        placed = dc.parse_map(MAP)
        self.assertEqual(placed, {
            ".rodata.k_cfgs": [0x08000bb8],
            ".rodata.k_regions": [0x08000bc0],
            ".bss.s_perm": [0x20002000],
            ".sram2_buf": [0x2007c000],
        })

    def test_definition_moved_to_another_file_at_same_address_passes(self):
        other = MAP.replace("build\bench_chain.c.o", "build\bench.c.o")
        self.assertEqual(dc.compare_map(dc.parse_map(MAP), dc.parse_map(other)), [])

    def test_symbol_less_table_moved_fails(self):
        moved = MAP.replace("0x08000bc0", "0x08000be0")
        self.assertEqual(dc.compare_map(dc.parse_map(MAP), dc.parse_map(moved)),
                         [(".rodata.k_regions", "MOVED")])

    def test_same_map_passes(self):
        self.assertEqual(dc.compare_map(dc.parse_map(MAP), dc.parse_map(MAP)), [])


if __name__ == "__main__":
    unittest.main()
