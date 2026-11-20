# tools/quality: the quality gate

`tools/quality.sh` runs every check of the coding standard (`docs/CODING-STANDARD.md`) over the repository and
prints one summary table. It exits non-zero on any finding, on a missing tool or a tool of the wrong version, and on
a check that could not run (SKIPPED) unless the run names that check with `--allow-skip`.

```sh
bash tools/quality.sh                          # WSL or Linux, every check
bash tools/quality.sh --only misra             # one check; --list prints the names
bash tools/quality.sh --allow-skip target-gcc  # local only: a SKIPPED target-gcc does not fail the run
```

The scratch directory (`.quality-XXXXXX/` in the repository root, so that a Windows gcc can write there) is deleted
at the end; `.gitignore` covers one that a killed run leaves behind.

## The checks

| Check | Tool (version asserted) | Files | What fails it |
|---|---|---|---|
| `target-gcc` | arm-none-eabi-gcc (14.2 recorded) | `src/*.c` | any warning with `build.py`'s target flags + the strict set (`WARN`, `GCC_ONLY`), `-Wstack-usage=1024` |
| `clang-target` | clang 21 | `src/*.c` | any warning, `--target=arm-none-eabi`, `-fsyntax-only` |
| `host-cc` | host gcc (`HOSTCC`, default `gcc`) and clang 21 | portable C (`-std=c99`) and `tests/*.c` (`-std=c11`) | any warning |
| `misra` | cppcheck 2.16.0 `--addon=misra` | `src/*.c` | any finding not covered by `suppress-memlab.txt`; a cppcheck failure |
| `tidy` | clang-tidy 21 | `src/*.c`, `tests/*.c` | any warning (all are errors); a clang-tidy failure |
| `sanitizers` | host gcc and clang 21, ASan + UBSan | `tests/*.c` (with the `/* module: src/x.c */` they name) | a build failure, a test failure, any sanitizer report |
| `coverage` | clang 21, llvm-profdata 21, llvm-cov 21 | portable C | a test or tool failure; the table is reported, not gated |
| `python` | python3 | `tools/*.py`, `tests/*.py` | any unittest failure or error |

A tool is "missing" when `command -v` does not find it; the check's row then reads `FAIL (tool missing: ...)`. The
versions are read from `--version`: cppcheck must be exactly 2.16.0, clang, clang-tidy, llvm-profdata and llvm-cov
must be 21.x. Overrides: `ARM_GCC`, `HOSTCC`, `CLANG`, `TIDY`, `PROFDATA`, `LLVMCOV`, `CPPCHECK`, `PYTHON`.

## Where the Arm gcc comes from

1. `ARM_GCC`, if set.
2. `arm-none-eabi-gcc` on `PATH` (CI, Linux).
3. The Windows toolchain at `build.py`'s `DEFAULT_GCC`:
   - in **Git Bash** (`cygpath` present, no `wslpath`) it runs natively;
   - in **WSL** it is started through `powershell.exe`, because a Windows gcc launched straight from WSL cannot find
     its own `cc1`. That needs WSL interop. When interop is off the `.exe` does not start and the check is SKIPPED.

## Skipped checks and `--allow-skip`

A SKIPPED check has proved nothing, so by default it fails the run. A **local** run may accept a skip by naming the
check: `--allow-skip <check>` (repeatable). The summary then shows the row as `SKIPPED (allowed by --allow-skip: ...)`,
prints `allowed to skip (--allow-skip): ...`, and ends `RESULT: PASS, except skipped (allowed): ...`. An unknown check
name exits 2. When the `CI` environment variable is set (GitHub Actions sets `CI=true`) `--allow-skip` is refused
with exit 2: CI must run every check, and the workflow never passes it.

The only expected skip is `target-gcc` in WSL without interop. Then run the gate in two steps, both from the
repository root:

```sh
bash tools/quality.sh --allow-skip target-gcc   # 1. in WSL: every other check
bash tools/quality.sh --only target-gcc         # 2. in Git Bash on Windows: the Arm gcc natively
```

Step 1 must end `RESULT: PASS, except skipped (allowed): target-gcc` and step 2 `RESULT: PASS`. Together they are
one passing gate.

## Suppressions and deviations

- `tools/quality/suppress-memlab.txt`: cppcheck suppression lines, one rule in one file
  (`misra-c2012-11.4:src/regs.h`), each under a comment naming its record in `docs/deviations/memlab.md`.
  `docs/MISRA-COMPLIANCE.md` indexes the records.
- clang-tidy: `NOLINT(<check>): <record ID>` on the line. The checks that are off, and why, are listed in
  `docs/CODING-STANDARD.md`.
- No blanket suppressions, no `*` file patterns, no `#pragma` to silence a tool.

## The layout check

`tools/disasm_check.py` is not part of the gate (it needs two builds). See `docs/CODING-STANDARD.md`: it compares the
protected functions' instructions and start addresses, the data symbols (`nm`) and the non-code input sections of
the linker map, between a base build and a new one.
