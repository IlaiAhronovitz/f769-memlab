#!/usr/bin/env bash
# tools/quality.sh - the quality gate of f769-memlab (docs/CODING-STANDARD.md).
#
# Runs every check of the standard over the repo, prints one summary table and
# exits non-zero on any finding, on a missing or wrong-version tool, and on a
# check that could not run (SKIPPED), unless that check is named with --allow-skip.
#
#   bash tools/quality.sh                          every check
#   bash tools/quality.sh --only misra             one check (--list prints the names)
#   bash tools/quality.sh --allow-skip target-gcc  a SKIPPED target-gcc does not fail the run.
#        Local runs only (refused when CI is set): WSL without interop cannot start the
#        Windows Arm gcc; then run `bash tools/quality.sh --only target-gcc` in Git Bash too.
#
# Runs in WSL or Linux (every check), in Git Bash (target-gcc only) and on an Ubuntu CI runner.
# Tool overrides (environment): ARM_GCC, HOSTCC, CLANG, TIDY, PROFDATA, LLVMCOV, CPPCHECK, PYTHON.
# Source lists come from globs, so new files are picked up:
#   target code  src/*.c                (all of it touches registers, except the portable files)
#   portable C   src/bench_chain.c src/libmin.c (also built on the host)
#   host C tests tests/*.c
#   Python       tools/*.py tests/*.py  (run with python3 -m unittest)
set -u
cd "$(dirname "$0")/.."

CHECKS="target-gcc clang-target host-cc misra tidy sanitizers coverage python"
ONLY=""; ALLOW_SKIP=""
is_check() { [ -n "$1" ] && case " $CHECKS " in *" $1 "*) true ;; *) false ;; esac; }
while [ $# -gt 0 ]; do
    case "$1" in
        --only) ONLY="${2:-}"; shift 2 ;;
        --allow-skip)
            is_check "${2:-}" || { echo "--allow-skip: unknown check '${2:-}'; checks: $CHECKS" >&2; exit 2; }
            ALLOW_SKIP="$ALLOW_SKIP $2"; shift 2 ;;
        --list) printf '%s\n' $CHECKS; exit 0 ;;
        -h|--help) sed -n '2,20p' "$0"; exit 0 ;;
        *) echo "unknown argument: $1 (try --help)" >&2; exit 2 ;;
    esac
done
if [ -n "$ONLY" ] && ! is_check "$ONLY"; then echo "unknown check '$ONLY'; checks: $CHECKS" >&2; exit 2; fi
# CI must run every check: a skipped check there is a broken runner, never an accepted result.
if [ -n "$ALLOW_SKIP" ] && [ -n "${CI:-}" ]; then
    echo "--allow-skip is refused when CI is set (CI=$CI): every check must run in CI" >&2; exit 2
fi

WARN="-Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wshadow -Wcast-qual -Wstrict-prototypes \
 -Wmissing-prototypes -Wmissing-declarations -Wundef -Wdouble-promotion -Wformat=2 -Wnull-dereference \
 -Wredundant-decls -Wvla -Wswitch-default -Werror"
GCC_ONLY="-Wlogical-op -Wduplicated-cond -Wduplicated-branches -Wjump-misses-init -Wcast-align=strict"
# build.py's target flags (keep in step with CFLAGS there).
TARGET_FLAGS="-mcpu=cortex-m7 -mthumb -mfloat-abi=soft -O2 -g3 -std=c11 -ffreestanding \
 -ffunction-sections -fdata-sections -Isrc"
