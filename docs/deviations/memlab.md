# Deviation records: f769-memlab target code

Format (MISRA Compliance:2020, section 5): ID, guideline and category, where it applies, why compliance is
not reasonable here, what keeps it safe, and how the tools are told (each suppression line in
`tools/quality/suppress-memlab.txt` and each tool setting names its record). One record per pattern, not per line.
Rule numbers only; the rule texts are MISRA's copyright.

Several records exist because of one constraint of this repository: the published cycle counts
(`results/run-20261120-1310.md`) are only reproducible while the timed code compiles to the same instructions.
`tools/disasm_check.py` checks this; where a fix would have changed the machine code of a timed function, the
code was left as published and is recorded here.

---

## DEV-MEM-01: memory-mapped registers and cache maintenance by address
- **Guidelines:** Rule 11.4 (advisory), Rule 11.6 (required). CERT/clang-tidy: `performance-no-int-to-ptr`,
  `clang-analyzer-core.FixedAddressDereference`.
- **Where:** `REG32()` in `src/regs.h`, used by every target file; the flash base addresses read by `run_flash()`
  (`src/main.c`); `dcache_clean_range()` / `dcache_invalidate_range()` in `src/cache.c`, which turn the buffer
  address into the integer that DCCMVAC / DCIMVAC take.
- **Why:** the Cortex-M7 system control block, MPU, DWT, RCC, PWR, flash interface and DMA2 are reached only
  through fixed addresses (ARMv7-M ARM, RM0410). Cache maintenance by address is defined on the integer value of the
  address.
- **Safety:** every address is a named constant in one header, taken from the cited manual section; each is
  32-bit aligned and `volatile`. The integer conversion uses `uintptr_t`, which is exactly 32 bits here.
- **Tools:** `misra-c2012-11.4:<file>` for each target file and `misra-c2012-11.6:src/cache.c` in the
  suppression list; the two clang-tidy checks are off in `.clang-tidy` and `tools/quality.sh` for this record only.

## DEV-MEM-02: inline assembly
- **Guideline:** Directive 4.3 (required).
- **Where:** `dsb()`, `isb()` and `wfi()` in `src/regs.h`; inside the three timed functions in `src/bench.c`, the
  compiler barrier `BARRIER()` and a `dsb` before the second cycle-counter read.
- **Why:** the barriers have no C equivalent, and the benchmark's correctness depends on them (no load of the loop
  may leave the timing window). The timed functions keep their own copies because changing them would change the
  published machine code.
- **Safety:** each instruction sits in a one-line function or macro with a `memory` clobber; nothing else in the
  project uses assembly outside `src/startup.s`.
- **Tools:** Directive 4.3 is checked by review (cppcheck does not check it); no suppression needed.

## DEV-MEM-03: the benchmark's deliberate memory access patterns
- **Guidelines:** Rule 11.5 (advisory), Rule 18.4 (advisory). clang-tidy: none left.
- **Where:** `chase_build()` (`src/bench_chain.c`) writes a pointer chain into a byte buffer; `run_seq()` (`src/main.c`)
  reads the same buffer as 32-bit words; `p += 8` in `bench_seq_read()` / `bench_seq_write()` (`src/bench.c`, timed);
  `memset` / `memcpy` (`src/libmin.c`) take `void *` by their standard signatures.
- **Why:** measuring the memory system needs the same bytes seen as words and as pointers, and a pointer walk in
  the timed loops. Rewriting the walk as indexing could change the timed machine code.
- **Safety:** every buffer is aligned to 32 bytes (`aligned(32)` and the linker script), so the word and pointer
  views are aligned; the timed loops stop at `words / 8` blocks, inside the buffer; `tests/test_chain.c` checks
  under ASan + UBSan that the chain stays in its buffer and is one cycle.
- **Tools:** `misra-c2012-11.5:src/bench_chain.c`, `src/main.c`, `src/libmin.c`; `misra-c2012-18.4:src/bench.c`.

## DEV-MEM-04: libmin uses reserved names
- **Guidelines:** Rule 21.2 (required), Rule 8.7 (advisory). clang-analyzer: `security.insecureAPI.DeprecatedOrUnsafeBufferHandling` on the one `memset` call in `main()`.
- **Where:** `src/libmin.h`, `src/libmin.c`; the call in `main()`.
- **Why:** the image is linked `-nostdlib`. GCC may emit calls to `memset` and `memcpy` on its own (struct
  zeroing and copies), so these exact external names must exist. Annex K's `memset_s` does not exist in a
  freestanding build, and the call clears a fixed-size object with `sizeof` of that object.
- **Safety:** the two functions have exactly the standard signatures and semantics (byte loops), so a
  compiler-generated call behaves as the standard says. `memcpy` has no caller in C, which is what Rule 8.7 sees.
- **Tools:** `misra-c2012-21.2:src/libmin.h`, `src/libmin.c`; `misra-c2012-8.7:src/libmin.c`; one
  `NOLINT(clang-analyzer-security.insecureAPI.DeprecatedOrUnsafeBufferHandling)` on the call in `main()`.

## DEV-MEM-05: a DMA destination stays non-const
- **Guideline:** clang-tidy `readability-non-const-parameter` (no MISRA rule involved).
- **Where:** `dma_copy(..., uint32_t *dst, ...)` in `src/dmatest.c` (timed).
- **Why:** the CPU never writes through `dst`, the DMA controller does; a `const` would tell the reader the
  opposite.
