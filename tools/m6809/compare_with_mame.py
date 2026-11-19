"""
Name:        compare_with_mame.py
Description: tools/m6809/disassembler.py checked against MAME's 6809 disassembler (build/bin/oracle_m6809) at
             every address of the program ROM (6000-FFFF, and 4000-5FFF in each of the five banks), and of an
             image of every opcode with every postbyte
Version:     0.1.0
Created:     19-11-2026

    python tools/m6809/compare_with_mame.py [--oracle PATH] [--show N]

Each address's length and text must be MAME's. Opcodes the 6809 does not document (MAME's "undocumented"
ones: 01, 02, 05, 0B, 14, 15, 18, 1B, 38, 3E, 41..., and page 2/3 opcodes that fall through to page 1) are
ILLEGAL for this disassembler: they are counted apart, and the program must not reach any of them (the
analysis reports any it reaches).
"""
import argparse
import os
import subprocess
import sys
import tempfile

TOOLS = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ROOT = os.path.dirname(TOOLS)
sys.path.insert(0, TOOLS)

import makaimura_rom as rom                     # noqa: E402
from m6809 import disassembler as dis           # noqa: E402

EXE = ".exe" if os.name == "nt" else ""


def oracle(path, image, first, last):
    with tempfile.NamedTemporaryFile(delete=False, suffix=".bin") as handle:
        handle.write(bytes(image))
        name = handle.name
    try:
        output = subprocess.run([path, name, f"{first:X}", f"{last:X}"], capture_output=True, text=True,
                                check=True).stdout
    finally:
        os.unlink(name)
    result = {}
    for line in output.splitlines():
        address, length, text = line.split(" ", 2)
        result[int(address, 16)] = (int(length, 16), text.rstrip())
    return result


def compare(path, image, first, last, label, show):
    reference = oracle(path, image, first, last)
    read = lambda address: image[address & 0xFFFF]    # noqa: E731
    same = differ = illegal = 0
    shown = 0
    for address in range(first, last + 1):
        mame_length, mame_text = reference[address]
        ins = dis.decode(read, address)
        if ins.flow == "illegal":
            illegal += 1
            continue
        if ins.size == mame_length and ins.text == mame_text:
            same += 1
            continue
        differ += 1
        if shown < show:
            shown += 1
            print(f"  {label} {address:04X}  {bytes(image[address:address + 5]).hex()}  "
                  f"mine: {ins.size} {ins.text!r}   MAME: {mame_length} {mame_text!r}")
    print(f"{label}: {same} the same, {differ} different, {illegal} undocumented or illegal (left out)")
    return differ


def opcode_image():
    """Every opcode of the three pages, each followed by every postbyte in turn, laid out 8 bytes apart."""
    image = bytearray([0x12] * 0x10000)
    address = 0
    for page in (None, 0x10, 0x11):
        for opcode in range(0x100):
            for post in range(0x100) if page is None and opcode in (0x30, 0x34, 0x1E, 0xA6) else (0x9F, 0x84):
                if address + 8 > 0x10000:
                    return image, address - 1
                code = ([page] if page is not None else []) + [opcode, post, 0x12, 0x34, 0x56]
                image[address:address + len(code)] = bytes(code)
                address += 8
    return image, address - 1


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[2])
    parser.add_argument("--oracle", default=os.path.join(ROOT, "build", "bin", "oracle_m6809" + EXE))
    parser.add_argument("--show", type=int, default=20, help="how many differences to print")
    args = parser.parse_args()

    program = rom.load_program(rom.default_paths(ROOT))
    total = 0
    base = bytearray(0x10000)
    base[0x6000:0x10000] = program.region[0x6000:0x10000]
    total += compare(args.oracle, base, 0x6000, 0xFFFF, "fixed 6000-FFFF", args.show)
    for bank in range(rom.BANKS):
        image = bytearray(base)
        for address in range(0x4000, 0x6000):
            image[address] = program.bank(bank, address)
        total += compare(args.oracle, image, 0x4000, 0x5FFF, f"bank {bank} 4000-5FFF", args.show)
    image, last = opcode_image()
    total += compare(args.oracle, image, 0, last, "every opcode", args.show)
    print("the same as MAME's" if total == 0 else f"{total} differences")
    return 1 if total else 0


if __name__ == "__main__":
    sys.exit(main())