CLANG_TARGET="--target=arm-none-eabi -mcpu=cortex-m7 -mthumb -mfloat-abi=soft -ffreestanding -std=c11 -Isrc"
# The standard's base set; docs/CODING-STANDARD.md says why each of the five checks is off.
TIDY_BASE='-*,bugprone-*,cert-*,clang-analyzer-*,misc-*,performance-*,portability-*,readability-*,-readability-magic-numbers,-readability-identifier-length,-misc-include-cleaner,-bugprone-easily-swappable-parameters,-readability-avoid-unconditional-preprocessor-if'
# DEV-MEM-01 (memory-mapped registers, docs/deviations/memlab.md): these two are off only for the
# register-level target files. The portable code and the host tests are checked with them on.
TIDY_DEV01=',-performance-no-int-to-ptr,-clang-analyzer-core.FixedAddressDereference'
TIDY_CONFIG='{CheckOptions: {readability-function-cognitive-complexity.Threshold: 25, readability-function-size.LineThreshold: 80}}'
HOSTCC="${HOSTCC:-gcc}"
CLANG="${CLANG:-clang-21}"
TIDY="${TIDY:-clang-tidy-21}"
PROFDATA="${PROFDATA:-llvm-profdata-21}"
LLVMCOV="${LLVMCOV:-llvm-cov-21}"
CPPCHECK="${CPPCHECK:-cppcheck}"
PY="${PYTHON:-python3}"
# The versions the standard was measured with: another version gives another verdict.
V_CPPCHECK='Cppcheck 2\.16\.0( |$)'
V_LLVM='version 21\.'

TARGET_SRC=$(ls src/*.c)
PORTABLE_SRC=$(ls src/bench_chain.c src/libmin.c 2>/dev/null)
HOST_TESTS=$(ls tests/*.c 2>/dev/null || true)
PY_SRC=$(ls tools/*.py tests/*.py)
# shellcheck disable=SC2086
REGISTER_SRC=$(for f in $TARGET_SRC; do case " $(echo $PORTABLE_SRC) " in *" $f "*) ;; *) echo "$f" ;; esac; done)

SCRATCH=$(mktemp -d -p . .quality-XXXXXX)   # in the repo: a Windows gcc can write here (.gitignore: .quality-*/)
trap 'rm -rf "$SCRATCH"' EXIT
LOG="$SCRATCH/log"
SUMMARY=()
FAILED=0
SKIPS=""

want() { [ -z "$ONLY" ] || [ "$ONLY" = "$1" ]; }
allowed_skip() { case " $ALLOW_SKIP " in *" $1 "*) true ;; *) false ;; esac; }

# record <check> <files> <findings> [SKIPPED|FAIL] [note]
# A SKIPPED check proves nothing: it fails the run unless this run named it with --allow-skip.
record() {
    local verdict="PASS" note="${5:-}"
    case "${4:-}" in
        SKIPPED)
            if allowed_skip "$1"; then verdict="SKIPPED (allowed by --allow-skip: $note)"; SKIPS="$SKIPS $1"
            else verdict="FAIL (SKIPPED without --allow-skip $1: $note)"; FAILED=1; fi ;;
        FAIL) verdict="FAIL ($note)"; FAILED=1 ;;
        *) if [ "$3" != "0" ]; then verdict="FAIL"; FAILED=1; fi ;;
    esac
    SUMMARY+=("$(printf '| %-13s | %5s | %8s | %s |' "$1" "$2" "$3" "$verdict")")
}

# need <check> <tool> [version regex]: FAIL the check (and return 1) when the tool is missing or
# is not the version the standard names. A missing tool must never read as "0 findings".
need() {
    local check=$1 t=$2 vre=${3:-} v
    if ! command -v "$t" >/dev/null 2>&1; then
        echo "FAIL: $t not found"; record "$check" - - FAIL "tool missing: $t"; return 1
    fi
    if [ -n "$vre" ]; then
        v=$("$t" --version 2>&1 | tr -d '\r' | head -3 | tr '\n' ' ')
        if ! printf '%s' "$v" | grep -qE "$vre"; then
            echo "FAIL: $t reports '$v'; the standard needs /$vre/"
            record "$check" - - FAIL "wrong version: $t"; return 1
        fi
    fi
    return 0
}

