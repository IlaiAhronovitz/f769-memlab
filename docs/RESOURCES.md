# Resources: f769-memlab

Measured from a scratch build on 2026-11-20 (a copy of the tree under `%TEMP%`, Arm GNU Toolchain 14.2 rel1):

```
python build.py        # build.py's own flags, plus -fstack-usage added for the stack table
arm-none-eabi-size -A build/memlab.elf
```

Flags (from `build.py`): `-mcpu=cortex-m7 -mthumb -mfloat-abi=soft -O2 -g3 -std=c11 -Wall -Wextra -Werror ...`, linked
with `-nostartfiles -nostdlib -Wl,--gc-sections`. The image layout is frozen (see `CLAUDE.md`, DEV-MEM-10): these
numbers are the published layout.

## Flash and RAM per memory region

Linker script: `linker/stm32f769ni.ld`. Sizes in bytes. Most RAM is deliberately the benchmark working sets.

| Region | Address | Size | Used | Content |
|---|---|---|---|---|
| FLASH (AXIM) | `0x08000000` | 2 MiB | 3384 (0.2 %) | `.vectors` 64, `.text` + `.rodata` 2992, `.itcm_text` load image 328 |
| ITCM (code) | `0x00000100` | 7936 | 328 (4 %) | `.itcm_text`: the timed loops (`bench_chase`, `bench_seq_read`, `bench_seq_write`) |
| ITCMBUF | `0x00002000` | 8 KiB | 8192 (100 %) | `.itcm_buf`: ITCM working set |
| RESULTS (DTCM) | `0x20000000` | 8 KiB | 8128 (99 %) | `.results`: the block `build.py read` dumps |
| DTCM (data, bss) | `0x20002000` | 24 KiB | 8192 | `.data` 0 + `.bss` 8192 |
| DTCMBUF | `0x20008000` | 64 KiB | 65536 (100 %) | `.dtcm_buf`: DTCM working set |
| STACK (DTCM) | `0x2001C000` | 16 KiB | not measured | stack grows down from `0x20020000` |
| SRAM1B | `0x20040000` | 128 KiB | 131072 (100 %) | `.sram1_buf`: SRAM1 working set |
| DMABUF | `0x20060000` | 64 KiB | 16384 (25 %) | `.dma_buf`: DMA test buffers A and B, 8 KiB each, 32-byte aligned |
| SRAM2B | `0x2007C000` | 16 KiB | 16384 (100 %) | `.sram2_buf`: SRAM2 working set |

Total image: text 3384, data 0, bss 253888 (includes every NOLOAD buffer above).

## Largest stack frames

From `-fstack-usage` (all static). The gate's limit is 1 KiB per function.

| Frame (bytes) | Function | File |
|---|---|---|
| 96 | `main` | `src/main.c:176` |
| 48 | `run_flash` | `src/main.c:135` |
| 48 | `dmatest_run` | `src/dmatest.c:100` |
| 48 | `bench_seq_read` | `src/bench.c:38` |
| 16 | `chase_build` | `src/bench_chain.c:35` |
| 8 | `res_add` | `src/results.c:7` |

The worst call chain depth is not measured.

## DMA areas and MPU regions

- **DMA2, stream 0, channel 0**, memory to memory, 32-bit, FIFO mode: copies buffer A to buffer B, 8 KiB each, in
  `.dma_buf` at `0x20060000` (`dmatest.c`).
- **MPU regions in use** (each is one power-of-two region aligned to its own size; `PRIVDEFENA` on):

| Region | Base | Size | Attribute | When |
|---|---|---|---|---|
| 0 | `0x20040000` (SRAM1 bench) | 128 KiB (2^17) | write-through, or non-cacheable | the WT and NC cache configurations (`main.c`) |
| 1 | `0x2007C000` (SRAM2 bench) | 16 KiB (2^14) | write-through, or non-cacheable | same configurations |
| 0 | `0x20060000` (`.dma_buf`) | 64 KiB (2^16) | non-cacheable | the DMA test's "MPU fix" case (`dmatest.c`) |

  The MPU is reconfigured between phases; the regions are not all live at once.
- The I-cache and D-cache are switched per configuration (off, I only, I+D write-back, write-through, non-cacheable).
  CCSIDR is read at run time and reported; the firmware does not assume the data-sheet geometry.

## Clocks

| Clock | Value | Source |
|---|---|---|
| SYSCLK = HCLK | 216 MHz | HSI 16 MHz, /M=8, x N=216 (VCO 432 MHz), /P=2; 7 flash wait states, prefetch and ART on |
| APB1 | 54 MHz | HCLK /4 |
| APB2 | 108 MHz | HCLK /2 |
| Time base | DWT cycle counter at 216 MHz | all published numbers are cycles |

The first run read back PLLCFGR 0x29003608, CFGR 0x0000940A, FLASH_ACR 0x307 (README).

## Peripherals used

- RCC, PWR, FLASH (wait states, ART accelerator and its reset), DMA2 (stream 0 only).
- Cortex-M7: I-cache, D-cache, MPU, DWT (cycle counter), the CCSIDR cache geometry register.
- Memories under test: ITCM, DTCM, SRAM1, SRAM2, flash via AXIM and via ITCM + ART.
- Not used: Ethernet, UART, USB, FMC/SDRAM (planned), DSI, camera, GPIO pins.

## Power

Power: not measured yet.
