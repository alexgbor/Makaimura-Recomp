#!/usr/bin/env python3
"""
Name:        coverage.py
Description: what of the programs the checks have run: the reference (gref) on the attract mode and every input
             script, its executed addresses against the static analyses (the 6809's and the Z80's), the code found
             and never run by routine, and a map of the program ROM (code run, code never run, tables, fill, the rest)
Version:     0.1.0
Created:     20-11-2026

    python tools/coverage.py [--accuracy mame|board] [--routines N]

The runs' addresses go to build/coverage/ (derived from the ROM: never committed). Every address a run executed
must be an instruction the analysis found (else the translation would stop there: m6809_fault); the code found and
never run is what no check has reached yet (the translation of it is unchecked against the reference).
"""
import argparse
import glob
import os
import subprocess
import sys

TOOLS = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(TOOLS)
sys.path.insert(0, TOOLS)

import makaimura_rom as rom                 # noqa: E402
from m6809 import analysis                  # noqa: E402
from z80cpu import analysis as z80analysis  # noqa: E402
import symbols                              # noqa: E402

GREF = os.path.join(ROOT, "build", "bin", "gref.exe" if os.name == "nt" else "gref")
OUT = os.path.join(ROOT, "build", "coverage")


VECTORS = (0xFFF2, 0xFFF4, 0xFFF6, 0xFFF8, 0xFFFA, 0xFFFC, 0xFFFE)


def vector_entries(program):
    """the vectors' targets (as tools/disassemble.py starts its analysis)"""
    entries = []
    for address in VECTORS:
        target = (program.fixed(address) << 8) | program.fixed(address + 1)
        if target != 0xFFFF:
            entries.append((None, target, f"vector {address:04X}"))
    return entries


def script_frames(path):
    """As check_translation.py runs a script: to its last change and 600 frames more"""
    last = 0
    with open(path) as handle:
        for line in handle:
            parts = line.split()
            if parts and parts[0].isdigit():
                last = max(last, int(parts[0]))
    return last + 600


def run_all(accuracy, reuse):
    """gref on the attract mode and each script: the 6809's and the Z80's executed addresses"""
    os.makedirs(OUT, exist_ok=True)
    runs = [("attract", None, 1200)]
    for path in sorted(glob.glob(os.path.join(ROOT, "tests", "controls", "*.txt"))):
        runs.append((os.path.splitext(os.path.basename(path))[0], path, script_frames(path)))
    ran, z80_ran, per_run = set(), set(), {}
    for name, script, frames in runs:
        executed = os.path.join(OUT, f"{name}-{accuracy}.executed")
        z80_executed = os.path.join(OUT, f"{name}-{accuracy}.z80")
        command = [GREF, "--accuracy", accuracy, "--frames", str(frames), "--executed", executed,
                   "--sound", os.devnull, "--sound-executed", z80_executed]
        if script:
            command += ["--inputs", script]
        if not (reuse and os.path.exists(executed) and os.path.exists(z80_executed)):
            subprocess.run(command, check=True, capture_output=True)
        mine = set()
        with open(executed) as handle:
            for line in handle:
                if line.strip():
                    mine.add(symbols.parse_address(line.strip())[::-1])
        with open(z80_executed) as handle:
            z80_ran |= {int(line, 16) for line in handle if line.strip()}
        per_run[name] = mine
        ran |= mine
        print(f"  {name}: {frames} frames, {len(mine)} 6809 instructions")
    return ran, z80_ran, per_run


def routines_of(code, names):
    """each instruction's routine: the nearest call target or named place before it (in its bank)"""
    starts = {k for k in code.calls} | {k for k in names if k in code.instructions}
    owner, current = {}, {}
    for k in sorted(code.instructions, key=lambda k: (-1 if k[0] is None else k[0], k[1])):
        if k in starts or k[0] not in current:
            current[k[0]] = k
        owner[k] = current[k[0]]
    return owner


def label(k, names):
    name = names.get(k) or names.get((None, k[1]))
    return (f"{k[0]}:" if k[0] is not None else "") + f"{k[1]:04X}" + (f" {name}" if name else "")