- **Tools:** one `NOLINT(readability-non-const-parameter)` on the definition.

## DEV-MEM-06: early return from a failed polling loop
- **Guideline:** Rule 15.5 (advisory).
- **Where:** `wait_set()` and `clock_216_init()` in `src/system.c`; `dma_copy()` in `src/dmatest.c` (timed).
- **Why:** each early return reports which step failed (a clock step number, or 0 cycles for a DMA error). A single
  exit was tried for `wait_set()` and changed the machine code of `clock_216_init()`, which every published number
  depends on; `dma_copy()` is timed. Both stay as published.
- **Safety:** every exit returns a defined value; there is nothing to release.
- **Tools:** `misra-c2012-15.5:src/system.c`, `misra-c2012-15.5:src/dmatest.c`.

## DEV-MEM-07: the timed loops count their parameters down
- **Guideline:** Rule 17.8 (advisory).
- **Where:** `while (reps-- != 0U)` in `bench_seq_read()` and `bench_seq_write()` (`src/bench.c`, timed).
- **Why:** a local copy of the count is the fix, and these are the measurement itself; they stay as published.
- **Safety:** the parameters are passed by value and not read after the loop.
- **Tools:** `misra-c2012-17.8:src/bench.c`.

## DEV-MEM-08: a function called only from assembly
- **Guideline:** Rule 8.7 (advisory).
- **Where:** `fault_report()` in `src/results.c`, declared in `src/results.h`, called by `Fault_Handler` in
  `src/startup.s`.
- **Why:** the caller is assembly, which the checker does not see, so it reports that the function could be
  `static`. It must keep external linkage.
- **Tools:** `misra-c2012-8.7:src/results.c`.

## DEV-MEM-09: chase_build does not check its size (a defect left in place)
- **Guideline:** CERT ARR30-C (no out-of-bounds index); Directive 4.1 (required: run-time failures minimised).
  Found in review; clang-tidy's `clang-analyzer-security.ArrayBound` reports the same path.
- **Where:** `chase_build()` in `src/bench_chain.c`.
- **Finding:** for `bytes` under 32, `n - 1` wraps and the shuffle indexes `s_perm` far out of bounds; over 128 KiB
  it writes past `s_perm`. A NULL `buf` is dereferenced.
- **Why it stays:** the guard was written and tested, then removed. `chase_build` links first in `.text`, right
  before `cache.c` and `dmatest.c`, whose functions run from flash inside the DMA test's timing windows. The guard
  made `chase_build` 12 bytes longer, which moved those functions and changed their alignment to the 32-byte
  I-cache lines, and with it, possibly, the published clean / invalidate / DMA cycle counts.
- **Safety:** the only caller is `run_chase()` in `src/main.c`, which passes 1 KiB .. the region size
  (at most 128 KiB, the size of `s_sram1`) and the static, 32-byte-aligned bench buffers; the precondition is
  written in `src/bench_chain.h`. `tests/test_chain.c` covers every size the firmware uses.
- **Tools:** one `NOLINT(clang-analyzer-security.ArrayBound)` on the first `s_perm[i]` read of the shuffle.
- **To close it:** add the guard at the next measurement run, when the numbers are re-published anyway.

## DEV-MEM-10: the bench buffers and `k_regions` stay at file scope
- **Guideline:** Rule 8.9 (advisory: an object used in one function should be defined in that function).
- **Where:** `s_dtcm`, `s_sram1`, `s_sram2`, `s_itcm` and `k_regions` in `src/main.c`.
- **Finding:** all five are used only by `main()`.
- **Why it stays:** moving them (and `k_cfgs`) into `main()` changed **10 published figures** with byte-identical
  timed code, the largest SRAM2 at 16 KiB under write-through: **7.01** cycles per load instead of the published
  **3.97**. The others: SRAM2 16 KiB write-back 2.85 -> 2.67; SRAM1 chase 16 KiB 2.00/2.11 -> 2.10/2.10 (WB/WT),
  32 KiB WT 7.05 -> 7.08, 64 KiB WB 8.22 -> 8.13; bandwidth SRAM1 8 KiB WB 2.45 -> 2.43 and WT 2.44 -> 2.45, SRAM2
  16 KiB WT 2.44 -> 2.43; flash AXIM WT 1.20 -> 1.25. The pointer chase reads `k_regions` from flash through the
  D-cache, and near a cache-size working set the result depends on which cache set that flash line falls in. The
  3.97 / 7.01 difference reproduced over two runs each; the committed layout reproduced 3.97 and 2.85 on the same
  board the same day. With the definitions back at file scope, in their original order, the rendered tables of
  `results/run-20261120-1551.md` (from the 212 raw result rows) match `results/run-20261120-1310.md` exactly; in the
  raw block only the flash-read checksums differ (the image changed: `memset`, see the commit) and one chase row by
  3 cycles of 32781 (board run, 2026-11-20).
- **What it teaches:** a cache-size working set is a cliff. Its figure is a property of the whole image's layout, not
  of the memory alone. The README flags the rows near the cache size as layout-sensitive.
- **Tools:** `misra-c2012-8.9:src/main.c` in `tools/quality/suppress-memlab.txt`.
