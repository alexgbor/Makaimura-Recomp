"""
Name:        disassembler.py
Description: the Motorola 6809's decoder: every instruction's length, operands and control flow, and its text
Version:     0.1.0
Created:     19-11-2026

The 6809 as Motorola's MC6809 datasheet (and MAME's src/devices/cpu/m6809, BSD-3-Clause, Nathan Woods and
others) describes it: three opcode pages (none, $10, $11), the addressing modes immediate, direct (the DP
register's page), extended, indexed (with its postbyte) and relative, and the inherent instructions.

decode(read, address) gives an Instruction: its size, its mnemonic, its addressing mode and operand, the text
as MAME's disassembler writes it (6x09dasm.cpp: "$" hex, ",X", "[...]"), and how control leaves it (flow):

    next      the next instruction only
    branch    a conditional branch: target, or the next instruction
    jump      an unconditional jump to target
    call      a subroutine call to target (BSR, LBSR, JSR to a known address), then the next instruction
    return    RTS, or PULS with PC
    rti       RTI
    ijump     a jump through a register or memory (JMP indexed / indirect, TFR or EXG into PC)
    icall     a call through a register or memory (JSR indexed / indirect)
    swi       SWI, SWI2, SWI3 (through their vectors), then the next instruction (on return)
    wait      CWAI and SYNC: the CPU waits for an interrupt, then goes on with the next instruction
    illegal   an opcode the 6809 does not have

'read' is a function of an address (0-FFFF) giving its byte: the caller decides which bank the address is in.
"""
from dataclasses import dataclass, field

REGISTERS_INDEX = ("X", "Y", "U", "S")
# TFR and EXG's postbyte (the 6809's codes; 6, 7 and C-F are not registers on the 6809)
REGISTERS_TFR = {0x0: "D", 0x1: "X", 0x2: "Y", 0x3: "U", 0x4: "S", 0x5: "PC", 0x8: "A", 0x9: "B", 0xA: "CC",
                 0xB: "DP"}
PUSH_S = ("CC", "A", "B", "DP", "X", "Y", "U", "PC")    # bit 0 first; with U, bit 6 is S
PUSH_U = ("CC", "A", "B", "DP", "X", "Y", "S", "PC")

BRANCHES = ("BRA", "BRN", "BHI", "BLS", "BCC", "BCS", "BNE", "BEQ",
            "BVC", "BVS", "BPL", "BMI", "BGE", "BLT", "BGT", "BLE")

# the read-modify-write group (00-0F direct, 40-4F A, 50-5F B, 60-6F indexed, 70-7F extended)
RMW = {0x0: "NEG", 0x3: "COM", 0x4: "LSR", 0x6: "ROR", 0x7: "ASR", 0x8: "ASL", 0x9: "ROL", 0xA: "DEC",
       0xC: "INC", 0xD: "TST", 0xE: "JMP", 0xF: "CLR"}

# the accumulator groups (80-BF with A, C0-FF with B): low nibble -> (mnemonic, 16-bit operand)
GROUP_A = {0x0: ("SUBA", False), 0x1: ("CMPA", False), 0x2: ("SBCA", False), 0x3: ("SUBD", True),
           0x4: ("ANDA", False), 0x5: ("BITA", False), 0x6: ("LDA", False), 0x7: ("STA", False),
           0x8: ("EORA", False), 0x9: ("ADCA", False), 0xA: ("ORA", False), 0xB: ("ADDA", False),
           0xC: ("CMPX", True), 0xD: ("JSR", True), 0xE: ("LDX", True), 0xF: ("STX", True)}
GROUP_B = {0x0: ("SUBB", False), 0x1: ("CMPB", False), 0x2: ("SBCB", False), 0x3: ("ADDD", True),
           0x4: ("ANDB", False), 0x5: ("BITB", False), 0x6: ("LDB", False), 0x7: ("STB", False),
           0x8: ("EORB", False), 0x9: ("ADCB", False), 0xA: ("ORB", False), 0xB: ("ADDB", False),
           0xC: ("LDD", True), 0xD: ("STD", True), 0xE: ("LDU", True), 0xF: ("STU", True)}

