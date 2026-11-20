"""results.py - parse and format the f769-memlab results block.

The layout mirrors src/results.h: a 128-byte header of 32 little-endian
words, then up to 400 rows of 20 bytes (<HBBIIII). Pure Python, no board
needed, so it is unit-tested on the host (tests/test_results.py).
"""
import struct
from dataclasses import dataclass, field

MAGIC = 0x4C4D374D
FORMAT_VERSION = 1
ST_RUNNING, ST_DONE, ST_FAULT = 1, 2, 3
HEADER_WORDS = 32
ROW = struct.Struct("<HBBIIII")

TESTS = {1: "chase", 2: "seq read", 3: "seq write", 10: "DMA, no maintenance",
         11: "DMA, clean + invalidate", 12: "DMA, non-cacheable", 13: "clean range",
         14: "invalidate range", 15: "DMA copy", 16: "CPU fill"}
REGIONS = {1: "DTCM", 2: "SRAM1", 3: "SRAM2", 4: "ITCM", 5: "flash AXIM",
           6: "flash ITCM+ART", 7: "DMA buffers"}
CFGS = {0: "caches off", 1: "I on", 2: "I+D, WB", 3: "I+D, WT", 4: "I+D, NC",
        5: "caches off, ART off", 6: "I+D, ART off"}
HEADER_FIELDS = ["magic", "version", "status", "n_rows", "idcode", "cpuid", "clidr",
                 "ccsidr_d", "ccsidr_i", "mpu_type", "sysclk_hz", "pllcfgr", "cfgr",
                 "acr", "pwr_csr1", "fault_pc", "fault_cfsr", "fault_hfsr",
                 "fault_addr", "step"]


@dataclass
class Row:
    test: int
    region: int
    cfg: int
    size: int
    ops: int
    cycles: int
    extra: int

    def per_op(self):
        return self.cycles / self.ops if self.ops else float("nan")

    def bytes_per_cycle(self):
        return 4.0 * self.ops / self.cycles if self.cycles else float("nan")


@dataclass
class Results:
    header: dict
    rows: list = field(default_factory=list)

    @property
    def status(self):
        return self.header["status"]

    @property
    def step(self):
        return self.header["step"]


def parse(blob):
    if len(blob) < HEADER_WORDS * 4:
        raise ValueError("results block too short")
    words = struct.unpack_from(f"<{HEADER_WORDS}I", blob, 0)
    header = dict(zip(HEADER_FIELDS, words))
    header["clock_rc"] = struct.unpack("<i", struct.pack("<I", words[20]))[0]
    if header["magic"] != MAGIC:
        raise ValueError(f"bad magic 0x{header['magic']:08X}: firmware not running?")
    if header["version"] != FORMAT_VERSION:
        raise ValueError(f"format version {header['version']}, expected {FORMAT_VERSION}")
    n = min(header["n_rows"], (len(blob) - HEADER_WORDS * 4) // ROW.size)
    rows = [Row(*ROW.unpack_from(blob, HEADER_WORDS * 4 + i * ROW.size)) for i in range(n)]
    return Results(header, rows)


def cache_geometry(ccsidr):
    """(size in bytes, ways, line bytes) from a CCSIDR value."""
    line = 1 << ((ccsidr & 7) + 4)
    ways = ((ccsidr >> 3) & 0x3FF) + 1
    sets = ((ccsidr >> 13) & 0x7FFF) + 1
    return sets * ways * line, ways, line


def describe(h):
    dev, rev = h["idcode"] & 0xFFF, h["idcode"] >> 16
    cpu = h["cpuid"]
    d_size, d_ways, d_line = cache_geometry(h["ccsidr_d"])
    i_size, i_ways, i_line = cache_geometry(h["ccsidr_i"])
    status = {ST_RUNNING: "RUNNING", ST_DONE: "DONE", ST_FAULT: "FAULT"}.get(h["status"], "?")
    lines = [
        f"- Status: {status}, {h['n_rows']} rows, clock rc {h['clock_rc']}",
        f"- Device: DEV_ID 0x{dev:03X}, REV_ID 0x{rev:04X}; core r{(cpu >> 20) & 0xF}p{cpu & 0xF}"
        f" (CPUID 0x{cpu:08X})",
        f"- SYSCLK: {h['sysclk_hz'] / 1e6:.0f} MHz (PLLCFGR 0x{h['pllcfgr']:08X},"
        f" CFGR 0x{h['cfgr']:08X}, FLASH_ACR 0x{h['acr']:08X})",
        f"- D-cache: {d_size // 1024} KiB, {d_ways}-way, {d_line}-byte lines;"
        f" I-cache: {i_size // 1024} KiB, {i_ways}-way, {i_line}-byte lines",
        f"- MPU regions: {(h['mpu_type'] >> 8) & 0xFF}",
    ]
    if h["status"] == ST_FAULT:
        lines.append(f"- FAULT at pc 0x{h['fault_pc']:08X}, CFSR 0x{h['fault_cfsr']:08X},"
                     f" HFSR 0x{h['fault_hfsr']:08X}, BFAR 0x{h['fault_addr']:08X}")
    return lines


def _kib(n):
    return f"{n // 1024} KiB" if n >= 1024 else f"{n} B"


def chase_tables(res):
    out = []
    chase = [r for r in res.rows if r.test == 1]
    for region in sorted({r.region for r in chase}):
        rows = [r for r in chase if r.region == region]
        cfgs = sorted({r.cfg for r in rows})
        sizes = sorted({r.size for r in rows})
        out.append(f"\n### Pointer chase, {REGIONS.get(region, region)}: cycles per load\n")
        out.append("| Working set | " + " | ".join(CFGS.get(c, str(c)) for c in cfgs) + " |")
        out.append("|---|" + "---|" * len(cfgs))
        for s in sizes:
            cells = []
            for c in cfgs:
                hit = [r for r in rows if r.size == s and r.cfg == c]
                cells.append(f"{hit[0].per_op():.2f}" if hit else "")
            out.append(f"| {_kib(s)} | " + " | ".join(cells) + " |")
    return out


def seq_tables(res):
    out = ["\n### Sequential access: bytes per cycle\n",
           "| Region | Working set | Configuration | Read | Write |", "|---|---|---|---|---|"]
    seq = [r for r in res.rows if r.test in (2, 3)]
    keys = sorted({(r.region, r.size, r.cfg) for r in seq})
    for region, size, cfg in keys:
        cells = []
        for test in (2, 3):
            hit = [r for r in seq if (r.region, r.size, r.cfg, r.test) == (region, size, cfg, test)]
            cells.append(f"{hit[0].bytes_per_cycle():.2f}" if hit else "")
        out.append(f"| {REGIONS.get(region, region)} | {_kib(size)} | {CFGS.get(cfg, cfg)} | "
                   f"{cells[0]} | {cells[1]} |")
    return out


def dma_table(res):
    out = ["\n### DMA and the D-cache (8 KiB copy, SRAM1)\n",
           "| Test | Configuration | Cycles | Mismatching words |", "|---|---|---|---|"]
    for r in res.rows:
        if r.test >= 10:
            mism = str(r.extra) if r.test in (10, 11, 12) else ""
            out.append(f"| {TESTS.get(r.test, r.test)} | {CFGS.get(r.cfg, r.cfg)} | "
                       f"{r.cycles} | {mism} |")
    return out


def to_markdown(res):
    out = ["# f769-memlab results", ""] + describe(res.header)
    out += chase_tables(res) + seq_tables(res) + dma_table(res)
    return "\n".join(out) + "\n"
