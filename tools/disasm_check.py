#!/usr/bin/env python3
"""disasm_check.py - prove that an edit left the timed machine code alone.

    python tools/disasm_check.py BASE NEW     (each an .elf, or `objdump -d` text)

The published cycle counts (results/) are only reproducible while the timed
code compiles to the same instructions. This compares two builds function by
function, after removing what legitimately moves when other code changes size:
instruction addresses, raw encodings (they hold branch and literal offsets)
and the absolute address printed before a `<symbol+offset>` target.

Protected functions are every function placed in ITCM (.itcm_text, the timed
loops) and the cache/MPU and DMA-test functions listed in PROTECTED. A protected
function fails when its instructions differ, or when its start address moved
(MOVED): its alignment to the I-cache lines is part of what was measured.

Data layout: with two .elf files, the addresses of every data, bss and rodata
symbol (`nm`, names without GCC's numeric suffixes) must be equal too, and so must
the address of every non-code input section in the linker map next to each .elf
(`memlab.map`): GCC emits no symbol for some static tables (`k_regions`,
`k_cfgs`), but the map still places `.rodata.k_regions`. DEV-MEM-10 showed a moved
`.rodata` table change published figures with identical code.

Exit status: 0 when every protected function is identical and in place and no
data symbol moved, appeared or vanished; 1 otherwise. The other functions are
listed too, for the record. Passing is necessary, not sufficient: a change that
moves code or data still needs the board comparison (docs/CODING-STANDARD.md).
"""
import os
import re
import subprocess
import sys

# cache.c (the maintenance sequences) and dmatest.c (code between the cycle
# reads). A static function that GCC inlined does not appear on its own: its
# instructions are compared inside the caller.
PROTECTED = {
    "icache_enable", "icache_disable", "dcache_all", "dcache_enable", "dcache_disable",
    "dcache_clean_invalidate_all", "dcache_clean_range", "dcache_invalidate_range",
    "mpu_region", "mpu_enable", "mpu_disable",
    "dmatest_run", "dma_copy", "prepare", "fill", "mismatches", "touch", "cyc",
}
FLASH_BASE = 0x08000000          # below this: ITCM (the .itcm_text copies)

_HEADER = re.compile(r"^([0-9a-f]+) <([^>]+)>:$")
_INSN = re.compile(r"^\s*[0-9a-f]+:\t[0-9a-f ]+\t(.*)$")   # 16- and 32-bit Thumb
_ADDR_BEFORE_SYM = re.compile(r"\b[0-9a-f]+ (<[^>]+>)")
_COMMENT_ADDR = re.compile(r"@ \(([0-9a-f]+) (<[^>]+>)\)")


def normalize(text):
    """One instruction's text without the addresses that move with layout."""
    text = _COMMENT_ADDR.sub(r"@ (\2)", text)
    text = _ADDR_BEFORE_SYM.sub(r"\1", text)
    return " ".join(text.split())


_CLONE = re.compile(r"\.(constprop|isra|part|cold)\.\d+$|\.\d+$")


def base_name(symbol):
    """`dma_copy.constprop.0` -> `dma_copy`, `k_cfgs.1` -> `k_cfgs`: GCC numbers
    clones and function-scope statics, and the numbers move between builds."""
    return _CLONE.sub("", symbol)


def parse(listing):
    """{function name: (start address, [normalized instructions])}."""
    funcs = {}
    current = None
    for line in listing.splitlines():
        head = _HEADER.match(line)
        if head:
            current = []
            funcs[base_name(head.group(2))] = (int(head.group(1), 16), current)
            continue
        insn = _INSN.match(line)
        if insn and current is not None:
            current.append(normalize(insn.group(1)))
    return funcs


def is_protected(name, addr):
    return addr < FLASH_BASE or name in PROTECTED


def compare(base, new):
    """[(name, verdict, protected)], verdict: identical, CHANGED, added, removed."""
    rows = []
    for name in sorted(set(base) | set(new)):
        if name not in new:
            rows.append((name, "removed", is_protected(name, base[name][0])))
        elif name not in base:
            rows.append((name, "added", is_protected(name, new[name][0])))
        else:
            if base[name][1] != new[name][1]:
                verdict = "CHANGED"
            elif base[name][0] != new[name][0]:
                verdict = "MOVED"
            else:
                verdict = "identical"
            rows.append((name, verdict,
                         is_protected(name, base[name][0]) or is_protected(name, new[name][0])))
    return rows


def failures(rows):
    """Protected functions that differ, vanished or appeared."""
    return [r for r in rows if r[2] and r[1] != "identical"]


_NM = re.compile(r"^([0-9a-f]+) ([A-Za-z]) (\S+)$")
_DATA_TYPES = set("bBdDrR")      # .bss, .data, .rodata (and the linker sections that hold them)


def parse_nm(listing):
    """{data symbol name: sorted [addresses]} from `nm -n` output. Mapping
    symbols ($d, $t) are skipped; a name defined in several files keeps every
    address."""
    syms = {}
    for line in listing.splitlines():
        m = _NM.match(line.strip())
        if not m or m.group(2) not in _DATA_TYPES or m.group(3).startswith("$"):
            continue
        syms.setdefault(base_name(m.group(3)), []).append(int(m.group(1), 16))
    return {name: sorted(addrs) for name, addrs in syms.items()}