# page 2 ($10) and page 3 ($11): low nibble (with the column 8, 9, A, B, C, D, E, F) -> mnemonic
PAGE2 = {0x3: ("CMPD", True), 0xC: ("CMPY", True), 0xE: ("LDY", True), 0xF: ("STY", True)}
PAGE2_B = {0xE: ("LDS", True), 0xF: ("STS", True)}
PAGE3 = {0x3: ("CMPU", True), 0xC: ("CMPS", True)}

INHERENT = {0x12: "NOP", 0x13: "SYNC", 0x19: "DAA", 0x1D: "SEX", 0x39: "RTS", 0x3A: "ABX", 0x3B: "RTI",
            0x3D: "MUL", 0x3F: "SWI"}


@dataclass
class Operand:
    """What an instruction works on. mode is one of: inherent, immediate, direct, extended, indexed,
    relative, register (TFR, EXG), stack (PSHS...). For indexed: base (X, Y, U, S, PC or None for [n16]),
    kind ('offset' with offset, 'acc' with acc, 'inc' / 'dec' with step), indirect, and the PC-relative
    address (address) where the offset is PC's."""
    mode: str
    value: int = None          # immediate value, direct/extended address, relative target, postbyte
    bits: int = 8              # an immediate's width
    base: str = None
    kind: str = None
    offset: int = None
    acc: str = None
    step: int = 0
    indirect: bool = False
    address: int = None        # an address the operand names outright (extended, [n16], n,PC)
    registers: tuple = ()      # TFR/EXG (source, destination), or the stack's registers in push order


@dataclass
class Instruction:
    address: int
    size: int
    opcode: int                # with its page: $10xx, $11xx
    mnemonic: str
    operand: Operand
    flow: str = "next"
    target: int = None         # of a branch, jump or call to a known address
    text: str = ""
    raw: bytes = field(default=b"", repr=False)

    @property
    def next(self):
        return (self.address + self.size) & 0xFFFF


class Reader:
    def __init__(self, read, address):
        self.read = read
        self.start = address
        self.pc = address
        self.bytes = []

    def u8(self):
        value = self.read(self.pc & 0xFFFF) & 0xFF
        self.bytes.append(value)
        self.pc = (self.pc + 1) & 0xFFFF
        return value

    def u16(self):
        high = self.u8()
        return (high << 8) | self.u8()


def s8(value):
    return value - 0x100 if value & 0x80 else value


def s16(value):
    return value - 0x10000 if value & 0x8000 else value


def s5(value):
    return value - 0x20 if value & 0x10 else value


def hex8(value):
    return f"${value & 0xFF:02X}"


def hex16(value):
    return f"${value & 0xFFFF:04X}"


def signed_text(value, digits=0):
    """An offset as MAME's 6x09dasm.cpp writes it: -$xx or $xx (digits: 2 for 8-bit offsets, 4 for 16-bit)."""
    return ("-" if value < 0 else "") + f"${abs(value):0{digits}X}"


