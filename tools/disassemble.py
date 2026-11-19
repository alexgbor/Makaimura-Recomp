"""
Name:        disassemble.py
Description: the 6809 program's static analysis from its vectors and symbols.txt, and its listing
             (build/makaimurg.lst: derived from the ROM, so never committed)
Version:     0.1.0
Created:     19-11-2026

    python tools/disassemble.py              the summary: code found, what is left unresolved
    python tools/disassemble.py --listing    and the listing, build/makaimurg.lst
"""
import argparse
import os
import sys

TOOLS = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(TOOLS)
sys.path.insert(0, TOOLS)

import makaimura_rom as rom                      # noqa: E402
from m6809 import analysis, disassembler as dis  # noqa: E402
import symbols                                   # noqa: E402

VECTORS = (("SWI3", 0xFFF2), ("SWI2", 0xFFF4), ("FIRQ", 0xFFF6), ("IRQ", 0xFFF8), ("SWI", 0xFFFA),
           ("NMI", 0xFFFC), ("RESET", 0xFFFE))


def vector_entries(program):
    entries = []
    for name, address in VECTORS:
        target = (program.fixed(address) << 8) | program.fixed(address + 1)
        if target != 0xFFFF:
            entries.append((None, target, f"vector {name}"))
    return entries


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[2])
    parser.add_argument("--rom", nargs="*", help="the ROM set's .zips or folder (default: rom/)")
    parser.add_argument("--listing", action="store_true", help="write build/makaimurg.lst")
    parser.add_argument("--unresolved", action="store_true", help="each unresolved jump with what leads to it")
    parser.add_argument("--executed", help="the reference's executed addresses (gref --executed) to check against")
    parser.add_argument("--show", type=int, default=40, help="how many missed stretches to list")
    args = parser.parse_args()

    program = rom.load_program(args.rom or rom.default_paths(ROOT))
    syms = symbols.load(os.path.join(ROOT, "symbols.txt"))
    entries = vector_entries(program) + [(s.bank, s.address, s.name) for s in syms.code()]
    code = analysis.explore(program, entries, syms.tables(), syms.no_return())

    covered = analysis.coverage(code)
    print(f"instructions: {len(code.instructions)}, routines called: {len(code.calls)}, "
          f"fixed ROM 6000-FFFF covered: {sum(covered)} of {len(covered)} bytes ({100 * sum(covered) / len(covered):.1f}%)")
    banked = sorted({k for k in code.instructions if k[0] is not None})
    print(f"banked code (4000-5FFF): {len(banked)} instructions" +
          (f", banks {sorted({b for b, _ in banked})}" if banked else ""))
    print(f"unresolved jumps and calls through registers or memory: {len(code.unresolved)}")
    if args.unresolved:
        for bank, ins in sorted(code.unresolved, key=lambda item: item[1].address):
            print(f"  {ins.address:04X}  {ins.text}")
    print(f"flow out of the ROM: {len(code.outside)}")
    for bank, ins, target in code.outside[:20]:
        print(f"  {(ins.address if ins else 0):04X} -> {target:04X}  {ins.text if ins else ''}")
    print(f"illegal opcodes reached: {len(code.illegal)}")
    for bank, ins in code.illegal[:20]:
        print(f"  {ins.address:04X}  {ins.raw.hex()}  (from {code.entries.get(analysis.key(bank, ins.address), '?')})")

    if args.executed:
        # the reference's executed addresses (gref --executed): each must be an instruction the analysis found
        ran = set()
        with open(args.executed) as handle:
            for line in handle:
                text = line.strip()
                if text:
                    address, bank = symbols.parse_address(text)
                    ran.add((bank, address))
        found = set(code.instructions)
        order = lambda k: (-1 if k[0] is None else k[0], k[1])    # noqa: E731
        missed = sorted((k for k in ran if k not in found and k[1] >= 0x4000), key=order)
        in_ram = sorted((k for k in ran if k[1] < 0x4000), key=order)
        print(f"executed by the reference: {len(ran)}; in RAM: {len(in_ram)}"
              f" ({in_ram[0][1]:04X}-{in_ram[-1][1]:04X})" if in_ram else f"executed by the reference: {len(ran)}")
        print(f"executed but not found statically: {len(missed)}")
        # the starts of the missed stretches, with the instruction before them that the analysis did find
        starts = [k for k in missed if (k[0], k[1] - 1) not in ran and all((k[0], k[1] - n) not in ran
                                                                            for n in range(1, 5))]
        for bank, address in starts[:args.show]:
            print(f"  {'' if bank is None else f'{bank}:'}{address:04X}")
        if len(starts) > args.show:
            print(f"  ... {len(starts) - args.show} more")

    if args.listing:
        os.makedirs(os.path.join(ROOT, "build"), exist_ok=True)
        path = os.path.join(ROOT, "build", f"{rom.SET}.lst")
        names = syms.names()
        with open(path, "w", encoding="utf-8") as out:
            for (bank, address), ins in sorted(code.instructions.items(), key=lambda kv: (kv[0][0] or -1, kv[0][1])):
                label = names.get((bank, address)) or names.get((None, address))
                if analysis.key(bank, address) in code.calls or label:
                    out.write(f"\n{label or f'sub_{address:04X}'}:\n")
                prefix = f"{bank}:" if bank is not None else "  "
                out.write(f"{prefix}{address:04X}  {ins.raw.hex():<10} {ins.text}\n")
        print(f"listing: {path}")


if __name__ == "__main__":
    main()
