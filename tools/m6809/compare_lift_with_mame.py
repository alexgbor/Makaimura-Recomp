"""
Name:        compare_lift_with_mame.py
Description: the lift (tools/m6809/lift.py) checked against MAME's MC6809 core: every instruction run on both
             from many states, and what they leave compared, register by register, flag by flag, and every bus
             cycle (its kind, address, value and cycle)
Version:     0.1.0
Created:     19-11-2026

license:BSD-3-Clause
    python tools/m6809/compare_lift_with_mame.py [--states N] [--all-opcodes] [--seed S]

The instructions are the program's (the static analysis: tools/m6809/analysis.py and symbols.txt), or with
--all-opcodes every opcode of the three pages, every indexed postbyte, random operands. MAME's side is
build/bin/oracle_m6809_step (tools/reference/oracle_m6809_step.cpp). The IRQ's entry is checked too. Not run
here: SYNC and CWAI (they wait for the interrupt: the runtime's), SWI*, the illegal opcodes.
"""
import argparse
import os
import random
import subprocess
import sys

TOOLS = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ROOT = os.path.dirname(TOOLS)
sys.path.insert(0, TOOLS)

from m6809 import disassembler as dis           # noqa: E402
from m6809 import interpreter, lift              # noqa: E402

ORACLE = os.path.join(ROOT, "build", "bin", "oracle_m6809_step" + (".exe" if os.name == "nt" else ""))
SKIP = {"SYNC", "CWAI", "SWI", "SWI2", "SWI3", "ILLEGAL"}


def program_instructions():
    import importlib.util
    import makaimura_rom as rom
    import symbols
    from m6809 import analysis
    spec = importlib.util.spec_from_file_location("disassemble", os.path.join(TOOLS, "disassemble.py"))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    program = rom.load_program(rom.default_paths(ROOT))
    syms = symbols.load(os.path.join(ROOT, "symbols.txt"))
    entries = module.vector_entries(program) + [(s.bank, s.address, s.name) for s in syms.code()]
    code = analysis.explore(program, entries, syms.tables(), syms.no_return())
    seen = {}
    for (bank, address), instruction in sorted(code.instructions.items(), key=lambda item: (item[0][0] or -1, item[0][1])):
        seen.setdefault(bytes(instruction.raw), instruction)
    return list(seen.values())


def every_opcode(rng):
    """One instruction of every opcode (every postbyte where indexed), at random addresses"""
    instructions = []
    prefixes = [(), (0x10,), (0x11,)]
    for prefix in prefixes:
        for opcode in range(256):
            if not prefix and opcode in (0x10, 0x11):
                continue
            postbytes = [None]
            probe = bytes(prefix) + bytes([opcode, 0x84, 0, 0, 0])
            first = dis.decode(lambda a: probe[a] if a < len(probe) else 0, 0)
            if first.mnemonic in SKIP:
                continue
            if first.operand and first.operand.mode == "indexed":
                postbytes = range(256)
            for post in postbytes:
                tail = [rng.randrange(256) for _ in range(4)]
                body = bytes(prefix) + bytes([opcode]) + (bytes([post]) if post is not None else b"") + bytes(tail)
                address = rng.randrange(0x0000, 0xFFF0)
                image = {address + i: b for i, b in enumerate(body)}
                instruction = dis.decode(lambda a: image.get(a, 0), address)
                if instruction.mnemonic in SKIP:
                    continue
                instructions.append(instruction)
    return instructions


def random_state(rng, instruction):
    regs = [rng.randrange(256), rng.randrange(256), rng.randrange(256),
            rng.randrange(0x10000), rng.randrange(0x10000), rng.randrange(0x10000), rng.randrange(0x10000)]
    # small pages and indexes now and then: the program's own kind of values
    if rng.random() < 0.3:
        regs[lift.DP] = 0
    cc = rng.randrange(256)
    memory = {}
    if instruction.mnemonic == "RTI":
        memory[regs[lift.S]] = rng.randrange(256) | 0x80
    for i, value in enumerate(instruction.raw):
        memory[(instruction.address + i) & 0xFFFF] = value
    return regs, cc, memory


def flags_of(cc):
    return {name: (cc >> number) & 1 for number, name in enumerate(lift.CC_FLAGS)}


def case_line(pc, regs, cc, memory, irq):
    a, b, dp, x, y, u, s = regs
    chunks = " ".join(f"{address:04X}:{value:02X}" for address, value in sorted(memory.items()))
    return f"{pc:04X} {a:02X} {b:02X} {dp:02X} {x:04X} {y:04X} {u:04X} {s:04X} {cc:02X} {int(irq)} {chunks}"