def indexed(r):
    """The indexed postbyte and what follows it."""
    post = r.u8()
    base = REGISTERS_INDEX[(post >> 5) & 3]
    if not post & 0x80:
        offset = s5(post & 0x1F)
        return Operand("indexed", value=post, base=base, kind="offset", offset=offset), \
            f"{signed_text(offset)},{base}"
    indirect = bool(post & 0x10)
    mode = post & 0x0F
    if mode == 0x0:
        op, text = Operand("indexed", value=post, base=base, kind="inc", step=1), f",{base}+"
    elif mode == 0x1:
        op, text = Operand("indexed", value=post, base=base, kind="inc", step=2), f",{base}++"
    elif mode == 0x2:
        op, text = Operand("indexed", value=post, base=base, kind="dec", step=1), f",-{base}"
    elif mode == 0x3:
        op, text = Operand("indexed", value=post, base=base, kind="dec", step=2), f",--{base}"
    elif mode == 0x4:
        op, text = Operand("indexed", value=post, base=base, kind="offset", offset=0), f",{base}"
    elif mode == 0x5:
        op, text = Operand("indexed", value=post, base=base, kind="acc", acc="B"), f"B,{base}"
    elif mode == 0x6:
        op, text = Operand("indexed", value=post, base=base, kind="acc", acc="A"), f"A,{base}"
    elif mode == 0xB:
        op, text = Operand("indexed", value=post, base=base, kind="acc", acc="D"), f"D,{base}"
    elif mode == 0x8:
        offset = s8(r.u8())
        op, text = Operand("indexed", value=post, base=base, kind="offset", offset=offset), \
            f"{signed_text(offset, 2)},{base}"
    elif mode == 0x9:
        offset = s16(r.u16())
        op, text = Operand("indexed", value=post, base=base, kind="offset", offset=offset), \
            f"{signed_text(offset, 4)},{base}"
    elif mode == 0xC:
        offset = s8(r.u8())
        address = (r.pc + offset) & 0xFFFF
        op, text = Operand("indexed", value=post, base="PC", kind="offset", offset=offset, address=address), \
            f"{hex16(address)},PCR"
    elif mode == 0xD:
        offset = s16(r.u16())
        address = (r.pc + offset) & 0xFFFF
        op, text = Operand("indexed", value=post, base="PC", kind="offset", offset=offset, address=address), \
            f"{hex16(address)},PCR"
    elif mode == 0xF and indirect:
        address = r.u16()
        op = Operand("indexed", value=post, base=None, kind="offset", offset=0, address=address, indirect=True)
        return op, f"[{hex16(address)}]"
    else:
        return None, None    # 7, A, E, and F without indirection: not 6809 modes
    if indirect:
        if op.kind in ("inc", "dec") and op.step == 1:
            return None, None    # ,R+ and ,-R have no indirect form
        op.indirect = True
        text = f"[{text}]"
    return op, text


def stack_registers(post, user):
    names = PUSH_U if user else PUSH_S
    return tuple(names[bit] for bit in range(8) if post & (1 << bit))


def stack_text(post, user, push):
    """The registers as MAME writes them: a push from PC down, a pull from CC up, A and B together as D."""
    other = "S" if user else "U"
    if push:
        order = (("PC", 0x80), (other, 0x40), ("Y", 0x20), ("X", 0x10), ("DP", 0x08), ("D", 0x06), ("B", 0x04),
                 ("A", 0x02), ("CC", 0x01))
    else:
        order = (("CC", 0x01), ("D", 0x06), ("A", 0x02), ("B", 0x04), ("DP", 0x08), ("X", 0x10), ("Y", 0x20),
                 (other, 0x40), ("PC", 0x80))
    names = []
    for name, mask in order:
        if name == "D":
            if post & 0x06 == 0x06:
                names.append("D")
                post &= ~0x06
            continue
        if post & mask:
            names.append(name)
    return ",".join(names)


def extended_text(address):
    """MAME writes '>' before an extended address of the first page (an assembler would make it direct)."""
    return (">" if address < 0x100 else "") + hex16(address)


def decode(read, address):
    """The instruction at 'address' ('read' gives the bytes)."""
    r = Reader(read, address)
    opcode = r.u8()
    page = 0
    if opcode in (0x10, 0x11):
        page = opcode
        opcode = r.u8()
        # a page byte in front of another page byte: MAME's 6809 takes the second as the page (the first
        # one's cycle is spent); the program has none, so this is left illegal
        if opcode in (0x10, 0x11):
            return finish(r, (page << 8) | opcode, "ILLEGAL", Operand("inherent"), "illegal")
    full = (page << 8) | opcode if page else opcode

    if page == 0:
        return decode_page0(r, opcode)
    return decode_page23(r, page, opcode, full)


