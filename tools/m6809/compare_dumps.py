"""
Name:        compare_dumps.py
Description: two runs' bus at each IRQ (--irq-dumps of gref, gng_headless or gng_headless_c) compared byte by byte,
             the stack's bytes left out (gng/board.h: gng_stack_byte): where the decompiled program first parts from
             the reference
Version:     0.1.0
Created:     20-11-2026

license:BSD-3-Clause
    python tools/m6809/compare_dumps.py REFERENCE.bin OTHER.bin [--all]
"""
import argparse
import sys

SIZE = 0x3000


def stack_byte(address):
    return 0x02C0 <= address < 0x0300 or address in (0x00E2, 0x00E3)


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[2])
    parser.add_argument("reference")
    parser.add_argument("other")
    parser.add_argument("--all", action="store_true", help="every IRQ that differs, not only the first")
    args = parser.parse_args()
    with open(args.reference, "rb") as a, open(args.other, "rb") as b:
        index = 0
        found = 0
        while True:
            x, y = a.read(SIZE), b.read(SIZE)
            if len(x) < SIZE or len(y) < SIZE:
                break
            different = [address for address in range(SIZE) if x[address] != y[address] and not stack_byte(address)]
            if different:
                found += 1
                shown = ", ".join(f"{address:04X}: {x[address]:02X}/{y[address]:02X}" for address in different[:24])
                print(f"IRQ {index}: {len(different)} bytes differ (reference/other): {shown}")
                if not args.all:
                    return 1
            index += 1
    print(f"{index} IRQs compared, {found} differ")
    return 1 if found else 0


if __name__ == "__main__":
    sys.exit(main())
