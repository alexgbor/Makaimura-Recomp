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
Jump tables the symbols describe (symbols.txt: "table" entries) add their entries as code. Code the program
copies to RAM and runs there (symbols.txt's "overlay": the bytes it copies) is read from those bytes, keyed by its
RAM address with no bank.
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
    jump_tables: dict = field(default_factory=dict)      # (bank, jump's address) -> (table, [targets])


def key(bank, address):
    return (None if address >= FIXED_START or address < 0x4000 else bank, address)


def bank_of(value):
    """What the latch selects for a value written to 3E00 (MAME's bankswitch_w)."""
    return 4 if value == 4 else value & 3


INDEX_REGISTERS = ("X", "Y", "U")


def writes_index(ins, register):
    """Whether 'ins' may change an index register: its auto-increment and decrement modes, LDr, LEAr, ABX,
    TFR/EXG, PULx, PSHU/PULU's U, and calls (the routine may change any)."""
    name, op = ins.mnemonic, ins.operand
    if name in ("LD" + register, "LEA" + register) or ins.flow in ("call", "icall", "swi"):
        return True
    if name == "ABX":
        return register == "X"
    if name in ("TFR", "EXG", "PULS", "PULU"):
        if name in ("PULU",) and register == "U":
            return True
        return register in op.registers
    if name == "PSHU" and register == "U":
        return True
    return op.mode == "indexed" and op.base == register and op.kind in ("inc", "dec")


def track(ins, regs):
    """A, B, X, Y and U's constants after 'ins' (regs: {'A': value or None, ...})."""
    name, op = ins.mnemonic, ins.operand
    loaded = None
    if name in ("LDX", "LDY", "LDU") and op.mode == "indexed" and not op.indirect and op.kind == "acc" and             op.base in INDEX_REGISTERS and regs.get(op.base) is not None:
        loaded = ("table", regs[op.base])    # a word of a table, by an accumulator's offset
    for register in INDEX_REGISTERS:
        if writes_index(ins, register):
            regs[register] = op.value if name == "LD" + register and op.mode == "immediate" else None
    if loaded:
        regs["loaded_" + name[-1]] = loaded[1]
    for register in INDEX_REGISTERS:
        if writes_index(ins, register) and not (loaded and name[-1] == register):
            regs.pop("loaded_" + register, None)
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


MAX_TABLE = 64


def plausible(bank, target):
    """A code address a table of this bank could hold: the fixed ROM, or the window with the bank known."""
    return target >= FIXED_START or (target >= 0x4000 and bank is not None)


def known_ends(code, ins):
    """Where a table found at a jump cannot go past: the code right after the jump (tables usually follow it)."""
    return ins.next


def looks_like_code(program, bank, target, count=3):
    """Whether the first 'count' instructions at 'target' (falling through) are legal 6809 instructions."""
    read = program.reader(bank)
    address = target
    for _ in range(count):
        ins = dis.decode(read, address)
        if ins.flow == "illegal":
            return False
        if ins.flow in ("jump", "return", "rti", "ijump"):
            return True
        address = ins.next
    return True


def read_table(program, bank, table, code, counts=None):
    """A jump table's entries, from 'table' (the LDX #table before the jump). Without its size in the symbols,
    it ends at the first entry that is not a code address or whose target does not decode as code, at the
    lowest target it holds when that is past the table (the code the table points to usually follows it), at a
    place already known to be code, or after MAX_TABLE entries."""
    read = program.reader(bank)
    if counts and table in counts:
        # the symbols' size (and bank, for a table in the window): their entries, the unused ones (0000) left out
        count, table_bank = counts[table]
        if table_bank is not None:
            read = program.reader(table_bank)
        entries = ((read(table + 2 * i) << 8) | read(table + 2 * i + 1) for i in range(count))
        return [target for target in entries if target]
    targets = []
    lowest = 0x10000
    for index in range(MAX_TABLE):
        place = table + 2 * index
        if place >= lowest or key(bank, place) in code.instructions or key(bank, place + 1) in code.instructions:
            break
        target = (read(place) << 8) | read(place + 1)
        if not plausible(bank, target) or not looks_like_code(program, bank, target):
            break
        if target > table:
            lowest = min(lowest, target)
        targets.append(target)
    return targets


def explore(program, entries, tables=(), stop=(), ram_code=None):
    """The code reached from 'entries' ((bank, address, why)); 'tables' are (bank, address, count, why):
    jump tables of 16-bit addresses whose targets run in 'bank'; 'stop' are addresses of routines that do
    not return to their caller; 'ram_code' {address: byte} the RAM that holds code (the overlays)."""
    code = Code()
    work = []
    seen = set()
    ram_code = ram_code or {}

    def reader(bank):
        base = program.reader(bank)
        if not ram_code:
            return base
        return lambda address: ram_code[address] if address in ram_code else base(address)

    def add(bank, address, why):
        k = key(bank, address)
        if k not in code.entries:
            code.entries[k] = why
        work.append((bank, address))

    for bank, address, why in entries:
        add(bank, address, why)
    counts = {address: (count, table_bank) for _, address, count, _, table_bank in tables}
    for bank, address, count, why, table_bank in tables:
        read = program.reader(table_bank)
        for index in range(count):
            target = (read(address + 2 * index) << 8) | read(address + 2 * index + 1)
            if target:    # (0000: an unused slot)
                add(bank, target, f"{why}[{index}]")

    stops = set(stop)
    while work:
        bank, address = work.pop()
        regs = {"A": None, "B": None, "X": None, "Y": None, "U": None}
        while True:
            if (bank, address) in seen:
                break
            seen.add((bank, address))
            if address < 0x4000 and address not in ram_code:
                code.outside.append((bank, None, address))
                break
            if 0x4000 <= address < FIXED_START and bank is None:
                code.unknown_bank.append((None, address))
                break
            k = key(bank, address)
            ins = code.instructions.get(k) or dis.decode(reader(bank), address)
            code.instructions[k] = ins
            if ins.flow == "illegal":
                code.illegal.append((bank, ins))
                break
            if ins.target is not None and ins.flow in ("branch", "jump", "call"):
                target = ins.target
                if target < 0x4000 and target not in ram_code:
                    code.outside.append((bank, ins, target))
                elif 0x4000 <= target < FIXED_START and bank is None:
                    code.unknown_bank.append((ins, target))
                else:
                    if ins.flow == "call":
                        code.calls.add(key(bank, target))
                    add(bank, target, f"{ins.mnemonic.lower()} from {address:04X}")
            if ins.flow in ("ijump", "icall"):
                op = ins.operand
                table = regs.get(op.base) if (op.mode == "indexed" and op.indirect and
                                              op.base in INDEX_REGISTERS and op.kind == "acc") else None
                if table is None and op.mode == "indexed" and not op.indirect and op.kind == "offset" and                         op.offset == 0 and op.base in INDEX_REGISTERS:
                    table = regs.get("loaded_" + op.base)    # LDX #table / LDX A,X / JSR ,X
                if table is not None and (bank, address) not in code.jump_tables:
                    targets = read_table(program, bank, table, code, counts)
                    code.jump_tables[(bank, address)] = (table, targets)
                    for index, target in enumerate(targets):
                        if ins.flow == "icall":
                            code.calls.add(key(bank, target))
                        add(bank, target, f"table {table:04X}[{index}] from {address:04X}")
                elif table is None:
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