def rom_map(program, code, ran, syms):
    """by area (the fixed ROM, each bank's window): bytes of code run, code never run, tables, fill, the rest"""
    tables = {}
    for target_bank, address, count, name, bank in syms.tables():
        for offset in range(2 * count):
            tables[(bank, address + offset)] = True
    areas = [("6000-FFFF", None, 0x6000, 0x10000)] + [(f"bank {b} 4000-5FFF", b, 0x4000, 0x6000) for b in range(5)]
    lines = []
    for title, bank, start, end in areas:
        kind = {}
        for (b, address), ins in code.instructions.items():
            if b == bank and start <= address < end:
                for offset in range(len(ins.raw)):
                    kind[address + offset] = "run" if (b, address) in ran else "never"
        read = (lambda a: program.fixed(a)) if bank is None else (lambda a, b=bank: program.bank(b, a))
        counts = {"run": 0, "never": 0, "table": 0, "fill": 0, "other": 0}
        address = start
        while address < end:
            if address in kind:
                counts[kind[address]] += 1
                address += 1
                continue
            if (bank, address) in tables:
                counts["table"] += 1
                address += 1
                continue
            # fill: a run of 16 or more of the same byte (FF, 00...)
            run = address
            while run < end and run not in kind and read(run) == read(address):
                run += 1
            if run - address >= 16:
                counts["fill"] += run - address
                address = run
            else:
                counts["other"] += 1
                address += 1
        size = end - start
        lines.append(f"  {title}: code run {counts['run']} ({100 * counts['run'] / size:.1f}%), code never run "
                     f"{counts['never']} ({100 * counts['never'] / size:.1f}%), tables {counts['table']}, fill "
                     f"{counts['fill']}, the rest (data) {counts['other']} ({100 * counts['other'] / size:.1f}%)")
    return lines


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[2])
    parser.add_argument("--accuracy", choices=("mame", "board"), default="mame")
    parser.add_argument("--reuse", action="store_true", help="the runs' addresses from build/coverage/ if there")
    parser.add_argument("--routines", type=int, default=40, help="how many routines never run (or partly) to list")
    args = parser.parse_args()

    program = rom.load_program(rom.default_paths(ROOT))
    syms = symbols.load(os.path.join(ROOT, "symbols.txt"))
    entries = vector_entries(program) + [(s.bank, s.address, s.name) for s in syms.code()]
    code = analysis.explore(program, entries, syms.tables(), syms.no_return(), syms.ram_code(program))
    names = syms.names()

    print(f"the reference's runs ({args.accuracy} mode):")
    ran, z80_ran, per_run = run_all(args.accuracy, args.reuse)
    ran = {analysis.key(b, a) for b, a in ran}

    found = set(code.instructions)
    in_ram = {k for k in ran if k[1] < 0x4000}
    missed = sorted((k for k in ran - found if k[1] >= 0x4000), key=lambda k: (-1 if k[0] is None else k[0], k[1]))
    print(f"\n6809: {len(found)} instructions found, {len(ran & found)} run ({100 * len(ran & found) / len(found):.1f}%)"
          f"; run outside the ROM (the RAM overlay): {len(in_ram)}; run and not found: {len(missed)}")
    for k in missed[:20]:
        print(f"  NOT FOUND: {label(k, names)}")

    owner = routines_of(code, names)
    sizes = {}
    for k, ins in code.instructions.items():
        r = owner[k]
        total, never = sizes.get(r, (0, 0))
        sizes[r] = (total + len(ins.raw), never + (0 if k in ran else len(ins.raw)))
    order = lambda item: (-item[0], -1 if item[1][0] is None else item[1][0], item[1][1])    # noqa: E731
    untouched = sorted(((never, r) for r, (total, never) in sizes.items() if never == total and never), key=order)
    partly = sorted(((never, r) for r, (total, never) in sizes.items() if 0 < never < total), key=order)
    print(f"routines: {len(sizes)}; never run: {len(untouched)} ({sum(n for n, _ in untouched)} bytes); "
          f"partly run: {len(partly)} ({sum(n for n, _ in partly)} bytes not run)")
    print(f"the largest never run:")
    for never, r in untouched[:args.routines]:
        print(f"  {label(r, names)}: {never} bytes")
    print(f"the most not run of those partly run:")
    for never, r in partly[:args.routines // 2]:
        print(f"  {label(r, names)}: {never} of {sizes[r][0]} bytes")

    print("\nthe program ROM:")
    for line in rom_map(program, code, ran, syms):
        print(line)

    z80 = z80analysis.explore(z80analysis.load())
    z80_found = set(z80.instructions)
    z80_missed = sorted(z80_ran - z80_found)
    print(f"\nZ80: {len(z80_found)} instructions found, {len(z80_ran & z80_found)} run "
          f"({100 * len(z80_ran & z80_found) / len(z80_found):.1f}%); run and not found: {len(z80_missed)}")
    for address in z80_missed[:20]:
        print(f"  NOT FOUND: {address:04X}")
    return 1 if missed or z80_missed else 0


if __name__ == "__main__":
    sys.exit(main())
