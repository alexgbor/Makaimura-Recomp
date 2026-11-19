"""
Name:        analysis.py
Description: the 6809 program's code found from its vectors and symbols, without traces: recursive descent
             through branches, jumps and calls, following the bank the 3E00 latch selects, with what is left
             unresolved (jumps through registers and memory) listed for the symbols to describe
Version:     0.1.0
Created:     19-11-2026

The code is keyed by (bank, address): an address in 6000-FFFF is the same in every bank (bank None), one in
4000-5FFF needs the bank the latch selects. Each path carries the bank it runs with (None: not known) and the
constants A and B hold, so that "LDB #$03 / STB $3E00" switches the path to bank 3. A call keeps the caller's
bank on its return (the program's routines that switch banks put them back; the reference's traces check it).
Jump tables the symbols describe (symbols.txt: "table" entries) add their entries as code.
"""
from dataclasses import dataclass, field

from . import disassembler as dis

FIXED_START = 0x6000
BANK_LATCH = 0x3E00

# instructions that leave A and B as they are (the rest make them unknown)
KEEPS_AB_PREFIXES = ("ST", "CMP", "BIT", "LEA", "LDX", "LDY", "LDU", "LDS", "PSH", "ORCC", "ANDCC", "NOP")
MEMORY_RMW = ("NEG", "COM", "LSR", "ROR", "ASR", "ASL", "ROL", "DEC", "INC", "TST", "CLR")


@dataclass
class Code:
    instructions: dict = field(default_factory=dict)    # (bank, address) -> Instruction
    entries: dict = field(default_factory=dict)          # (bank, address) -> why (vector, call, table...)
    calls: set = field(default_factory=set)              # (bank, address) of call targets
    unresolved: list = field(default_factory=list)       # (bank, Instruction) jumps and calls through registers
    outside: list = field(default_factory=list)          # (bank, Instruction, target): flow out of the ROM
    unknown_bank: list = field(default_factory=list)     # (Instruction, target): into 4000-5FFF, bank unknown
    illegal: list = field(default_factory=list)          # (bank, Instruction)
    bank_switches: list = field(default_factory=list)    # (bank, Instruction, new bank or None)


def key(bank, address):
    return (None if address >= FIXED_START else bank, address)


def bank_of(value):
    """What the latch selects for a value written to 3E00 (MAME's bankswitch_w)."""
    return 4 if value == 4 else value & 3


def track(ins, regs):
    """A and B's constants after 'ins' (regs: {'A': value or None, 'B': ...})."""
    name, op = ins.mnemonic, ins.operand
    if name in ("LDA", "LDB") and op.mode == "immediate":
        regs[name[-1]] = op.value
    elif name == "LDD" and op.mode == "immediate":
        regs["A"], regs["B"] = op.value >> 8, op.value & 0xFF
    elif name in ("CLRA", "CLRB"):
        regs[name[-1]] = 0
    elif name.startswith(KEEPS_AB_PREFIXES) or ins.flow in ("branch", "jump"):
        pass
    elif name in MEMORY_RMW and op.mode != "inherent":
        pass
    elif name == "TFR" and op.registers[1] not in ("A", "B", "D"):
        pass
    else:
        regs["A"] = regs["B"] = None


def explore(program, entries, tables=(), stop=()):
    """The code reached from 'entries' ((bank, address, why)); 'tables' are (bank, address, count, why):
    jump tables of 16-bit addresses whose targets run in 'bank'; 'stop' are addresses of routines that do
    not return to their caller."""
    code = Code()
    work = []
    seen = set()

    def add(bank, address, why):
        k = key(bank, address)
        if k not in code.entries:
            code.entries[k] = why
        work.append((bank, address))

    for bank, address, why in entries:
        add(bank, address, why)
    for bank, address, count, why in tables:
        read = program.reader(None)
        for index in range(count):
            target = (read(address + 2 * index) << 8) | read(address + 2 * index + 1)
            add(bank, target, f"{why}[{index}]")

    stops = set(stop)
    while work:
        bank, address = work.pop()
        regs = {"A": None, "B": None}
        while True:
            if (bank, address) in seen:
                break
            seen.add((bank, address))
            if address < 0x4000:
                code.outside.append((bank, None, address))
                break
            if address < FIXED_START and bank is None:
                code.unknown_bank.append((None, address))
                break
            k = key(bank, address)
            ins = code.instructions.get(k) or dis.decode(program.reader(bank), address)
            code.instructions[k] = ins
            if ins.flow == "illegal":
                code.illegal.append((bank, ins))
                break
            if ins.target is not None and ins.flow in ("branch", "jump", "call"):
                target = ins.target
                if target < 0x4000:
                    code.outside.append((bank, ins, target))
                elif target < FIXED_START and bank is None:
                    code.unknown_bank.append((ins, target))
                else:
                    if ins.flow == "call":
                        code.calls.add(key(bank, target))
                    add(bank, target, f"{ins.mnemonic.lower()} from {address:04X}")
            if ins.flow in ("ijump", "icall"):
                code.unresolved.append((bank, ins))
            # the bank latch
            if ins.mnemonic in ("STA", "STB", "STD") and ins.operand.mode == "extended" and \
                    ins.operand.value in (BANK_LATCH, BANK_LATCH - 1):
                stored = None
                if ins.mnemonic == "STD":
                    stored = regs["B"] if ins.operand.value == BANK_LATCH - 1 else regs["A"]
                else:
                    stored = regs[ins.mnemonic[-1]]
                bank = None if stored is None else bank_of(stored)
                code.bank_switches.append((bank, ins, bank))
            track(ins, regs)
            if ins.flow in ("jump", "return", "rti", "ijump", "illegal"):
                break
            if ins.flow == "call" and ins.target in stops:
                break
            address = ins.next
    return code


def coverage(code, start=FIXED_START, end=0x10000):
    """The bytes of start-end the code covers."""
    covered = bytearray(end - start)
    for (bank, address), ins in code.instructions.items():
        if bank is None and start <= address < end:
            for offset in range(ins.size):
                if address + offset < end:
                    covered[address + offset - start] = 1
    return covered
