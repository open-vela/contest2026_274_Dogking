#!/usr/bin/env python3
"""Dump the A733 R_PIO (bank L, the PL pins) registers from Linux.

Why: the same panel and the same wiring already work from Linux on this board,
so the Linux register values are the ground truth for what openvela must
reproduce.  PL5 is the ST7735 D/C line (gpiochip1 line 5 = global GPIO357 =
physical pin 22).

/dev/mem access needs root, and busybox/devmem2 are not installable here, so use
mmap directly:

    sudo python3 rpio_dump.py                 # read-only snapshot
    sudo python3 rpio_dump.py --dc high       # drive D/C high, then snapshot
    sudo python3 rpio_dump.py --dc low        # drive D/C low,  then snapshot

Compare the two snapshots: whichever register bit follows the level is the one
openvela has to write.

Reproduce the working Linux sequence first (it is what proves D/C toggling):
    pip3 install --break-system-packages spidev gpiod   # if not already present
"""
import argparse
import mmap
import os
import struct
import sys

R_PIO_BASE = 0x07025000
R_CCU_BASE = 0x07010000
BANK_STRIDE = 0x30          # sun60iw2 R_PIO is pinctrl hw_type 1
BANK_L = 11
GPIOCHIP1_BASE = 352        # gpiochip1 line N == global GPIO (352 + N)
DC_GLOBAL = 357             # as used by the working vision-4 test
PAGE = 4096


class Mem:
    def __init__(self, base, size=0x1000):
        self.base = base
        self.size = max(size, PAGE)
        fd = os.open("/dev/mem", os.O_RDWR | os.O_SYNC)
        self.mm = mmap.mmap(fd, self.size, mmap.MAP_SHARED,
                            mmap.PROT_READ | mmap.PROT_WRITE,
                            offset=base & ~(PAGE - 1))
        os.close(fd)
        self.skew = base & (PAGE - 1)

    def rd(self, off):
        # /dev/mem requires aligned access
        a = (self.skew + off) & ~3
        return struct.unpack_from("<I", self.mm, a)[0]

    def wr(self, off, val):
        a = (self.skew + off) & ~3
        struct.pack_into("<I", self.mm, a, val)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--dc", choices=["high", "low"], default=None,
                    help="drive the ST7735 D/C line (PL5) before dumping")
    ap.add_argument("--hold", type=float, default=0.0,
                    help="seconds to keep the level before dumping")
    args = ap.parse_args()

    if os.geteuid() != 0:
        sys.exit("run as root: sudo python3 rpio_dump.py")

    import time
    rpio = Mem(R_PIO_BASE)
    rccu = Mem(R_CCU_BASE)

    bank_l = BANK_L * BANK_STRIDE          # initial_bank_offset is 0 for hw_type 1

    if args.dc:
        line = DC_GLOBAL - GPIOCHIP1_BASE
        # mux -> gpio_out (function 1), 10 mA, no pull, then set the level
        cfg = bank_l + (line // 8) * 4
        drv = bank_l + 0x14 + (line // 8) * 4
        pul = bank_l + 0x24 + (line // 16) * 4
        sh = (line % 8) * 4
        psh = (line % 16) * 2

        v = rpio.rd(cfg)
        rpio.wr(cfg, (v & ~(0xF << sh)) | (1 << sh))
        v = rpio.rd(drv)
        rpio.wr(drv, (v & ~(0xF << sh)) | (1 << sh))
        v = rpio.rd(pul)
        rpio.wr(pul, v & ~(3 << psh))

        for off in (0x10, 0x14, 0x18):
            v = rpio.rd(bank_l + off)
            if args.dc == "high":
                rpio.wr(bank_l + off, v | (1 << line))
            else:
                rpio.wr(bank_l + off, v & ~(1 << line))
        print("drove PL5 %s (line %d)" % (args.dc, line))
        if args.hold:
            time.sleep(args.hold)

    print()
    print("R_PIO  base=0x%08x  bank L @ +0x%03x" % (R_PIO_BASE, bank_l))
    for off in range(0x00, 0x30, 4):
        print("  +0x%02x = 0x%08x" % (off, rpio.rd(bank_l + off)))

    print()
    print("R_CCU  base=0x%08x" % R_CCU_BASE)
    for off in (0x00, 0x0C, 0x10, 0x1C, 0x20):
        print("  +0x%02x = 0x%08x" % (off, rccu.rd(off)))

    print()
    print("PL5 mux field (bank L +0x00 bits 20..23) = 0x%x"
          % ((rpio.rd(bank_l + 0x00) >> 20) & 0xF))
    print("PL5 data bit  (bank L +0x10 bit 5)       = %d"
          % ((rpio.rd(bank_l + 0x10) >> 5) & 1))
    print("PL5 dset bit  (bank L +0x14 bit 5)       = %d"
          % ((rpio.rd(bank_l + 0x14) >> 5) & 1))
    print("PL5 dclr bit  (bank L +0x18 bit 5)       = %d"
          % ((rpio.rd(bank_l + 0x18) >> 5) & 1))


if __name__ == "__main__":
    main()