def parse_result(line):
    parts = line.split()
    state = [int(value, 16) for value in parts[:9]]
    cycles = int(parts[9])
    accesses = []
    for token in parts[10:]:
        kind, address, value, cycle = token.split(":")
        accesses.append((kind, int(address, 16), int(value, 16), int(cycle)))
    return state, cycles, accesses


def interpret(lifter, regs, cc, memory):
    machine = interpreter.Machine(regs, flags_of(cc), memory)
    pc = machine.run(lifter)
    r = machine.regs
    state = [pc, r[lift.A], r[lift.B], r[lift.DP], r[lift.X], r[lift.Y], r[lift.U], r[lift.S], machine.cc]
    return state, machine.cycles, machine.accesses


NAMES = ("PC", "A", "B", "DP", "X", "Y", "U", "S", "CC")


def describe(mame, ours):
    differences = []
    for name, a, b in zip(NAMES, mame[0], ours[0]):
        if a != b:
            differences.append(f"{name} MAME {a:04X} lift {b:04X}")
    if mame[1] != ours[1]:
        differences.append(f"cycles MAME {mame[1]} lift {ours[1]}")
    if mame[2] != ours[2]:
        for index, (a, b) in enumerate(zip(mame[2] + [None] * 40, ours[2] + [None] * 40)):
            if a != b:
                differences.append(f"bus cycle {index}: MAME {a} lift {b}")
                break
    return "; ".join(differences)


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[2])
    parser.add_argument("--states", type=int, default=20, help="states per instruction (default 20)")
    parser.add_argument("--all-opcodes", action="store_true", help="every opcode and postbyte, not the program's")
    parser.add_argument("--seed", type=int, default=6809)
    parser.add_argument("--show", type=int, default=12)
    args = parser.parse_args()
    rng = random.Random(args.seed)

    instructions = every_opcode(rng) if args.all_opcodes else program_instructions()
    instructions = [i for i in instructions if i.mnemonic not in SKIP]
    cases, lifters, unsupported = [], [], []
    for instruction in instructions:
        try:
            lifter = lift.lift(instruction)
        except lift.Unsupported as problem:
            unsupported.append((instruction, str(problem)))
            continue
        for _ in range(args.states):
            regs, cc, memory = random_state(rng, instruction)
            cases.append((instruction.address, regs, cc, memory, False))
            lifters.append(lifter)
    # the IRQ's entry, with I clear, at random places
    irq_cases = 200
    for _ in range(irq_cases):
        address = rng.randrange(0x10000)
        regs = [rng.randrange(256), rng.randrange(256), rng.randrange(256)] + [rng.randrange(0x10000) for _ in range(4)]
        cc = rng.randrange(256) & ~lift.CC_I
        cases.append((address, regs, cc, {address: 0x12}, True))
        lifters.append(lift.IrqEntry(address))

    text = "\n".join(case_line(pc, regs, cc, memory, irq) for pc, regs, cc, memory, irq in cases) + "\n"
    result = subprocess.run([ORACLE], input=text, capture_output=True, text=True, check=True)
    lines = result.stdout.splitlines()
    assert len(lines) == len(cases), f"{len(lines)} results for {len(cases)} cases"

    failures = {}
    same = 0
    for (pc, regs, cc, memory, irq), lifter, line in zip(cases, lifters, lines):
        mame = parse_result(line)
        ours = interpret(lifter, regs, cc, memory)
        if mame == ours:
            same += 1
            continue
        name = "IRQ entry" if irq else lifter.instruction.text
        key = name if irq else (lifter.instruction.mnemonic, lifter.instruction.operand.mode if lifter.instruction.operand else "")
        failures.setdefault(key, []).append((pc, name, describe(mame, ours)))
    kinds = len({lifter.instruction.raw if lifter.instruction else b"irq" for lifter in lifters})
    print(f"{len(instructions)} instructions ({kinds} forms with the IRQ entry), {len(cases)} cases: "
          f"{same} the same as MAME's core")
    if unsupported:
        print(f"not lifted: {len(unsupported)}")
        for instruction, problem in unsupported[:args.show]:
            print(f"  {instruction.address:04X}  {instruction.text:20s} {problem}")
    if failures:
        print(f"different: {sum(len(v) for v in failures.values())} cases, {len(failures)} kinds")
        for key, items in list(failures.items())[:args.show]:
            pc, name, difference = items[0]
            print(f"  {pc:04X}  {name:24s} ({len(items)}) {difference}")
    return 0 if not failures and not unsupported else 1


if __name__ == "__main__":
    sys.exit(main())
