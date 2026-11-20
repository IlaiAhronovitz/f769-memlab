# MISRA C:2012 compliance: f769-memlab

Claimed per MISRA Compliance:2020. Rule numbers and categories only; the rule texts are MISRA's copyright.
Scope: all target C code, `src/*.c` and `src/*.h` (built with arm-none-eabi-gcc 14.2, `-std=c11`). `src/startup.s`
is assembly and outside the C guidelines. The host tests (`tests/*.c`) are built under the same strict warnings
and clang-tidy, but are not part of the compliance claim.

## Guideline Enforcement Plan
| Guidelines | Checked by |
|---|---|
| Rules that the cppcheck 2.16 misra addon implements (most of sections 2, 5, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22) | cppcheck `--addon=misra`, in `tools/quality.sh` (`--only misra`) |
| Directives 1.1, 2.1, 3.1, 4.1, 4.2, 4.3, 4.4, 4.5, 4.6, 4.7, 4.8, 4.9, 4.10, 4.11, 4.12, 4.13, 4.14 | review; Dir 2.1 also by the `-Werror` builds; Dir 4.1 also by ASan/UBSan on host tests |
| Language extensions and implementation-defined behaviour (Rule 1.1-1.3) | `-Wpedantic -Werror` with gcc and clang, plus review of the `__attribute__` and `__asm` uses |
| Everything the tools above miss | review against the rule list |

A per-rule table (every rule, its category, tool or review) belongs to the gate task. Copy it from the sibling
repository's `docs/MISRA-COMPLIANCE.md` when that exists, so the three repositories stay in step.

## Guideline Re-categorization Plan
Empty. No advisory rule is disapplied project-wide: the advisory rules that are broken (8.7, 8.9, 11.4, 11.5, 15.5,
17.8, 18.4) are broken through deviation records, each limited to named files.

## Deviation register
Records: [`docs/deviations/memlab.md`](deviations/memlab.md). Suppressions: `tools/quality/suppress-memlab.txt`
(each line under a comment naming its record).

| ID | Guidelines | Category | Scope |
|---|---|---|---|
| DEV-MEM-01 | Rule 11.4, Rule 11.6 | advisory, required | memory-mapped registers (`REG32`), cache maintenance by address |
| DEV-MEM-02 | Directive 4.3 | required | `dsb`/`isb`/`wfi` helpers; barriers inside the timed loops |
| DEV-MEM-03 | Rule 11.5, Rule 18.4 | advisory | buffers read as words and as pointer chains; pointer walk in the timed loops |
| DEV-MEM-04 | Rule 21.2, Rule 8.7 | required, advisory | `libmin`'s `memset`/`memcpy` under their standard names |
| DEV-MEM-05 | (clang-tidy only) | - | DMA destination parameter stays non-const |
| DEV-MEM-06 | Rule 15.5 | advisory | early return from a failed polling loop (clock bring-up, DMA error) |
| DEV-MEM-07 | Rule 17.8 | advisory | timed loops count their parameters down |
| DEV-MEM-08 | Rule 8.7 | advisory | `fault_report` is called from assembly |
| DEV-MEM-09 | Directive 4.1 (CERT ARR30-C) | required | `chase_build` precondition not checked (layout of the timed flash code) |
| DEV-MEM-10 | Rule 8.9 | advisory | bench buffers and `k_regions` stay at file scope in `src/main.c` (measured layout) |

## Compliance summary
From `bash tools/quality.sh` on 2026-11-20 (cppcheck 2.16.0, clang-tidy 21.1.8). The final run was in two steps
(`docs/CODING-STANDARD.md`): every check in WSL with `--allow-skip target-gcc`, and `target-gcc` (arm-none-eabi-gcc
14.2.Rel1) from Git Bash; both passed.

| Check | Findings before | Findings after |
|---|---|---|
| MISRA (cppcheck addon + cppcheck style) | 157 | 0 (with the suppressions above) |
| clang-tidy (CERT, bugprone, analyzer, readability) | 234 | 0 |

Mandatory rules: no violation found and no deviation. Required rules: compliant except DEV-MEM-01 (11.6),
DEV-MEM-02 (Dir 4.3), DEV-MEM-04 (21.2) and DEV-MEM-09 (Dir 4.1). Status: **compliant with deviations**.
