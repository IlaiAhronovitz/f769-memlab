# f769-memlab

Measuring the Cortex-M7 memory system of an STM32F769 on real hardware: what the
L1 caches, the TCMs, write-through versus write-back and the MPU actually cost
in cycles, and what goes wrong when DMA and a write-back D-cache share memory.

Bare-metal C, register level, no HAL. The timed loops run from ITCM, so the
instruction side is the same in every configuration and only the data side is
measured.

## Board

STM32F769I-EVAL (MB1219 Rev.A, an "ES" engineering-sample board) with the
MB1166 DSI display, a camera module and Ethernet. The MCU is an STM32F769NI:
Cortex-M7, 2 MiB flash, 512 KiB RAM.

| Memory | Address | Size | Cached? |
|---|---|---|---|
| ITCM RAM | 0x0000_0000 | 16 KiB | no (TCM) |
| Flash via ITCM + ART | 0x0020_0000 | 2 MiB | ART accelerator |
| Flash via AXIM | 0x0800_0000 | 2 MiB | L1 caches |
| DTCM RAM | 0x2000_0000 | 128 KiB | no (TCM) |
| SRAM1 | 0x2002_0000 | 368 KiB | L1 D-cache |
| SRAM2 | 0x2007_C000 | 16 KiB | L1 D-cache |

The firmware reads the cache geometry from CCSIDR and reports it, rather than
assuming the data-sheet figures (16 KiB I + 16 KiB D on the F76x line).

The on-board debugger of this board enumerates as an ST-LINK/V2 (USB
`0483:3748`), which has no virtual COM port. So the target never prints:
every result goes into a block at 0x2000_0000 (DTCM, never cached), and the
host reads it over SWD.

## What it measures

For each cache configuration (caches off, I-cache on, I+D write-back, I+D with
the SRAM buffers write-through, I+D with the SRAM buffers non-cacheable) and
each memory (DTCM, SRAM1, SRAM2, ITCM):