count() { grep -cE "$1" "$LOG" || true; }
nfiles() { echo $#; }

# The Arm gcc: ARM_GCC, else on PATH (CI), else the Windows toolchain named in build.py's DEFAULT_GCC.
# In Git Bash that one runs natively. From WSL it is started through powershell.exe (a Windows gcc
# launched straight from WSL cannot find its own cc1), which needs WSL interop.
ARM_MODE=direct
find_arm_gcc() {
    if [ -n "${ARM_GCC:-}" ]; then echo "$ARM_GCC"; return 0; fi
    if command -v arm-none-eabi-gcc >/dev/null 2>&1; then echo arm-none-eabi-gcc; return 0; fi
    local dir
    dir=$(tr -d '\r' < build.py | sed -n 's/^DEFAULT_GCC = r"\(.*\)"$/\1/p')
    [ -n "$dir" ] || return 0
    if command -v cygpath >/dev/null 2>&1 && ! command -v wslpath >/dev/null 2>&1; then
        if [ -x "$(cygpath -u "$dir")/arm-none-eabi-gcc.exe" ]; then echo "$(cygpath -u "$dir")/arm-none-eabi-gcc.exe"; fi
        return 0
    fi
    if command -v wslpath >/dev/null 2>&1 && command -v powershell.exe >/dev/null 2>&1 \
            && [ -x "$(wslpath -u "$dir")/arm-none-eabi-gcc.exe" ]; then
        echo "$dir/arm-none-eabi-gcc.exe"
    fi
    return 0
}
arm_gcc() {
    if [ "$ARM_MODE" = powershell ]; then
        powershell.exe -NoProfile -Command "& '$ARMGCC' $*; exit \$LASTEXITCODE"
    else "$ARMGCC" "$@"; fi
}

# ---- target code: the Arm gcc with build.py's flags + the strict set -------
if want target-gcc; then
    echo "== target-gcc"
    ARMGCC=$(find_arm_gcc)
    case "$ARMGCC" in [A-Za-z]:/*) ARM_MODE=powershell ;; esac   # a Windows path, seen from WSL
    if [ -z "$ARMGCC" ]; then
        record target-gcc "$(nfiles $TARGET_SRC)" - SKIPPED "no arm-none-eabi-gcc"
    elif ! arm_gcc --version >/dev/null 2>&1; then
        record target-gcc "$(nfiles $TARGET_SRC)" - SKIPPED "$ARMGCC does not run (WSL interop off?)"
    else
        arm_gcc --version 2>&1 | tr -d '\r' | head -1
        : > "$LOG"
        for f in $TARGET_SRC; do
            # shellcheck disable=SC2086
            arm_gcc $TARGET_FLAGS $WARN $GCC_ONLY -fstack-usage -Wstack-usage=1024 \
                -c "$f" -o "$SCRATCH/$(basename "$f").o" >>"$LOG" 2>&1 || echo "error: $f did not compile" >>"$LOG"
        done
        sed -i 's/\r$//' "$LOG"
        cat "$LOG"
        record target-gcc "$(nfiles $TARGET_SRC)" "$(count '(warning|error):')"
    fi
fi

# ---- target code: clang, syntax only ---------------------------------------
if want clang-target && { echo "== clang-target"; need clang-target "$CLANG" "$V_LLVM"; }; then
    : > "$LOG"
    for f in $TARGET_SRC; do
        # shellcheck disable=SC2086
        "$CLANG" $CLANG_TARGET $WARN -fsyntax-only "$f" >>"$LOG" 2>&1 || echo "error: $f" >>"$LOG"
    done
    cat "$LOG"
    record clang-target "$(nfiles $TARGET_SRC)" "$(count '(warning|error):')"
fi

# ---- portable C and host tests: host gcc + clang, strict -------------------
if want host-cc && { echo "== host-cc"; need host-cc "$HOSTCC" && need host-cc "$CLANG" "$V_LLVM"; }; then
    : > "$LOG"
    for f in $PORTABLE_SRC; do
        # shellcheck disable=SC2086
        "$HOSTCC" -std=c99 -Isrc $WARN $GCC_ONLY -fsyntax-only "$f" >>"$LOG" 2>&1 || echo "error: $HOSTCC $f" >>"$LOG"
        # shellcheck disable=SC2086
        "$CLANG" -std=c99 -Isrc $WARN -fsyntax-only "$f" >>"$LOG" 2>&1 || echo "error: clang $f" >>"$LOG"
    done
    for f in $HOST_TESTS; do
        # shellcheck disable=SC2086
        "$HOSTCC" -std=c11 -Isrc $WARN $GCC_ONLY -fsyntax-only "$f" >>"$LOG" 2>&1 || echo "error: $HOSTCC $f" >>"$LOG"
        # shellcheck disable=SC2086
        "$CLANG" -std=c11 -Isrc $WARN -fsyntax-only "$f" >>"$LOG" 2>&1 || echo "error: clang $f" >>"$LOG"
    done
    cat "$LOG"
    # shellcheck disable=SC2086
    record host-cc "$(nfiles $PORTABLE_SRC $HOST_TESTS)" "$(count '(warning|error):')"
fi

# ---- MISRA C:2012 (cppcheck misra addon) -----------------------------------
if want misra && { echo "== misra"; need misra "$CPPCHECK" "$V_CPPCHECK"; }; then
    SUPP="$SCRATCH/suppressions.txt"
    cat tools/quality/suppress-*.txt 2>/dev/null | tr -d '\r' | grep -vE '^\s*(#|$)' > "$SUPP" || true
    mkdir -p "$SCRATCH/cppcheck"        # keeps cppcheck's work files out of the repo
    # shellcheck disable=SC2086
    "$CPPCHECK" --addon=misra --std=c11 --platform=unix32 --enable=warning,style,performance,portability \
        --inline-suppr --error-exitcode=1 -Isrc --suppressions-list="$SUPP" \
        --cppcheck-build-dir="$SCRATCH/cppcheck" \
        --template='{file}:{line}: {severity} {id}: {message}' --quiet $TARGET_SRC >"$LOG" 2>&1
    rc=$?
    cat "$LOG"
    n=$(grep -cE '^[^ ]+:[0-9]+: ' "$LOG" || true)
    # A non-zero exit with no finding line is a tool failure (addon not found, crash), not a pass.
    if [ "$rc" -ne 0 ] && [ "$n" = 0 ]; then echo "cppcheck failed (exit $rc)"; n=1; fi
    record misra "$(nfiles $TARGET_SRC)" "$n"
fi

# ---- CERT C and bug-finding (clang-tidy) -----------------------------------
if want tidy && { echo "== tidy"; need tidy "$TIDY" "$V_LLVM"; }; then
    rc=0
    # shellcheck disable=SC2086
    "$TIDY" $REGISTER_SRC -checks="$TIDY_BASE$TIDY_DEV01" -config="$TIDY_CONFIG" -warnings-as-errors='*' \
        --quiet -- $CLANG_TARGET $WARN >"$LOG" 2>&1 || rc=$?
    # shellcheck disable=SC2086
    "$TIDY" $PORTABLE_SRC -checks="$TIDY_BASE" -config="$TIDY_CONFIG" -warnings-as-errors='*' \
        --quiet -- $CLANG_TARGET $WARN >>"$LOG" 2>&1 || rc=$?
    if [ -n "$HOST_TESTS" ]; then
        # shellcheck disable=SC2086
        "$TIDY" $HOST_TESTS -checks="$TIDY_BASE" -config="$TIDY_CONFIG" -warnings-as-errors='*' \
            --quiet -- -std=c11 -Isrc $WARN >>"$LOG" 2>&1 || rc=$?
    fi
    grep -E '(warning|error):' "$LOG" | grep -v 'warnings treated as errors' || true
    n=$(grep -E '(warning|error):' "$LOG" | grep -vc 'warnings treated as errors' || true)
    if [ "$rc" -ne 0 ] && [ "$n" = 0 ]; then echo "clang-tidy failed (exit $rc)"; tail -20 "$LOG"; n=1; fi
    # shellcheck disable=SC2086
    record tidy "$(nfiles $TARGET_SRC $HOST_TESTS)" "$n"
fi

# ---- host C tests under ASan + UBSan, host gcc and clang -------------------
build_host_test() {   # <cc> <test.c> <out> <extra flags...>
    local cc=$1 t=$2 out=$3; shift 3
    local mod
    mod=$(sed -n 's|^/\* *module: *\(src/[a-z_]*\.c\).*|\1|p' "$t" | head -1)
    # shellcheck disable=SC2086
    "$cc" -std=c11 -Isrc $WARN "$@" "$t" $mod -o "$out"
}

if want sanitizers && { echo "== sanitizers"; need sanitizers "$HOSTCC" && need sanitizers "$CLANG" "$V_LLVM"; }; then
    : > "$LOG"
    for t in $HOST_TESTS; do
        for cc in "$HOSTCC" "$CLANG"; do
            exe="$SCRATCH/$(basename "$t" .c)-$(basename "$cc")"
            if build_host_test "$cc" "$t" "$exe" -fsanitize=address,undefined \
                    -fno-sanitize-recover=all -g -O1 >>"$LOG" 2>&1; then
                "$exe" >>"$LOG" 2>&1 || echo "error: $exe failed" >>"$LOG"
            else
                echo "error: $t did not build with $cc" >>"$LOG"
            fi
        done
    done
    cat "$LOG"
    # shellcheck disable=SC2086
    record sanitizers "$(nfiles $HOST_TESTS)" "$(count '(error:|ERROR:|runtime error)')"
fi

# ---- coverage of the portable C modules (clang source-based) ---------------
if want coverage && { echo "== coverage"; need coverage "$CLANG" "$V_LLVM" && need coverage "$PROFDATA" "$V_LLVM" \
        && need coverage "$LLVMCOV" "$V_LLVM"; }; then
    : > "$LOG"
    PROFILES=""; OBJS=""
    for t in $HOST_TESTS; do
        exe="$SCRATCH/$(basename "$t" .c)-cov"
        if build_host_test "$CLANG" "$t" "$exe" -fprofile-instr-generate -fcoverage-mapping -O0 >>"$LOG" 2>&1; then
            LLVM_PROFILE_FILE="$exe.profraw" "$exe" >>"$LOG" 2>&1 || echo "error: $exe failed" >>"$LOG"
            PROFILES="$PROFILES $exe.profraw"
            if [ -z "$OBJS" ]; then OBJS="$exe"; else OBJS="$OBJS -object $exe"; fi
        else
            echo "error: $t did not build for coverage" >>"$LOG"
        fi
    done
    if [ -n "$PROFILES" ]; then
        # shellcheck disable=SC2086
        "$PROFDATA" merge -sparse $PROFILES -o "$SCRATCH/all.profdata" >>"$LOG" 2>&1 \
            || echo "error: llvm-profdata merge failed" >>"$LOG"
        # shellcheck disable=SC2086
        "$LLVMCOV" report $OBJS -instr-profile="$SCRATCH/all.profdata" -show-branch-summary \
            $PORTABLE_SRC > "$SCRATCH/cov.txt" 2>&1 || echo "error: llvm-cov report failed" >>"$LOG"
        cat "$SCRATCH/cov.txt"
    else
        echo "error: no coverage profile (no host C test built)" >>"$LOG"
    fi
    cat "$LOG"
    # The gate fails on a test or tool failure only; the table is reported, not gated.
    # shellcheck disable=SC2086
    record coverage "$(nfiles $PORTABLE_SRC)" "$(count '(error:|FAIL)')"
fi

# ---- Python: unit tests (results parser, disasm checker, chain host test) --
if want python && { echo "== python"; need python "$PY"; }; then
    "$PY" -m unittest discover -s tests -v >"$LOG" 2>&1 || echo "error: unittest failed" >>"$LOG"
    tail -5 "$LOG"
    # shellcheck disable=SC2086
    record python "$(nfiles $PY_SRC)" "$(count '^(error:|FAIL:|ERROR:)')"
fi

echo
echo "| check         | files | findings | verdict |"
echo "|---------------|-------|----------|---------|"
printf '%s\n' "${SUMMARY[@]}"
[ -n "$ALLOW_SKIP" ] && echo "allowed to skip (--allow-skip):$ALLOW_SKIP"
if [ "$FAILED" != 0 ]; then echo "RESULT: FAIL"; exit 1; fi
if [ -n "$SKIPS" ]; then echo "RESULT: PASS, except skipped (allowed):$SKIPS"; exit 0; fi
echo "RESULT: PASS"
exit 0
