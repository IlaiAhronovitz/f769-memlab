# Coding standard: f769-memlab

This repository follows the same quality standard as its sibling projects (f769-net, m24sr-nfc). "Done" means
every check below runs clean, and anything left over is a documented deviation with a reason.

## What is applied
| Area | Standard | Checked by |
|---|---|---|
| C language subset | MISRA C:2012 (Amendments 1-4), claimed per MISRA Compliance:2020 | cppcheck 2.16.0 `--addon=misra`, plus review |
| Secure coding | SEI CERT C | clang-tidy 21: `cert-*`, `bugprone-*`, `clang-analyzer-*` |
| Warnings | gcc and clang strict set, `-Werror` | arm-none-eabi-gcc 14.2 (target), gcc 11 and clang 21 (host) |
| Runtime defects | no ASan/UBSan finding in any host test | gcc 11 and clang 21 sanitizers |
| Tests | host tests for the portable code, with line and branch coverage | clang source-based coverage; Python `unittest` |
| Clean code | cognitive complexity <= 25, <= 80 lines per function | clang-tidy readability checks |

The gate checks the tool versions: a different cppcheck (2.7 finds 27 MISRA "violations" in the same tree) or a
clang/LLVM other than 21 fails the check instead of producing a different verdict.

## The rule that comes first: the measured layout does not change
The README publishes measured cycle counts (`results/`). They can only be reproduced while the measured image keeps
the same instructions **at the same addresses**, and the same data at the same addresses:
- the three timed loops in `src/bench.c` (placed in ITCM, `.itcm_text`);
- the cache and MPU sequences in `src/cache.c`, and `src/dmatest.c`, which run from flash inside the DMA test's
  timing windows: their address and their alignment to the 32-byte I-cache lines matter too, so everything linked
  before them (`src/bench_chain.c`) must keep its size;
- the data the measurement reads: the flash tables (`k_regions`, `k_cfgs` in `src/main.c`), the working-set buffers,
  `s_perm` and the results block. DEV-MEM-10 moved two flash tables with byte-identical code, and 10 published
  figures changed;
- the link order: `build.py` links `sorted(glob("src/*.c"))`, so a new `src/` file whose name sorts before
  `dmatest.c` moves the timed flash code.

After any change to `src/`, build the old and the new tree (each in its own copy) and compare:
```
python build.py build                                  # in a copy of the old tree: keep build/memlab.elf + .map
python build.py build                                  # in the new tree
python tools/disasm_check.py old/build/memlab.elf build/memlab.elf
```
Exit 0 means: every protected function has the same instructions at the same start address, no data symbol moved,
appeared or vanished, and no loaded non-code input section in the linker map (`memlab.map`, next to each `.elf`)
moved. That is necessary, not sufficient: any change that moves code or data also needs a board run whose tables
match `results/run-20261120-1310.md` exactly. If a fix would change protected code or the layout, do not make it:
write a deviation record instead.

## How to run the checks
In WSL or Linux, from the repository root:
```
bash tools/quality.sh                 # every check, one summary table, non-zero exit on any finding
bash tools/quality.sh --only misra    # one check: target-gcc clang-target host-cc misra tidy sanitizers coverage python
python -m unittest discover -s tests  # the Python and host-C tests only (also: python build.py test)
```
A check that cannot run (`SKIPPED`), a missing tool and a tool of the wrong version all fail the gate.
`tools/quality/README.md` describes every check.

**On Windows**, the gate runs in WSL. `target-gcc` uses the Windows Arm toolchain named in `build.py`, started through
WSL interop; when interop is off it cannot start, and the check is SKIPPED. A local run may then accept that one skip
by naming it, and run the check natively from Git Bash:
```
bash tools/quality.sh --allow-skip target-gcc   # in WSL: must end "RESULT: PASS, except skipped (allowed): target-gcc"
bash tools/quality.sh --only target-gcc         # in Git Bash: must end "RESULT: PASS"
```
Both steps together are one passing gate. `--allow-skip` is printed in the summary, is refused when the `CI`
variable is set, and CI never passes it.

**CI** (`.github/workflows/ci.yml`) builds the firmware and runs `python3 build.py test`. It does not run
`tools/quality.sh` or `tools/disasm_check.py` yet: until that job exists, the gate is run locally before every
commit, and the compliance summary in `docs/MISRA-COMPLIANCE.md` is the record of that local run.

## clang-tidy configuration
`.clang-tidy` and `tools/quality.sh` enable `bugprone-*`, `cert-*`, `clang-analyzer-*`, `misc-*`, `performance-*`,
`portability-*` and `readability-*`, with every warning an error. Five checks are off for all files; they are the
shared base set of the sibling repositories:

| Check off | Why |
|---|---|
| `readability-magic-numbers` | register-level code writes bit patterns, offsets and sizes taken from the reference manual |
| `readability-identifier-length` | loop indices (`i`, `n`, `k`) are clearer short |
| `misc-include-cleaner` | it needs an include map for the freestanding headers and flags `<stdint.h>` types as unused includes |
| `bugprone-easily-swappable-parameters` | `(src, dst, words)`-style signatures; every call is internal and covered by the tests or the board run |
| `readability-avoid-unconditional-preprocessor-if` | not used in this code; off to keep the base set the same in all three repositories |

Two more are off **only for the register-level target files** (every `src/*.c` except the portable
`src/bench_chain.c` and `src/libmin.c`), under deviation DEV-MEM-01: `performance-no-int-to-ptr` and
`clang-analyzer-core.FixedAddressDereference`. The gate runs the portable code and the host tests with them on.
`.clang-tidy` (used by editors) lists them as off for every file, with a comment naming DEV-MEM-01.

## How to add a deviation
1. Fix the finding if you can do it without changing protected machine code or the layout.
2. Otherwise add a record to `docs/deviations/memlab.md` (ID, guideline, where, why, what keeps it safe, how the
   tools are told), and list it in the register in `docs/MISRA-COMPLIANCE.md`.
3. Suppress only what the record names: a `misra-c2012-<rule>:<file>` line in `tools/quality/suppress-memlab.txt`
   with the record ID in a comment above it, or a `NOLINT(<check>): <record ID>` comment. No blanket suppressions,
   no `#pragma`s, no disabled checks without a reason in this document.

Refer to MISRA rules by number only: the rule texts are copyrighted.
