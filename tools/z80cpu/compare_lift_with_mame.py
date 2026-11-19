"""
Name:        compare_lift_with_mame.py
Description: the sound lift (tools/z80cpu/lift.py) checked against MAME 0.289's Z80 core: every instruction of the
             sound program run on both from many states, the registers, the flags the program can see, IFF1, IFF2,
             IM, the cycles and every bus cycle (kind, address, value, cycle) compared
Version:     0.1.0
Created:     19-11-2026

license:BSD-3-Clause
    python tools/z80cpu/compare_lift_with_mame.py [--states N] [--seed S]

The instructions are the sound program's (tools/z80cpu/analysis.py); MAME's side is build/bin/oracle_z80_step. F and
F' are compared on the bits the lift keeps (S Z H P/V N C: lift.F_SEEN); WZ is not compared. The IRQ's entry is
checked too. HALT is not run here (it waits: the runtime's).
"""
import argparse
import os
import random
import subprocess
import sys

TOOLS = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ROOT = os.path.dirname(TOOLS)
sys.path.insert(0, TOOLS)

from z80cpu import analysis, interpreter, lift    # noqa: E402

ORACLE = os.path.join(ROOT, "build", "bin", "oracle_z80_step" + (".exe" if os.name == "nt" else ""))
ORDER = (lift.BC, lift.DE, lift.HL, lift.IX, lift.IY)


def flags_of(f, suffix=""):
    return {name + suffix: (f >> lift.F_BITS[name]) & 1 for name in lift.FLAGS}


def f_of(flags, suffix=""):
    return sum(flags[name + suffix] << lift.F_BITS[name] for name in lift.FLAGS)


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[2])
    parser.add_argument("--states", type=int, default=20)
    parser.add_argument("--seed", type=int, default=80)
    parser.add_argument("--show", type=int, default=12)
    args = parser.parse_args()
    rng = random.Random(args.seed)
    program = analysis.load()
    code = analysis.explore(program)
    cases, lifters, unsupported = [], [], []
    for address in sorted(code.instructions):
        instruction = code.instructions[address]
        if instruction.mnemonic == "halt":
            continue
        try:
            lifter = lift.lift(instruction, program.read)
        except lift.Unsupported as problem:
            unsupported.append((instruction, str(problem)))
            continue
        raw = bytes(program.read(address + i) for i in range(instruction.length))
        for _ in range(args.states):
            regs = [rng.randrange(0x10000) for _ in range(6)] + [rng.randrange(256)] + \
                [rng.randrange(0x10000) for _ in range(3)] + [rng.randrange(256)]
            f, f2 = rng.randrange(256), rng.randrange(256)
            iff = rng.randrange(2)
            cases.append((address, regs, f, f2, iff, {address + i: b for i, b in enumerate(raw)}, False))
            lifters.append(lifter)
    for _ in range(200):
        address = rng.randrange(0x10000)
        regs = [rng.randrange(0x10000) for _ in range(6)] + [rng.randrange(256)] + \
            [rng.randrange(0x10000) for _ in range(3)] + [rng.randrange(256)]
        cases.append((address, regs, rng.randrange(256), rng.randrange(256), 1, {address: 0}, True))
        lifters.append(lift.IrqEntry(address))

    def line(case):
        address, r, f, f2, iff, memory, irq = case
        af, af2 = (r[lift.A] << 8) | f, (r[lift.A2] << 8) | f2
        values = [address, r[lift.SP], af, r[lift.BC], r[lift.DE], r[lift.HL], r[lift.IX], r[lift.IY], af2,
                  r[lift.BC2], r[lift.DE2], r[lift.HL2], 0, 0, iff, iff, 1, int(irq)]
        chunks = " ".join(f"{a & 0xFFFF:04X}:{v:02X}" for a, v in sorted(memory.items()))
        return " ".join(f"{v:X}" for v in values) + " " + chunks

    result = subprocess.run([ORACLE], input="\n".join(line(c) for c in cases) + "\n", capture_output=True,
                            text=True, check=True)
    outputs = result.stdout.splitlines()
    assert len(outputs) == len(cases)
    failures, same = {}, 0
    for case, lifter, output in zip(cases, lifters, outputs):
        address, r, f, f2, iff, memory, irq = case
        parts = output.split()
        v = [int(x, 16) for x in parts[:14]] + [int(x) for x in parts[14:17]]
        mame = ([v[0], v[1], v[2] >> 8, v[2] & lift.F_SEEN, v[3], v[4], v[5], v[6], v[7], v[8] >> 8,
                 v[8] & lift.F_SEEN, v[9], v[10], v[11], v[14], v[15], v[16]], int(parts[17]),
                [(t.split(":")[0], int(t.split(":")[1], 16), int(t.split(":")[2], 16), int(t.split(":")[3]))
                 for t in parts[18:]])
        flags = flags_of(f)
        flags.update(flags_of(f2, "_alt"))
        machine = interpreter.Machine(r, flags, memory, (iff, iff), 1)
        pc = machine.run(lifter)
        m = machine.regs
        ours = ([pc, m[lift.SP], m[lift.A], f_of(machine.flags), m[lift.BC], m[lift.DE], m[lift.HL], m[lift.IX],
                 m[lift.IY], m[lift.A2], f_of(machine.flags, "_alt"), m[lift.BC2], m[lift.DE2], m[lift.HL2],
                 machine.iff1, machine.iff2, machine.im], machine.cycles, machine.accesses)
        if mame == ours:
            same += 1
            continue
        names = ("PC", "SP", "A", "F", "BC", "DE", "HL", "IX", "IY", "A'", "F'", "BC'", "DE'", "HL'", "IFF1",
                 "IFF2", "IM")
        difference = [f"{n} MAME {a:X} lift {b:X}" for n, a, b in zip(names, mame[0], ours[0]) if a != b]
        if mame[1] != ours[1]:
            difference.append(f"cycles MAME {mame[1]} lift {ours[1]}")
        if mame[2] != ours[2]:
            difference.append(f"bus MAME {mame[2]} lift {ours[2]}")
        name = "IRQ entry" if irq else lifter.instruction.text
        failures.setdefault(name.split()[0] + " " + ",".join(o.mode for o in (lifter.instruction.operands if lifter.instruction else [])),
                            []).append((address, name, "; ".join(difference)))
    print(f"{len(code.instructions)} instructions, {len(cases)} cases: {same} the same as MAME's core")
    if unsupported:
        print(f"not lifted: {len(unsupported)}")
        for instruction, problem in unsupported[:args.show]:
            print(f"  {instruction.address:04X}  {instruction.text:24s} {problem}")
    if failures:
        print(f"different: {sum(len(v) for v in failures.values())} cases, {len(failures)} kinds")
        for key, items in list(failures.items())[:args.show]:
            address, name, difference = items[0]
            print(f"  {address:04X}  {name:22s} ({len(items)}) {difference[:300]}")
    return 0 if not failures and not unsupported else 1


if __name__ == "__main__":
    sys.exit(main())