def finish(r, opcode, mnemonic, operand, flow="next", target=None, text_operand=None):
    text = mnemonic if not text_operand else f"{mnemonic:<7}{text_operand}"
    return Instruction(r.start, (r.pc - r.start) & 0xFFFF, opcode, mnemonic, operand, flow, target, text,
                       bytes(r.bytes))


def memory_operand(r, column, sixteen):
    """The operand of the columns 8 (immediate), 9 (direct), A (indexed), B (extended)."""
    if column == 0x8:
        if sixteen:
            value = r.u16()
            return Operand("immediate", value=value, bits=16), f"#{hex16(value)}"
        value = r.u8()
        return Operand("immediate", value=value, bits=8), f"#{hex8(value)}"
    if column == 0x9:
        value = r.u8()
        return Operand("direct", value=value), f"<{hex8(value)}"
    if column == 0xA:
        return indexed(r)
    value = r.u16()
    return Operand("extended", value=value, address=value), extended_text(value)


def decode_page0(r, opcode):
    high, low = opcode >> 4, opcode & 0x0F

    if high in (0x0, 0x6, 0x7):    # read-modify-write on memory
        name = RMW.get(low)
        if name is None:
            return finish(r, opcode, "ILLEGAL", Operand("inherent"), "illegal")
        if high == 0x0:
            value = r.u8()
            op, text = Operand("direct", value=value), f"<{hex8(value)}"
        elif high == 0x6:
            op, text = indexed(r)
            if op is None:
                return finish(r, opcode, "ILLEGAL", Operand("inherent"), "illegal")
        else:
            value = r.u16()
            op, text = Operand("extended", value=value, address=value), extended_text(value)
        if name == "JMP":
            if op.mode == "indexed":
                return finish(r, opcode, name, op, "ijump", None, text)
            if op.mode == "direct":
                return finish(r, opcode, name, op, "ijump", None, text)    # (DP's page: known only with DP)
            return finish(r, opcode, name, op, "jump", op.value, text)
        return finish(r, opcode, name, op, "next", None, text)

    if high in (0x4, 0x5):    # read-modify-write on A or B
        name = RMW.get(low)
        if name is None or name == "JMP":
            return finish(r, opcode, "ILLEGAL", Operand("inherent"), "illegal")
        return finish(r, opcode, name + ("A" if high == 0x4 else "B"), Operand("inherent"))

    if high == 0x1:
        if opcode in INHERENT:
            name = INHERENT[opcode]
            return finish(r, opcode, name, Operand("inherent"), "wait" if name == "SYNC" else "next")
        if opcode in (0x16, 0x17):
            offset = s16(r.u16())
            target = (r.pc + offset) & 0xFFFF
            name = "LBRA" if opcode == 0x16 else "LBSR"
            return finish(r, opcode, name, Operand("relative", value=target), "jump" if opcode == 0x16 else "call",
                          target, hex16(target))
        if opcode in (0x1A, 0x1C):
            value = r.u8()
            name = "ORCC" if opcode == 0x1A else "ANDCC"
            return finish(r, opcode, name, Operand("immediate", value=value), "next", None, f"#{hex8(value)}")
        if opcode in (0x1E, 0x1F):
            post = r.u8()
            source, destination = REGISTERS_TFR.get(post >> 4), REGISTERS_TFR.get(post & 0xF)
            name = "EXG" if opcode == 0x1E else "TFR"
            op = Operand("register", value=post, registers=(source, destination))
            if source is None or destination is None:
                return finish(r, opcode, name, op, "illegal", None, f"{source},{destination}")
            flow = "ijump" if destination == "PC" or (name == "EXG" and source == "PC") else "next"
            return finish(r, opcode, name, op, flow, None, f"{source},{destination}")
        return finish(r, opcode, "ILLEGAL", Operand("inherent"), "illegal")

    if high == 0x2:
        offset = s8(r.u8())
        target = (r.pc + offset) & 0xFFFF
        name = BRANCHES[low]
        flow = "jump" if name == "BRA" else ("next" if name == "BRN" else "branch")
        return finish(r, opcode, name, Operand("relative", value=target), flow,
                      None if name == "BRN" else target, hex16(target))

    if high == 0x3:
        if low <= 0x3:
            op, text = indexed(r)
            name = ("LEAX", "LEAY", "LEAS", "LEAU")[low]
            if op is None:
                return finish(r, opcode, "ILLEGAL", Operand("inherent"), "illegal")
            return finish(r, opcode, name, op, "next", None, text)
        if low <= 0x7:
            post = r.u8()
            user = low in (0x6, 0x7)
            name = ("PSHS", "PULS", "PSHU", "PULU")[low - 4]
            registers = stack_registers(post, user)
            op = Operand("stack", value=post, registers=registers)
            text = stack_text(post, user, name.startswith("PSH"))
            flow = "return" if name == "PULS" and post & 0x80 else ("ijump" if name == "PULU" and post & 0x80
                                                                     else "next")
            return finish(r, opcode, name, op, flow, None, text)
        if opcode == 0x3C:
            value = r.u8()
            return finish(r, opcode, "CWAI", Operand("immediate", value=value), "wait", None, f"#{hex8(value)}")
        if opcode in INHERENT:
            name = INHERENT[opcode]
            flow = {"RTS": "return", "RTI": "rti", "SWI": "swi"}.get(name, "next")
            return finish(r, opcode, name, Operand("inherent"), flow)
        return finish(r, opcode, "ILLEGAL", Operand("inherent"), "illegal")

    # 80-FF: the accumulator groups
    group = GROUP_A if high < 0xC else GROUP_B
    column = 0x8 + ((high - 0x8) & 0x3)
    name, sixteen = group[low]
    if column == 0x8 and name in ("STA", "STX", "STB", "STD", "STU"):
        return finish(r, opcode, "ILLEGAL", Operand("inherent"), "illegal")
    if opcode == 0x8D:    # BSR (where JSR immediate would be)
        offset = s8(r.u8())
        target = (r.pc + offset) & 0xFFFF
        return finish(r, opcode, "BSR", Operand("relative", value=target), "call", target, hex16(target))
    op, text = memory_operand(r, column, sixteen)
    if op is None:
        return finish(r, opcode, "ILLEGAL", Operand("inherent"), "illegal")
    if name == "JSR":
        if op.mode == "extended":
            return finish(r, opcode, name, op, "call", op.value, text)
        return finish(r, opcode, name, op, "icall", None, text)
    return finish(r, opcode, name, op, "next", None, text)


