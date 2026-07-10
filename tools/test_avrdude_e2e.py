#!/usr/bin/env python3
"""End-to-end test: avrdude 'oddprog' driver against the OddProg emulator.

Starts the emulator on a pty and drives it with real avrdude invocations:
signature check, flash write/verify/read-back, EEPROM, user signature,
fuse write via terminal mode, lock bits and chip erase.

Usage: test_avrdude_e2e.py [avrdude-binary] [avrdude.conf]
"""

import os
import random
import subprocess
import sys
import tempfile
import threading

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import oddprog_emulator as emu

AVRDUDE = sys.argv[1] if len(sys.argv) > 1 else \
    os.path.expanduser("~/dev/avrdude/build_darwin/src/avrdude")
AVRDUDE_CONF = sys.argv[2] if len(sys.argv) > 2 else \
    os.path.expanduser("~/dev/avrdude/build_darwin/src/avrdude.conf")

failures = 0


def check(name, ok, detail=""):
    global failures
    print(f"{'PASS' if ok else 'FAIL'}: {name}" + (f" ({detail})" if detail and not ok else ""))
    if not ok:
        failures += 1


def avrdude(port, *args):
    cmd = [AVRDUDE, "-C", AVRDUDE_CONF, "-c", "oddprog", "-P", port, "-p", "89LP51ED2", *args]
    return subprocess.run(cmd, capture_output=True, text=True, timeout=300)


def main():
    import pty as ptymod
    master, slave = ptymod.openpty()
    port = os.ttyname(slave)
    chip = emu.AT89LP51ED2()
    bridge = emu.OddProg(chip)
    threading.Thread(target=emu.serve, args=(master, bridge), daemon=True).start()

    tmp = tempfile.mkdtemp(prefix="oddprog_e2e_")
    rnd = random.Random(89051)

    # Signature check happens on every invocation; run a bare lock read first
    r = avrdude(port, "-U", "lock:r:-:h")
    check("signature check and lock read", r.returncode == 0, r.stderr.strip()[-300:])

    # Flash: write (with automatic verify) an awkward, non page-aligned size
    img = bytes(rnd.randrange(256) for _ in range(3001))
    flash_img = os.path.join(tmp, "flash.bin")
    with open(flash_img, "wb") as fh:
        fh.write(img)
    r = avrdude(port, "-U", f"flash:w:{flash_img}:r")
    check("flash write + verify", r.returncode == 0, r.stderr.strip()[-300:])
    check("flash content in chip model", bytes(chip.flash[:len(img)]) == img)
    check("rest of flash untouched", all(b == 0xFF for b in chip.flash[len(img):]))

    flash_rd = os.path.join(tmp, "flash_rd.bin")
    r = avrdude(port, "-U", f"flash:r:{flash_rd}:r")
    with open(flash_rd, "rb") as fh:
        rd = fh.read()
    check("flash read back", r.returncode == 0 and rd[:len(img)] == img and
          all(b == 0xFF for b in rd[len(img):]), r.stderr.strip()[-300:])

    # EEPROM: 32-byte pages, byte-granular auto-erase
    ee = bytes(rnd.randrange(256) for _ in range(517))
    ee_img = os.path.join(tmp, "ee.bin")
    with open(ee_img, "wb") as fh:
        fh.write(ee)
    r = avrdude(port, "-U", f"eeprom:w:{ee_img}:r")
    check("eeprom write + verify", r.returncode == 0, r.stderr.strip()[-300:])
    check("eeprom content in chip model", bytes(chip.eeprom[:len(ee)]) == ee)

    # User signature: 128-byte pages written as two half pages
    us = bytes(rnd.randrange(256) for _ in range(200))
    us_img = os.path.join(tmp, "usersig.bin")
    with open(us_img, "wb") as fh:
        fh.write(us)
    r = avrdude(port, "-U", f"usersig:w:{us_img}:r")
    check("usersig write + verify", r.returncode == 0, r.stderr.strip()[-300:])
    check("usersig content in chip model", bytes(chip.usersig[:len(us)]) == us)

    # Fuse write goes through the read row/modify/auto-erase/rewrite path
    r = avrdude(port, "-T", "write fuse 4 0x00")
    ok = r.returncode == 0 and chip.fuse_row[4] == 0x00 and \
        all(chip.fuse_row[k] == 0xFF for k in range(19) if k != 4)
    check("single fuse write preserves fuse row", ok,
          (r.stderr + r.stdout).strip()[-300:] + f" row={chip.fuse_row[:8].hex()}")

    # Lock bits: byte write, no auto-erase
    r = avrdude(port, "-T", "write lock 1 0x00")
    check("lock bit write", r.returncode == 0 and chip.lock_bits[1] == 0x00,
          (r.stderr + r.stdout).strip()[-300:])

    # Chip erase clears flash, EEPROM and lock bits but not fuses
    r = avrdude(port, "-e")
    ok = r.returncode == 0 and all(b == 0xFF for b in chip.flash) and \
        all(b == 0xFF for b in chip.eeprom) and chip.lock_bits[1] == 0xFF and \
        chip.fuse_row[4] == 0x00
    check("chip erase", ok, r.stderr.strip()[-300:])

    print(f"\n{'ALL TESTS PASSED' if failures == 0 else f'{failures} TEST(S) FAILED'}")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
