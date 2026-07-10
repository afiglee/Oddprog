#!/usr/bin/env python3
"""OddProg firmware emulator with a simulated AT89LP51ED2 target.

Emulates the OddProg SLIP packet protocol (see defs.h/protocol.c/prog.c) on a
pseudo terminal so the avrdude 'oddprog' driver can be tested without
hardware. The simulated target implements the ISP command set of the
AT89LP51RD2/ED2/ID2 datasheet Table 23-21, including half-page write limits,
byte-address rollover within a half page and page auto-erase, so incorrect
programming sequences corrupt data just like on silicon (flash writes can
only clear bits unless the page is erased first).

Usage: oddprog_emulator.py [--dump PREFIX]
Prints the pty device path on the first line of stdout, then serves forever.
"""

import os
import pty
import sys
import argparse

# SLIP (must match defs.h)
SLIP_END = 0xC0
SLIP_ESC = 0xDB
SLIP_ESC_END = 0xDC
SLIP_ESC_ESC = 0xDD

# Packet cmd flags (must match defs.h)
CMD_NEW_PACKET = 0x80
CMD_LAST_PACKET = 0x40
CMD_WRITE_DIRECTION = 0x20
CMD_DATASIZE_256 = 0x10
CMD_DATASIZE = 0x08
CMD_OPTIONS = 0x04
CMD_INSTR_SIZE_MASK = 0x03

OPTION_USE_SS = 0x01

ERROR_OK = 0x00
ERROR_PACKET_CORRUPTED = 0xC0
ERROR_PACKET_CHECKSUM = 0xCE
ERROR_PACKET_ESC_ERROR = 0xEC

BUFFER_SIZE = 0x40

# Status register (Table 23-22): LOAD | SUCCESS | WRTINH | BUSY(=1 when ready)
STATUS_READY = 0x0F


class AT89LP51ED2:
    """SPI-level model of the target chip."""

    def __init__(self):
        self.flash = bytearray(b"\xFF" * 65536)
        self.eeprom = bytearray(b"\xFF" * 4096)
        self.usersig = bytearray(b"\xFF" * 512)
        self.atmel_sig = bytearray(b"\xFF" * 128)
        self.atmel_sig[0:3] = bytes([0x1E, 0x64, 0x65])  # AT89LP51ED2 Device ID
        self.atmel_sig[0x30:0x32] = bytes([0x58, 0xD7])
        self.atmel_sig[0x60:0x62] = bytes([0xEC, 0xEF])
        self.fuse_row = bytearray(b"\xFF" * 64)
        self.lock_bits = bytearray(b"\xFF" * 64)
        self.frame = None  # bytes seen on MOSI while SS is low, None = SS high
        self.prog_enabled = False

    READ_MEMS = {
        0x30: ("flash", 0xFFFF),
        0xB0: ("eeprom", 0x0FFF),
        0x32: ("usersig", 0x01FF),
        0x38: ("atmel_sig", 0x007F),
        0x61: ("fuse_row", 0x003F),
        0x64: ("lock_bits", 0x003F),
    }
    # opcode: (memory, address mask, page size erased by auto-erase or None)
    WRITE_MEMS = {
        0x50: ("flash", 0xFFFF, None),
        0x70: ("flash", 0xFFFF, 128),
        0xD0: ("eeprom", 0x0FFF, None),
        0xD2: ("eeprom", 0x0FFF, "bytes"),  # EEPROM auto-erase is byte granular
        0x52: ("usersig", 0x01FF, None),
        0x72: ("usersig", 0x01FF, 128),
        0xE1: ("fuse_row", 0x003F, None),
        0xF1: ("fuse_row", 0x003F, 64),
        0xE4: ("lock_bits", 0x003F, None),
    }

    @staticmethod
    def roll(base, idx):
        # The byte address rolls over within the addressed 64-byte half page
        return (base & ~0x3F) | ((base + idx) & 0x3F)

    def ss_low(self):
        if self.frame is None:
            self.frame = []

    def exchange(self, mosi):
        """Full duplex byte exchange; returns the MISO byte."""
        if self.frame is None:  # Tolerate operation without SS
            self.frame = []
        f = self.frame
        miso = 0x00
        # Stream: preamble1 preamble2 opcode addr_hi addr_lo data...
        if len(f) >= 5 and f[0] == 0xAA and f[1] == 0x55:
            op, base = f[2], (f[3] << 8) | f[4]
            idx = len(f) - 5
            if op == 0x60:  # Read Status
                miso = STATUS_READY
            elif op in self.READ_MEMS:
                name, mask = self.READ_MEMS[op]
                miso = getattr(self, name)[self.roll(base, idx) & mask]
        f.append(mosi)
        return miso

    def ss_high(self):
        f, self.frame = self.frame, None
        if not f or len(f) < 3 or f[0] != 0xAA or f[1] != 0x55:
            return
        op = f[2]
        if op == 0xAC and len(f) >= 4:
            if f[3] == 0x53:
                self.prog_enabled = True
            return
        if op == 0x8A:  # Chip Erase: code, data and lock bits; fuses survive
            self.flash[:] = b"\xFF" * len(self.flash)
            self.eeprom[:] = b"\xFF" * len(self.eeprom)
            self.lock_bits[:] = b"\xFF" * len(self.lock_bits)
            return
        if op not in self.WRITE_MEMS or len(f) < 5:
            return
        name, mask, erase = self.WRITE_MEMS[op]
        mem = getattr(self, name)
        base, data = (f[3] << 8) | f[4], f[5:]
        if erase == 64 or erase == 128:  # Auto-erase wipes the whole page
            page = base & mask & ~(erase - 1)
            mem[page:page + erase] = b"\xFF" * erase
        for kk, value in enumerate(data):
            addr = self.roll(base, kk) & mask
            if erase == "bytes":
                mem[addr] = value  # Byte-granular erase/write
            else:
                mem[addr] &= value  # Flash programming can only clear bits

    def dump(self, prefix):
        for name in ("flash", "eeprom", "usersig", "fuse_row", "lock_bits"):
            with open(f"{prefix}{name}.bin", "wb") as fh:
                fh.write(getattr(self, name))


