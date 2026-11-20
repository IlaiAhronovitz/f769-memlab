#!/usr/bin/env python3
"""build.py - build, flash, run and read f769-memlab. No make needed.

    python build.py [build]        build/memlab.elf (+ .bin, .map)
    python build.py flash          build, then program the board and reset it
    python build.py run            flash, wait for the suite to finish, then
                                   write results/run-YYYYmmdd-HHMM.md
    python build.py read           dump the results block and print it
    python build.py test           host unit tests (tools/, no board needed)

Options: --serial SN  pin the ST-LINK by USB serial. This PC has many
ST-LINKs; two of them are ST-LINK/V2 (USB PID 3748), which OpenOCD cannot
tell apart by PID. Pass --serial whenever more than one is plugged in.
Environment: GCC_BIN (folder of arm-none-eabi-gcc), OPENOCD (openocd binary).
"""
import argparse
import datetime
import glob
import os
import shutil
import subprocess
import sys
import time

ROOT = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(ROOT, "tools"))
import results  # noqa: E402  (tools/results.py)

DEFAULT_GCC = r"C:/Program Files (x86)/Arm GNU Toolchain arm-none-eabi/14.2 rel1/bin"
OCD_GLOB = os.path.join(os.environ.get("LOCALAPPDATA", ""), "Microsoft", "WinGet", "Packages",
                        "xpack-dev-tools.openocd-xpack_*", "xpack-openocd-*")
ELF = "build/memlab.elf"
RESULTS_ADDR = 0x20000000
RESULTS_LEN = 8192

ARCH = ["-mcpu=cortex-m7", "-mthumb", "-mfloat-abi=soft"]
CFLAGS = ARCH + ["-O2", "-g3", "-std=c11", "-Wall", "-Wextra", "-Werror",
                 "-ffreestanding", "-ffunction-sections", "-fdata-sections",
                 "-Isrc"]
LDFLAGS = ARCH + ["-nostartfiles", "-nostdlib", "-Tlinker/stm32f769ni.ld",
                  "-Wl,--gc-sections", "-Wl,-Map=build/memlab.map"]


def tool(name):
    gcc_bin = os.environ.get("GCC_BIN", DEFAULT_GCC)
    path = os.path.join(gcc_bin, name + (".exe" if os.name == "nt" else ""))
    if os.path.exists(path):
        return path
    found = shutil.which(name)
    if not found:
        sys.exit(f"{name} not found: set GCC_BIN")
    return found


def run(cmd, **kw):
    print(" ".join(cmd), flush=True)
    return subprocess.run(cmd, check=True, **kw)


def build():
    os.makedirs("build", exist_ok=True)
    cc = tool("arm-none-eabi-gcc")
    objs = []
    for src in sorted(glob.glob("src/*.c")) + ["src/startup.s"]:
        obj = os.path.join("build", os.path.basename(src) + ".o")
        run([cc] + CFLAGS + ["-c", src, "-o", obj])
        objs.append(obj)
    run([cc] + LDFLAGS + objs + ["-lgcc", "-o", ELF])
    run([tool("arm-none-eabi-objcopy"), "-O", "binary", ELF, "build/memlab.bin"])
    run([tool("arm-none-eabi-size"), ELF])


def openocd(args, serial):
    exe = os.environ.get("OPENOCD")
    if exe:
        scripts = os.path.join(os.path.dirname(os.path.dirname(exe)), "openocd", "scripts")
    else:
        roots = glob.glob(OCD_GLOB)
        if not roots:
            sys.exit("OpenOCD not found: set OPENOCD")
        exe = os.path.join(roots[0], "bin", "openocd.exe")
        scripts = os.path.join(roots[0], "openocd", "scripts")
    cmd = [exe, "-s", scripts, "-f", "interface/stlink.cfg"]
    if serial:
        cmd += ["-c", f"adapter serial {serial}"]
    cmd += ["-f", "target/stm32f7x.cfg"] + args
    return run(cmd, capture_output=True, text=True)


def flash(serial):
    build()
    openocd(["-c", f"program {ELF} verify reset exit"], serial)


def dump(serial):
    path = os.path.join("build", "results.bin")
    # Halt first: an ST-LINK/V2 can read zeros while the core sleeps in WFI.
    # Halting out of WFI sometimes fails on this probe, so retry a few times.
    for attempt in range(3):
        try:
            openocd(["-c", "init", "-c", "halt",
                     "-c", f"dump_image {path} 0x{RESULTS_ADDR:08X} {RESULTS_LEN}",
                     "-c", "resume", "-c", "shutdown"], serial)
            break
        except subprocess.CalledProcessError:
            if attempt == 2:
                raise
            time.sleep(1)
    with open(path, "rb") as f:
        return f.read()


BACKUP = "backup/eval_flash_2026-11-20.bin"
BACKUP_SHA256 = "89599A5C3445BF1E2BA05B84B96C442ED12018C29352B7DF3A551B078C6C52A3"


def restore(serial):
    """Put back the image that was on the EVAL before this project (2 MiB)."""
    import hashlib
    with open(BACKUP, "rb") as f:
        digest = hashlib.sha256(f.read()).hexdigest().upper()
    if digest != BACKUP_SHA256:
        sys.exit(f"backup checksum mismatch: {digest}")
    openocd(["-c", "init", "-c", "reset halt",
             "-c", f"flash write_image erase {BACKUP} 0x08000000 bin",
             "-c", f"verify_image {BACKUP} 0x08000000 bin",
             "-c", "reset run", "-c", "shutdown"], serial)


def wait_done(serial, timeout_s=60):
    t0 = time.time()
    while True:
        blob = dump(serial)
        res = results.parse(blob)
        if res.status != results.ST_RUNNING or time.time() - t0 > timeout_s:
            return blob
        print(f"running: step {res.step}, {len(res.rows)} rows", flush=True)
        time.sleep(2)


def main():
    os.chdir(ROOT)
    ap = argparse.ArgumentParser()
    ap.add_argument("cmd", nargs="?", default="build",
                    choices=["build", "flash", "run", "read", "test", "restore"])
    ap.add_argument("--serial")
    a = ap.parse_args()

    if a.cmd == "build":
        build()
    elif a.cmd == "flash":
        flash(a.serial)
    elif a.cmd == "restore":
        restore(a.serial)
    elif a.cmd == "test":
        sys.exit(subprocess.call([sys.executable, "-m", "unittest", "discover",
                                  "-s", "tests", "-v"]))
    else:
        if a.cmd == "run":
            flash(a.serial)
            time.sleep(1)
            blob = wait_done(a.serial)
        else:
            blob = dump(a.serial)
        text = results.to_markdown(results.parse(blob))
        print(text)
        if a.cmd == "run":
            os.makedirs("results", exist_ok=True)
            stamp = datetime.datetime.now().strftime("%Y%m%d-%H%M")
            with open(f"results/run-{stamp}.md", "w", encoding="utf-8") as f:
                f.write(text)
            with open(f"results/run-{stamp}.bin", "wb") as f:
                f.write(blob)


if __name__ == "__main__":
    main()
