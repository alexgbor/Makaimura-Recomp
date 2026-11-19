"""
Name:        analysis.py
Description: the sound program's code found from its entries, without traces: recursive descent through jumps,
             branches and calls, and through the jump tables RST $30 is followed by
Version:     0.1.0
Created:     19-11-2026

license:BSD-3-Clause
The sound program (the "audiocpu" region, 0000-7FFF) starts at 0000 (reset) and 0038 (the IRQ, interrupt mode 1).
Its RST $30 is a jump through the table that follows it: 0030 pops the return address (the table), RST $28 adds
A twice and loads the word there, and JP (HL) goes to it; so RST $30 does not come back, and its table's words
are code. A table ends at its first word that is not a plausible target (in the program, past the RST vectors,
on code that decodes), at the lowest target it holds past itself, or where known code begins. RST $18, $20 and
$28 are routines that come back. A return address the program pushes itself (LD HL,nn then PUSH HL, nn code)
is code too. Bytes FF (RST $38: the IRQ's vector, never called) are taken for fill: the flow
stops there.
"""
from dataclasses import dataclass, field
import os
import sys

TOOLS = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ROOT = os.path.dirname(TOOLS)
sys.path.insert(0, TOOLS)
sys.path.insert(0, os.path.join(TOOLS, "core"))

from z80 import disassembler as zd    # noqa: E402

ENTRIES = (0x0000, 0x0038)
TABLE_JUMP = 0x30                     # RST $30: a jump through the words that follow it
DISPATCH = 0x0033                     # its JP (HL)
END = 0x8000


@dataclass
class Program:
    data: bytes

    def read(self, address):
        address &= 0xFFFF
        return self.data[address] if address < len(self.data) else 0xFF

    def memory(self):
        return zd.FlatMemory(self.data + bytes(0x10000 - len(self.data)), 0xFFFF)


@dataclass
class Code:
    instructions: dict = field(default_factory=dict)
    tables: dict = field(default_factory=dict)       # RST $30's address -> its targets
    calls: set = field(default_factory=set)
    unresolved: list = field(default_factory=list)
    pushed: set = field(default_factory=set)         # code addresses the program pushes as return addresses


def load(paths=None):
    import makaimura_rom as rom
    regions = rom.load_regions(paths or rom.default_paths(ROOT))
    return Program(bytes(regions["audiocpu"][:END]))


def plausible(program, target):
    if not 0x0040 <= target < END or program.read(target) == 0xFF:
        return False
    instruction = zd.decode(program.memory(), target)
    return instruction.flow != zd.FLOW_INVALID


def read_table(program, table, code):
    targets, lowest = [], END
    place = table
    while place + 1 < lowest and place not in code.instructions and place + 1 not in code.instructions:
        target = program.read(place) | (program.read(place + 1) << 8)
        if not plausible(program, target):
            break
        if target > table:
            lowest = min(lowest, target)
        targets.append(target)
        place += 2
    return targets


def explore(program, extra=()):
    code = Code()
    memory = program.memory()
    work = list(ENTRIES) + list(extra)
    while work:
        address = work.pop()
        previous = None
        while address not in code.instructions and address < END:
            if program.read(address) == 0xFF and address not in (0x0038,):
                break
            instruction = zd.decode(memory, address)
            code.instructions[address] = instruction
            if instruction.flow == zd.FLOW_INVALID:
                break
            if instruction.mnemonic == "rst" and instruction.target == TABLE_JUMP:
                table = (address + 1) & 0xFFFF
                targets = read_table(program, table, code)
                code.tables[address] = targets
                work.extend(targets)
                work.append(TABLE_JUMP)
                break
            if instruction.target is not None and instruction.flow in (zd.FLOW_JUMP, zd.FLOW_BRANCH, zd.FLOW_CALL):
                if instruction.flow == zd.FLOW_CALL:
                    code.calls.add(instruction.target)
                work.append(instruction.target)
            if instruction.mnemonic == "jp" and instruction.target is None:
                if address != DISPATCH:
                    code.unresolved.append(instruction)
                break
            if instruction.flow in (zd.FLOW_JUMP, zd.FLOW_RETURN) and instruction.condition is None:
                break
            # LD HL,nn / PUSH HL with nn code: a return address pushed by hand (074D: a RET later goes there)
            pushes_hl = instruction.mnemonic == "push" and instruction.operands[0].reg == "hl"
            loaded_hl = previous is not None and previous.mnemonic == "ld" and previous.operands[0].mode == "reg" \
                and previous.operands[0].reg == "hl" and previous.operands[1].mode == "immediate"
            if pushes_hl and loaded_hl and plausible(program, previous.operands[1].value):
                code.pushed.add(previous.operands[1].value)
                work.append(previous.operands[1].value)
            previous = instruction
            address = (address + instruction.length) & 0xFFFF
    return code


if __name__ == "__main__":
    import argparse
    parser = argparse.ArgumentParser(description="the sound program's static analysis")
    parser.add_argument("--executed", help="gref --sound-executed's addresses, to check against")
    args = parser.parse_args()
    program = load()
    code = explore(program)
    print(f"instructions: {len(code.instructions)}, RST $30 tables: {len(code.tables)} "
          f"({sum(len(t) for t in code.tables.values())} targets), unresolved: {len(code.unresolved)}")
    if args.executed:
        ran = {int(line, 16) for line in open(args.executed) if line.strip()}
        missed = sorted(ran - set(code.instructions))
        print(f"executed by the reference: {len(ran)}; not found statically: {len(missed)}",
              " ".join(f"{a:04X}" for a in missed[:20]))