def decode_page23(r, page, opcode, full):
    high, low = opcode >> 4, opcode & 0x0F
    if page == 0x10 and high == 0x2:
        if low == 0x0:    # (1020: MAME's undocumented XLBRA; LBRA is 16)
            return finish(r, full, "ILLEGAL", Operand("inherent"), "illegal")
        offset = s16(r.u16())
        target = (r.pc + offset) & 0xFFFF
        name = "L" + BRANCHES[low]
        flow = "jump" if name == "LBRA" else ("next" if name == "LBRN" else "branch")
        return finish(r, full, name, Operand("relative", value=target), flow,
                      None if name == "LBRN" else target, hex16(target))
    if opcode == 0x3F:
        return finish(r, full, "SWI2" if page == 0x10 else "SWI3", Operand("inherent"), "swi")
    if high >= 0x8:
        column = 0x8 + ((high - 0x8) & 0x3)
        if page == 0x10:
            table = PAGE2 if high < 0xC else PAGE2_B
        else:
            table = PAGE3 if high < 0xC else {}
        entry = table.get(low)
        if entry is None or (column == 0x8 and entry[0] in ("STY", "STS")):
            return finish(r, full, "ILLEGAL", Operand("inherent"), "illegal")
        name, sixteen = entry
        op, text = memory_operand(r, column, sixteen)
        if op is None:
            return finish(r, full, "ILLEGAL", Operand("inherent"), "illegal")
        return finish(r, full, name, op, "next", None, text)
    return finish(r, full, "ILLEGAL", Operand("inherent"), "illegal")
