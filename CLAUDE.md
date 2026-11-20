# f769-memlab: agent rules

Measures the Cortex-M7 memory system of an STM32F769 on real hardware: L1 caches, TCMs, write-through versus
write-back, the MPU, flash through ART, and DMA2 against a write-back D-cache (stale words, clean/invalidate cost).
Bare-metal C, register level, no HAL. The timed loops run from ITCM so only the data side varies. Public repo;
`README.md` has the results and `docs/RESOURCES.md` the memory budget.

If a `CLAUDE.local.md` exists next to this file, read it too: it holds the notes for one machine and is git-ignored.

## Board
- STM32F769I-EVAL (MB1219 Rev A, "ES" engineering-sample silicon; DEV_ID 0x451, REV_ID 0x1000), 216 MHz from HSI + PLL.
- On-board or external ST-LINK/V2 (USB 0483:3748, no virtual COM port). ST-LINK/V2 probes share that USB ID: **pass
  `--serial <SN>`** whenever more than one is plugged in. The target never prints: results go to a block at
  `0x20000000` (DTCM, never cached) and the host reads it over SWD.
- **BOOT0 = 0.** This chip reads system memory (0x00100000) as erased: with BOOT0 = 1 it boots there and locks up.
- The factory image is saved into `backup/` before the first flash; `build.py` records its SHA-256 and `restore`
  checks it.

## Commands (run in the repo root)
```
python build.py                       # build/memlab.elf (+ .bin, .map)
python build.py test                  # host tests: results parser, layout checker, pointer-chase builder
python build.py run --serial <SN>     # flash, wait for the suite, print and save results/run-YYYYmmdd-HHMM.md
python build.py read --serial <SN>    # dump the results block and print it again
python build.py restore --serial <SN> # factory demo back
bash tools/quality.sh                 # the quality gate, in WSL or Linux (no build.py wrapper here)
python tools/disasm_check.py BASE.elf NEW.elf   # the layout check (needs the .map next to each .elf)
```
Tools: Arm GNU Toolchain 14.2 (`GCC_BIN`), xPack OpenOCD 0.12 (`OPENOCD`), a host C compiler (`HOSTCC`, default
`gcc`; read by `tools/quality.sh` and `tests/test_chain_host.py`), Python 3.
Never flash or reset a board you were not given.

## Quality gate (mandatory before any commit)
`bash tools/quality.sh` must print `RESULT: PASS`: MISRA C:2012 (cppcheck 2.16.0), CERT C and clang-tidy 21, strict
gcc and clang warnings as errors, ASan and UBSan on `tests/test_chain.c`, coverage, stack frames under 1 KiB. A
missing tool, a wrong tool version or a SKIPPED check fails the run. Only a local run may accept a skip, by naming
it (`--allow-skip target-gcc`) and then running that check another way (`bash tools/quality.sh --only target-gcc`
from Git Bash, see `tools/quality/README.md`); CI never does. No blanket suppressions, no `#pragma` to silence a
tool: a finding is fixed or covered by a record in `docs/deviations/memlab.md` (DEV-MEM-nn) with a matching line in
`tools/quality/suppress-memlab.txt`. Read `docs/CODING-STANDARD.md` first.

## The measured layout is frozen
Published cycle counts depend on the exact image layout, not only on the memory under test. DEV-MEM-10: moving the
bench tables into `main()` changed 10 published figures with byte-identical timed code, the largest SRAM2, 16 KiB,
write-through: 7.01 instead of 3.97 cycles per load. Near a cache-size working set the figure depends on which cache
set a flash line falls in. Therefore:
- Do not change anything that moves code, rodata, data, the order of definitions, or alignment: no reordering, no
  new file that links ahead of the timed code (`build.py` links `src/*.c` sorted by name), no size-guard shuffling,
  no compiler-flag change.
- Check every `src/` change with `python tools/disasm_check.py BASE.elf NEW.elf`: protected functions (everything in
  `.itcm_text`, `cache.c`, `dmatest.c`) must have the same instructions at the same start addresses, and no data
  symbol or non-code input section (linker map) may move. Passing it is necessary, not sufficient.
- **A change that moves code or data needs a board comparison**: `python build.py run`, then the rendered tables
  (all 212 raw rows) against `results/run-20261120-1310.md`, every figure equal. Without a board run, do not make
  the change.
- The rows near the cache size (16-64 KiB) are layout-sensitive: say so when quoting them.

## Owner's rule: research first, no architecture change without an approved ADR
**No change to code or memory architecture** (cache/DMA layout, the working-set sections and MPU regions, the results
block format, linker layout) **without a sourced ADR, marked PROPOSED and approved by the owner.** Allowed without an
ADR: bug fixes, standard-compliance fixes that pass the frozen-layout rule above, measurement tooling, tests, docs.

## Board safety
- **Never touch option bytes or RDP.**
- Restore images live in `backup/` and are **never committed** (ST's copyright). Do not delete them.
- Caches and MPU are left in a known state by the firmware; do not reset the board mid-suite and trust the numbers.

## Public-repo hygiene
No personal data (names, e-mail, user-name paths, serial numbers of personal gear, details of one machine).
`research/`, `backup/`, `build/` and `CLAUDE.local.md` are git-ignored: **never add them, or anything from them, to a
commit**, and do not link to them from tracked files. Stage files by name, not with `git add -A`. Published numbers
come from a board run, with the chip's IDs; never from memory or a datasheet summary.