class OddProg:
    """Mirror of the firmware packet engine (protocol.c + prog.c)."""

    def __init__(self, chip):
        self.chip = chip
        self.options = 0
        self.bytes_left = 0

    def response(self, status, data=b""):
        pkt = bytearray([len(data) + 2, 0, status]) + bytearray(data)
        pkt[1] = (0 - sum(pkt[2:])) & 0xFF
        out = bytearray([SLIP_END])
        for b in pkt:
            if b == SLIP_END:
                out += bytes([SLIP_ESC, SLIP_ESC_END])
            elif b == SLIP_ESC:
                out += bytes([SLIP_ESC, SLIP_ESC_ESC])
            else:
                out.append(b)
        out.append(SLIP_END)
        return bytes(out)

    def on_packet(self, pkt):
        if len(pkt) < 3:
            return self.response(ERROR_PACKET_CORRUPTED)
        size = pkt[0]
        if size > BUFFER_SIZE - 1 or size < 2 or size != len(pkt) - 1:
            return self.response(ERROR_PACKET_CORRUPTED)
        if sum(pkt[1:]) & 0xFF:
            return self.response(ERROR_PACKET_CHECKSUM)
        cmd = pkt[2]
        data = bytearray(pkt[3:])
        if cmd & CMD_OPTIONS:
            self.options = data[1]
            return self.response(ERROR_OK)

        pointer = 0
        if cmd & CMD_NEW_PACKET:
            if cmd & CMD_DATASIZE_256:
                self.bytes_left = 256
            elif cmd & CMD_DATASIZE:
                self.bytes_left = data[0]
            else:
                self.bytes_left = 0
            if self.options & OPTION_USE_SS:
                self.chip.ss_low()
            instr_size = (cmd & CMD_INSTR_SIZE_MASK) + 1
            pointer = 1
            for _ in range(instr_size):
                self.chip.exchange(data[pointer])
                pointer += 1
        while pointer < len(data) and self.bytes_left:
            miso = self.chip.exchange(data[pointer])
            if not cmd & CMD_WRITE_DIRECTION:
                data[pointer] = miso
            pointer += 1
            self.bytes_left -= 1
        if self.options & OPTION_USE_SS and cmd & CMD_LAST_PACKET:
            self.chip.ss_high()

        if not cmd & CMD_WRITE_DIRECTION:
            return self.response(ERROR_OK, data)
        return self.response(ERROR_OK)


def serve(fd, oddprog, dump_prefix=None):
    packet = bytearray()
    receiving = False
    esc = False
    while True:
        try:
            raw = os.read(fd, 4096)
        except OSError:
            break
        if not raw:
            break
        for b in raw:
            if b == SLIP_END:
                if receiving and packet:
                    os.write(fd, oddprog.on_packet(bytes(packet)))
                    if dump_prefix:
                        oddprog.chip.dump(dump_prefix)
                    packet.clear()
                    receiving = False
                else:
                    receiving = True
                    packet.clear()
                esc = False
                continue
            if not receiving:
                continue
            if esc:
                esc = False
                if b == SLIP_ESC_END:
                    packet.append(SLIP_END)
                elif b == SLIP_ESC_ESC:
                    packet.append(SLIP_ESC)
                else:
                    os.write(fd, oddprog.response(ERROR_PACKET_ESC_ERROR))
                    receiving = False
                    packet.clear()
                continue
            if b == SLIP_ESC:
                esc = True
                continue
            packet.append(b)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--dump", metavar="PREFIX",
                        help="dump chip memories to PREFIX<mem>.bin after every packet")
    args = parser.parse_args()

    master, slave = pty.openpty()
    print(os.ttyname(slave), flush=True)
    serve(master, OddProg(AT89LP51ED2()), args.dump)


if __name__ == "__main__":
    main()