def compare_data(base, new):
    """[(name, verdict)] for every data symbol whose addresses differ: MOVED,
    added or removed. Every row is a failure."""
    rows = []
    for name in sorted(set(base) | set(new)):
        if name not in new:
            rows.append((name, "removed"))
        elif name not in base:
            rows.append((name, "added"))
        elif base[name] != new[name]:
            rows.append((name, "MOVED"))
    return rows


_MAP_NAME_ONLY = re.compile(r"^ (\.\S+)$")
_MAP_PLACE = re.compile(r"^ (\.\S+)?\s+0x([0-9a-f]+)\s+0x([0-9a-f]+)\s+(\S.*)$")


# Code (compared instruction by instruction) and sections that are not loaded.
_NOT_LAYOUT = (".text", ".debug", ".comment", ".ARM.attributes", ".note", ".stab")


def parse_map(listing):
    """{input section: sorted [addresses]} for every loaded non-code input
    section the linker placed (after "Linker script and memory map"). `.text*`
    is left to the instruction comparison; debug info and zero-size sections
    are ignored. The object file is not part of the key: moving a definition to
    another file at the same address (s_perm, bench.c -> bench_chain.c) keeps
    the layout."""
    placed = {}
    started = False
    pending = None
    for line in listing.splitlines():
        line = line.rstrip()
        if line.startswith("Linker script and memory map"):
            started = True
            continue
        if not started:
            continue
        only = _MAP_NAME_ONLY.match(line)
        if only:
            pending = only.group(1)
            continue
        m = _MAP_PLACE.match(line)
        name = (m.group(1) if m and m.group(1) else pending) if m else None
        pending = None
        if not name or name.startswith(_NOT_LAYOUT) or int(m.group(3), 16) == 0:
            continue
        placed.setdefault(base_name(name), []).append(int(m.group(2), 16))
    return {name: sorted(addrs) for name, addrs in placed.items()}


def compare_map(base, new):
    """[(section, verdict)] for every non-code input section that MOVED, was
    added or removed. Every row is a failure."""
    return compare_data(base, new)


def load_map(elf):
    """The linker map next to an .elf (`x.elf` -> `x.map`), or None."""
    path = elf[:-4] + ".map" if elf.endswith(".elf") else None
    if not path or not os.path.exists(path):
        return None
    with open(path, encoding="utf-8", errors="replace") as f:
        return f.read()


def _gcc_bin():
    """GCC_BIN, else build.py's DEFAULT_GCC (one copy of the install path)."""
    if os.environ.get("GCC_BIN"):
        return os.environ["GCC_BIN"]
    build_py = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "build.py")
    with open(build_py, encoding="utf-8") as f:
        m = re.search(r'^DEFAULT_GCC = r"(.*)"$', f.read(), re.M)
    return m.group(1) if m else ""


def tool_path(name):
    exe = os.path.join(_gcc_bin(), "arm-none-eabi-" + name + (".exe" if os.name == "nt" else ""))
    return exe if os.path.exists(exe) else "arm-none-eabi-" + name


def load(path):
    if path.endswith(".elf"):
        return subprocess.run([tool_path("objdump"), "-d", path], check=True,
                              capture_output=True, text=True).stdout
    with open(path, encoding="utf-8") as f:
        return f.read()


def load_nm(path):
    """`nm -n` of an .elf, or None for an objdump text file (no symbol table)."""
    if not path.endswith(".elf"):
        return None
    return subprocess.run([tool_path("nm"), "-n", "--defined-only", path], check=True,
                          capture_output=True, text=True).stdout


def report(rows):
    lines = ["| Function | Timed / protected | Result |", "|---|---|---|"]
    for name, verdict, prot in rows:
        lines.append(f"| `{name}` | {'yes' if prot else 'no'} | {verdict} |")
    return "\n".join(lines)


def main(argv):
    if len(argv) != 3:
        sys.exit(__doc__)
    rows = compare(parse(load(argv[1])), parse(load(argv[2])))
    print(report(rows))
    bad = failures(rows)
    print(f"\n{len(rows)} functions, {sum(r[2] for r in rows)} protected, "
          f"{len(bad)} protected differ or moved")
    base_nm, new_nm = load_nm(argv[1]), load_nm(argv[2])
    if base_nm is None or new_nm is None:
        print("data layout NOT checked: give two .elf files for the symbol-address comparison")
        return 1 if bad else 0
    base_data = parse_nm(base_nm)
    moved = compare_data(base_data, parse_nm(new_nm))
    for name, verdict in moved:
        print(f"data symbol `{name}`: {verdict}")
    print(f"{len(base_data)} data symbols, {len(moved)} moved, added or removed")
    base_map, new_map = load_map(argv[1]), load_map(argv[2])
    if base_map is None or new_map is None:
        print("input sections NOT checked: no .map next to both .elf files")
        return 1
    base_sec = parse_map(base_map)
    sec_moved = compare_map(base_sec, parse_map(new_map))
    for sec, verdict in sec_moved:
        print(f"input section `{sec}`: {verdict}")
    print(f"{len(base_sec)} non-code input sections, {len(sec_moved)} moved, added or removed")
    return 1 if bad or moved or sec_moved else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