1. **Pointer chase**, 1 KiB up to the size of the region. One random cycle
   through every 32-byte line (Sattolo's algorithm), so each load depends on
   the one before and lands on a new line. The result is cycles per dependent
   load, i.e. latency. The step where the working set outgrows the D-cache
   should show up in SRAM1.
2. **Sequential read and write**, in bytes per cycle: 8 KiB (fits in the
   cache) and the full region.
3. **Flash read** through AXIM and through ITCM + ART, with ART on and off.

Then the **DMA coherence test**: DMA2 copies 8 KiB inside SRAM1 while the
D-cache is on.
- **No maintenance:** counts the stale words.
- **Clean the source, invalidate the destination:** expected 0 stale words,
  and the cost of the two operations in cycles.
- **MPU makes the buffers non-cacheable:** expected 0 stale words, and the
  cost in CPU write speed.

Timing uses the DWT cycle counter, with a DSB before the closing read so no
load is still in flight. SYSCLK is 216 MHz from the PLL fed by HSI.

## Results (first run, 2026-11-20)

Full tables are in [`results/run-20261120-1310.md`](results/run-20261120-1310.md), with the
raw block in the `.bin` next to it. A later run of the current code
([`results/run-20261120-1551.md`](results/run-20261120-1551.md)) reproduced every table figure.

**Layout-sensitive rows.** Figures for working sets near the 16 KiB D-cache size (16-64 KiB) depend on the
whole image's layout, not only on the memory under test. Moving two small flash tables, with byte-identical timed
code, changed 10 figures, the largest from 3.97 to 7.01 cycles per load (SRAM2, 16 KiB, write-through); see
deviation DEV-MEM-10 in [`docs/deviations/memlab.md`](docs/deviations/memlab.md). Compare these rows only between
builds with the same layout.

The device reported itself as DEV_ID 0x451 and REV_ID 0x1000, core r1p0. The register
read-back confirms SYSCLK = 216 MHz (PLLCFGR 0x29003608, CFGR 0x0000940A, FLASH_ACR 0x307).
Cache geometry, read from CCSIDR: D-cache 16 KiB, 4-way; I-cache 16 KiB, 2-way; 32-byte lines.

**Latency: pointer chase, in cycles per dependent load**

| Working set | DTCM | SRAM1, caches off | SRAM1, I+D write-back | SRAM1, non-cacheable (MPU) |
|---|---|---|---|---|
| 1-16 KiB | 2.00 | 8.00 | 2.00 | 9.00 |
| 32 KiB | 2.00 | 8.00 | 7.18 | 9.00 |
| 64 KiB | 2.00 | 8.00 | 8.22 | 9.00 |
| 128 KiB | - | 8.00 | 8.56 | 9.00 |

- The cliff sits exactly where the working set outgrows the 16 KiB D-cache.
- Past the cliff, a miss costs a little more than an uncached load (8.6 against 8.0), because a whole 32-byte line is filled.
- A non-cacheable access with the cache on costs one cycle more than one with the cache off.
- SRAM2 behaves the same, one cycle slower (9 uncached).
- The TCMs are 2 cycles in every configuration.

**Bandwidth, in bytes per cycle (32-bit loads):**
- SRAM1 reads: 0.57 uncached; 2.45 cached at 8 KiB; 1.64 streaming 128 KiB through the cache.
- Writes: 3.5 everywhere, because the store buffer hides the memory.
- Flash: 0.51 through AXIM uncached, 1.21 cached, 1.88 through ITCM + ART, and 1.33 with ART off.

**DMA2 versus the D-cache (8 KiB copy inside SRAM1, write-back cache):**

| Case | Stale words | Cost |
|---|---|---|
| No maintenance | **2048 of 2048** | - |
| Clean source + invalidate destination | 0 | 3626 + 2325 cycles (about 14 and 9 cycles per line; 27.6 µs at 216 MHz) |
| Buffers non-cacheable through the MPU | 0 | CPU fill unchanged (5141 vs 5147 cycles; writes are buffered) |

The DMA copy itself takes 9764 cycles, about 0.84 bytes per cycle.

### Board note
This engineering-sample chip reads its system memory (0x0010_0000) as erased. With BOOT0 = 1
it boots from there and locks up, which is probably why the original demo stayed dark. Set
**BOOT0 = 0** to boot from flash. The original 2 MiB image was saved locally (SHA-256 in `build.py`; ST's image,
so it is not in this repo), and `python build.py restore` puts it back.

## Build and run

The tools are the Arm GNU Toolchain 14.2 (`arm-none-eabi-gcc`), xPack OpenOCD
0.12 and Python 3.

```
python build.py            # build/memlab.elf
python build.py test       # host tests: results parser, layout checker, pointer-chase builder (needs a host C compiler)
python build.py run        # flash, wait for the suite, print and save results
python build.py read       # read the results block again
bash tools/quality.sh      # the quality gate (WSL or Linux): MISRA C:2012, CERT C, strict warnings, sanitizers
```

If more than one ST-LINK/V2 is plugged in, pass `--serial <SN>`.

The code follows [`docs/CODING-STANDARD.md`](docs/CODING-STANDARD.md) (MISRA C:2012 with the deviations listed in
[`docs/MISRA-COMPLIANCE.md`](docs/MISRA-COMPLIANCE.md)); `tools/quality/README.md` describes the gate. Because the
published numbers depend on the image layout, a change to `src/` must also pass `tools/disasm_check.py` against the
measured build.

## Roadmap

1. Repeat runs to report run-to-run spread, and a write-through versus write-back write test.
2. The board's external SDRAM through the FMC: latency and bandwidth
   behind the cache, and MPU attributes for it.
3. Ethernet MAC with DMA descriptors in DTCM versus cacheable SRAM: the same
   coherence problem on a real peripheral, and raw L2 frame throughput.
4. CI: build and host tests on every push (`.github/workflows/ci.yml`).

## Limits

- The board carries engineering-sample silicon. The chip's DEV_ID, REV_ID and
  core revision are reported with every run, so results can be checked against
  the matching errata sheet.
- Soft-float build, so the FPU stays off. It plays no part in these
  measurements.
