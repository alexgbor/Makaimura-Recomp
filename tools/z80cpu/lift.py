"""
Name:        lift.py
Description: translates the sound program's Z80 instructions into the core's IR (tools/core/v60/ir.py), doing what
             MAME 0.289's Z80 core does: its bus cycles in its order, each at its cycle, and the flags the program
             can see as it sets them
Version:     0.1.0
Created:     19-11-2026

license:BSD-3-Clause
After MAME's src/devices/cpu/z80 (BSD-3-Clause, Juergen Buchmueller and others): z80.lst, the core's steps, where
each access comes at the start of its machine cycle ("N !! access": the access, then N cycles) and "+ N" are
cycles of its own; the opcode fetch is an M1 cycle of 4, a memory access 3. The instructions are decoded by
arcade-decomp's Z80 disassembler (tools/core/z80/disassembler.py, MAME's z80dasm.cpp's).

The registers are the core's decompiler's: BC, DE, HL, IX, IY, SP (16 bits, indexes 0-5), A (6), and the other
bank's BC', DE', HL', A' (7-10). The flags are S, Z, H, P/V, N, C (s, z, h, ov, n, cy; the other bank's s_alt...).
The undocumented flags (bits 3 and 5), the WZ register and the refresh register are not kept: the program never
puts F where it can be read (no PUSH AF, no DAA, no LD A,R), so they change nothing it does; the translator
refuses those instructions (tools/z80cpu/translate.py), so that this stays true.

lift(instruction, read) gives a Lifter: statements, control, cycles, accesses (kind R or W, cycle; R the opcode
and operand fetches too, whose addresses are known here: address set) and extras {"taken": cycles} for a branch
whose taken way is longer (JR cc, DJNZ, LDIR's repeat). CALL cc and RET cc make accesses only when taken: their
taken way's statements and accesses are taken_statements and taken_accesses, after the common ones.
Control: next; goto (JP, JR); branch (JR cc, JP cc, DJNZ, LDIR: condition, target); call (CALL, RST); call_if
(CALL cc); return (RET); return_if (RET cc); jump (JP (HL)); halt; and the effects di, ei, im (Effect).
"""
from dataclasses import dataclass, field
import os
import sys

TOOLS = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(TOOLS, "core"))

from v60 import ir                           # noqa: E402  (the core's IR)
from z80 import disassembler as zd           # noqa: E402  (the core's Z80 decoder)

BC, DE, HL, IX, IY, SP, A = 0, 1, 2, 3, 4, 5, 6
BC2, DE2, HL2, A2 = 7, 8, 9, 10
WIDTHS = (16, 16, 16, 16, 16, 16, 8, 16, 16, 16, 8)
NAMES = ("bc", "de", "hl", "ix", "iy", "sp", "a", "bc_alt", "de_alt", "hl_alt", "a_alt")
PAIRS = {"bc": BC, "de": DE, "hl": HL, "ix": IX, "iy": IY, "sp": SP}
BYTES = {"b": (BC, 1), "c": (BC, 0), "d": (DE, 1), "e": (DE, 0), "h": (HL, 1), "l": (HL, 0),
         "ixh": (IX, 1), "ixl": (IX, 0), "iyh": (IY, 1), "iyl": (IY, 0)}
FLAGS = ("s", "z", "h", "ov", "n", "cy")
F_BITS = {"s": 7, "z": 6, "h": 4, "ov": 2, "n": 1, "cy": 0}
F_SEEN = 0xD7          # the bits of F kept (S Z H P/V N C)
ALU = ("add", "adc", "sub", "sbc", "and", "xor", "or", "cp")
SHIFTS = ("rlc", "rrc", "rl", "rr", "sla", "sra", "sll", "srl")


class Unsupported(Exception):
    """An instruction the lift does not translate."""


@dataclass
class Control:
    kind: str = "next"
    target: int = None
    address: object = None
    condition: object = None


@dataclass
class Access:
    kind: str
    cycle: int
    address: int = None    # a fetch's (R of the instruction's own bytes)
    when: str = None       # "taken": only on the taken way


def c(value, bits):
    return ir.Const(value & ((1 << bits) - 1), bits)


def op(name, args, bits):
    return ir.Op(name, tuple(args), bits)


def lnot(value):
    return op("eq", (value, ir.Const(0, value.bits)), 1)


def parity(value):
    """P/V after logic: 1 for an even number of bits set (8 bits)"""
    folded = op("xor", (value, op("shr", (value, c(4, 8)), 8)), 8)
    folded = op("xor", (folded, op("shr", (folded, c(2, 8)), 8)), 8)
    folded = op("xor", (folded, op("shr", (folded, c(1, 8)), 8)), 8)
    return lnot(ir.trunc(folded, 1))


class Lifter:
    def __init__(self, instruction, read, prefix="t"):
        self.instruction = instruction
        self.read_byte = read
        self.address = instruction.address
        self.next_address = (instruction.address + instruction.length) & 0xFFFF
        self.statements = []
        self.accesses = []
        self.taken_statements = []
        self.taken_accesses = []
        self.control = Control()
        self.cycle = 0
        self.extras = {}
        self.prefix = prefix
        self.count = 0
        self.pc = instruction.address
        self.taken = False      # building the taken way (CALL cc, RET cc)
        self.lift()

    @property
    def cycles(self):
        return self.cycle

    # ---- the bus: an access, then its cycles ----

    def _accesses(self):
        return self.taken_accesses if self.taken else self.accesses

    def _statements(self):
        return self.taken_statements if self.taken else self.statements

    def emit(self, statement):
        self._statements().append(statement)

    def m1(self):
        """an opcode fetch (M1): 4 cycles"""
        value = self.read_byte(self.pc)
        self._accesses().append(Access("R", self.cycle, self.pc))
        self.pc = (self.pc + 1) & 0xFFFF
        self.cycle += 4
        return value

    def arg(self):
        """an operand byte fetched: 3 cycles"""
        value = self.read_byte(self.pc)
        self._accesses().append(Access("R", self.cycle, self.pc))
        self.pc = (self.pc + 1) & 0xFFFF
        self.cycle += 3
        return value

    def internal(self, cycles):
        self.cycle += cycles

    def temp(self, bits, name="v"):
        self.count += 1
        return ir.Temp(f"{self.prefix}{name}{self.count}", bits)

    def bind(self, value, name="v"):
        if isinstance(value, (ir.Const, ir.Temp)):
            return value
        temp = self.temp(value.bits, name)
        self.emit(ir.SetTemp(temp, value))
        return temp

    def read(self, address, name="m"):
        temp = self.temp(8, name)
        self.emit(ir.SetTemp(temp, ir.Load(address, 8)))
        self._accesses().append(Access("R", self.cycle))
        self.cycle += 3
        return temp

    def write(self, address, value):
        self.emit(ir.Store(address, value, 8))
        self._accesses().append(Access("W", self.cycle))
        self.cycle += 3

    # ---- registers and flags ----

    def reg(self, index):
        return ir.Reg(index, WIDTHS[index])

    def set_reg(self, index, value):
        self.emit(ir.SetReg(index, value))

    def get8(self, name):
        if name == "a":
            return self.reg(A)
        pair, part = BYTES[name]
        value = self.reg(pair)
        return ir.trunc(op("shr", (value, c(8, 8)), 16), 8) if part else ir.trunc(value, 8)

    def set8(self, name, value):
        if name == "a":
            self.set_reg(A, value)
            return
        pair, part = BYTES[name]
        old = self.reg(pair)
        value = ir.zext(value, 16)
        if part:
            new = op("or", (op("and", (old, c(0x00FF, 16)), 16), op("shl", (value, c(8, 8)), 16)), 16)
        else:
            new = op("or", (op("and", (old, c(0xFF00, 16)), 16), value), 16)
        self.set_reg(pair, new)

    def flag(self, name):
        return ir.Flag(name)

    def set_flag(self, name, value):
        self.emit(ir.SetFlag(name, value))

    def word(self, low, high):
        return op("or", (op("shl", (ir.zext(high, 16), c(8, 8)), 16), ir.zext(low, 16)), 16)

    def displaced(self, register, displacement):
        return op("add", (self.reg(PAIRS[register]), c(displacement, 16)), 16)

    # ---- flags of arithmetic and logic (8 bits) ----

    def alu(self, name, value):
        a = self.bind(self.reg(A), "a")
        value = self.bind(value, "o")
        a16, v16 = ir.zext(a, 16), ir.zext(value, 16)
        carry = ir.zext(self.flag("cy"), 16)
        if name in ("add", "adc"):
            total = op("add", (a16, v16), 16)
            if name == "adc":
                total = op("add", (total, carry), 16)
        elif name in ("sub", "sbc", "cp"):
            total = op("sub", (a16, v16), 16)
            if name == "sbc":
                total = op("sub", (total, carry), 16)
        else:
            total = ir.zext(op({"and": "and", "xor": "xor", "or": "or"}[name], (a, value), 8), 16)
        total = self.bind(total, "r")
        result = self.bind(ir.trunc(total, 8), "r")
        self.set_flag("s", ir.bit(result, 7))
        self.set_flag("z", lnot(result))
        if name in ("and", "xor", "or"):
            self.set_flag("h", c(1 if name == "and" else 0, 1))
            self.set_flag("ov", parity(result))
            self.set_flag("n", c(0, 1))
            self.set_flag("cy", c(0, 1))
        else:
            mixed = op("xor", (op("xor", (a16, v16), 16), total), 16)
            self.set_flag("h", ir.bit(mixed, 4))
            if name in ("add", "adc"):
                overflow = op("and", (op("xor", (a, result), 8), op("xor", (value, result), 8)), 8)
            else:
                overflow = op("and", (op("xor", (a, value), 8), op("xor", (a, result), 8)), 8)
            self.set_flag("ov", ir.bit(overflow, 7))
            self.set_flag("n", c(0 if name in ("add", "adc") else 1, 1))
            self.set_flag("cy", ir.bit(total, 8))
        if name != "cp":
            self.set_reg(A, result)

    def inc_dec(self, value, decrement):
        value = self.bind(value, "o")
        result = self.bind(op("sub" if decrement else "add", (value, c(1, 8)), 8), "r")
        self.set_flag("s", ir.bit(result, 7))
        self.set_flag("z", lnot(result))
        self.set_flag("h", op("eq", (op("and", (result, c(0x0F, 8)), 8), c(0x0F if decrement else 0, 8)), 1))
        self.set_flag("ov", op("eq", (result, c(0x7F if decrement else 0x80, 8)), 1))
        self.set_flag("n", c(1 if decrement else 0, 1))
        return result

    def shift(self, name, value):
        """the CB group's rotates and shifts: the result, S Z P from it, H 0, N 0, C the bit out"""
        value = self.bind(value, "o")
        carry = ir.zext(self.flag("cy"), 8)
        left = name in ("rlc", "rl", "sla", "sll")
        out = ir.bit(value, 7) if left else ir.bit(value, 0)
        if left:
            shifted = op("shl", (value, c(1, 8)), 8)
            low = {"rlc": ir.zext(out, 8), "rl": carry, "sla": c(0, 8), "sll": c(1, 8)}[name]
            result = op("or", (shifted, low), 8)
        else:
            shifted = op("shr", (value, c(1, 8)), 8)
            high = {"rrc": op("shl", (ir.zext(out, 8), c(7, 8)), 8), "rr": op("shl", (carry, c(7, 8)), 8),
                    "sra": op("and", (value, c(0x80, 8)), 8), "srl": c(0, 8)}[name]
            result = op("or", (shifted, high), 8)
        result = self.bind(result, "r")
        self.set_flag("s", ir.bit(result, 7))
        self.set_flag("z", lnot(result))
        self.set_flag("h", c(0, 1))
        self.set_flag("ov", parity(result))
        self.set_flag("n", c(0, 1))
        self.set_flag("cy", out)
        return result

    def condition(self, code):
        flag = {"nz": ("z", 0), "z": ("z", 1), "nc": ("cy", 0), "c": ("cy", 1), "po": ("ov", 0),
                "pe": ("ov", 1), "p": ("s", 0), "m": ("s", 1)}[code]
        value = self.flag(flag[0])
        return value if flag[1] else lnot(value)

    # ---- the stack ----

    def push16(self, value):
        value = self.bind(value, "p")
        high = self.bind(op("sub", (self.reg(SP), c(1, 16)), 16), "sp")
        self.write(high, ir.trunc(op("shr", (value, c(8, 8)), 16), 8))
        low = self.bind(op("sub", (high, c(1, 16)), 16), "sp")
        self.write(low, ir.trunc(value, 8))
        self.set_reg(SP, low)

    def pop16(self):
        sp = self.bind(self.reg(SP), "sp")
        low = self.read(sp, "pl")
        high = self.read(op("add", (sp, c(1, 16)), 16), "ph")
        self.set_reg(SP, op("add", (sp, c(2, 16)), 16))
        return self.bind(self.word(low, high), "w")

    # ---- the instructions ----

    def lift(self):
        ins = self.instruction
        if ins.flow == zd.FLOW_INVALID:
            raise Unsupported(f"{self.address:04X}: invalid")
        name = ins.mnemonic
        ops = ins.operands
        first = self.read_byte(self.address)
        index = first in (0xDD, 0xFD)
        if index:
            self.m1()
        second = self.read_byte(self.pc)
        if second == 0xCB and index:
            return self.indexed_cb(ins)
        if name in ("daa", "in", "out", "ini", "ind", "outi", "outd", "inir", "indr", "otir", "otdr", "reti",
                    "retn", "rrd", "rld") or (name == "push" and ops[0].reg == "af") or \
                (name == "ld" and any(o.mode == "reg" and o.reg in ("i", "r") for o in ops)):
            raise Unsupported(f"{self.address:04X}: {ins.text}")
        handler = getattr(self, "lift_" + name, None)
        if handler is None:
            raise Unsupported(f"{self.address:04X}: {ins.text}")
        handler(ins, ops)

    def operand_value(self, operand):
        """an 8-bit source: a register, an immediate (fetched), (HL), (BC), (DE), (IX+d), (nn)"""
        if operand.mode == "reg":
            return self.get8(operand.reg)
        if operand.mode == "immediate":
            return c(self.arg(), 8)
        if operand.mode == "reg_indirect":
            return self.read(self.reg(PAIRS[operand.reg]))
        if operand.mode == "indexed":
            self.arg()
            self.internal(5)
            return self.read(self.displaced(operand.reg, operand.disp))
        if operand.mode == "direct":
            self.arg()
            self.arg()
            return self.read(c(operand.value, 16))
        raise Unsupported(f"{self.address:04X}: operand {operand.mode}")

    # 8-bit and 16-bit loads
    def lift_ld(self, ins, ops):
        self.m1()
        destination, source = ops
        if destination.mode == "reg" and destination.reg in PAIRS:
            if source.mode == "immediate":
                self.arg()
                self.arg()
                self.set_reg(PAIRS[destination.reg], c(source.value, 16))
            elif source.mode == "direct":
                if self.read_byte(self.address) == 0xED:
                    self.m1()
                self.arg()
                self.arg()
                low = self.read(c(source.value, 16))
                high = self.read(c(source.value + 1, 16))
                self.set_reg(PAIRS[destination.reg], self.word(low, high))
            elif source.mode == "reg":    # LD SP,HL / IX / IY
                self.internal(2)
                self.set_reg(SP, self.reg(PAIRS[source.reg]))
            else:
                raise Unsupported(f"{self.address:04X}: {ins.text}")
            return
        if destination.mode == "direct" and source.mode == "reg" and source.reg in PAIRS:
            if self.read_byte(self.address) == 0xED:
                self.m1()
            self.arg()
            self.arg()
            value = self.bind(self.reg(PAIRS[source.reg]), "w")
            self.write(c(destination.value, 16), ir.trunc(value, 8))
            self.write(c(destination.value + 1, 16), ir.trunc(op("shr", (value, c(8, 8)), 16), 8))
            return
        if destination.mode == "reg":
            if source.mode == "reg":
                self.set8(destination.reg, self.get8(source.reg))
                return
            self.set8(destination.reg, self.operand_value(source))
            return
        # stores
        if destination.mode == "indexed":
            self.arg()
            if source.mode == "immediate":
                value = c(self.arg(), 8)
                self.internal(2)
            else:
                self.internal(5)
                value = self.get8(source.reg)
            self.write(self.displaced(destination.reg, destination.disp), value)
            return
        if destination.mode == "reg_indirect":
            value = c(self.arg(), 8) if source.mode == "immediate" else self.get8(source.reg)
            self.write(self.reg(PAIRS[destination.reg]), value)
            return
        if destination.mode == "direct":
            self.arg()
            self.arg()
            self.write(c(destination.value, 16), self.get8(source.reg))
            return
        raise Unsupported(f"{self.address:04X}: {ins.text}")

    def lift_push(self, ins, ops):
        self.m1()
        self.internal(1)
        self.push16(self.reg(PAIRS[ops[0].reg]))

    def lift_pop(self, ins, ops):
        if ops[0].reg == "af":
            raise Unsupported(f"{self.address:04X}: {ins.text}")
        self.m1()
        self.set_reg(PAIRS[ops[0].reg], self.pop16())

    def lift_ex(self, ins, ops):
        self.m1()
        first, second = ops
        if first.reg == "af":
            a = self.bind(self.reg(A), "a")
            self.set_reg(A, self.reg(A2))
            self.set_reg(A2, a)
            for name in FLAGS:
                kept = self.bind(ir.zext(self.flag(name), 8), "f")
                self.set_flag(name, self.flag(name + "_alt"))
                self.set_flag(name + "_alt", ir.trunc(kept, 1))
        elif first.reg == "de":
            de = self.bind(self.reg(DE), "de")
            self.set_reg(DE, self.reg(HL))
            self.set_reg(HL, de)
        elif first.mode == "reg_indirect":    # EX (SP),HL / IX / IY
            sp = self.bind(self.reg(SP), "sp")
            low = self.read(sp)
            high = self.read(op("add", (sp, c(1, 16)), 16))
            self.internal(1)
            register = PAIRS[second.reg]
            value = self.bind(self.reg(register), "w")
            self.write(op("add", (sp, c(1, 16)), 16), ir.trunc(op("shr", (value, c(8, 8)), 16), 8))
            self.write(sp, ir.trunc(value, 8))
            self.internal(2)
            self.set_reg(register, self.word(low, high))
        else:
            raise Unsupported(f"{self.address:04X}: {ins.text}")

    def lift_exx(self, ins, ops):
        self.m1()
        for register, other in ((BC, BC2), (DE, DE2), (HL, HL2)):
            kept = self.bind(self.reg(register), "x")
            self.set_reg(register, self.reg(other))
            self.set_reg(other, kept)

    # arithmetic and logic
    def alu_lift(self, ins, ops, name):
        self.m1()
        source = ops[-1]
        if source.mode == "reg" and source.reg in PAIRS:
            return self.alu16(ins, ops, name)
        self.alu(name, self.operand_value(source))

    def alu16(self, ins, ops, name):
        destination, source = ops
        if self.read_byte(self.address) == 0xED:
            self.m1()
        self.internal(7)
        register = PAIRS[destination.reg]
        a = self.bind(self.reg(register), "a")
        b = self.bind(self.reg(PAIRS[source.reg]), "b")
        a32, b32 = ir.zext(a, 32), ir.zext(b, 32)
        carry = ir.zext(self.flag("cy"), 32)
        if name == "add":
            total = op("add", (a32, b32), 32)
        elif name == "adc":
            total = op("add", (op("add", (a32, b32), 32), carry), 32)
        else:
            total = op("sub", (op("sub", (a32, b32), 32), carry), 32)
        total = self.bind(total, "r")
        result = self.bind(ir.trunc(total, 16), "r")
        self.set_flag("h", ir.bit(op("xor", (op("xor", (a32, b32), 32), total), 32), 12))
        self.set_flag("n", c(1 if name == "sbc" else 0, 1))
        self.set_flag("cy", ir.bit(total, 16))
        if name != "add":
            self.set_flag("s", ir.bit(result, 15))
            self.set_flag("z", lnot(result))
            if name == "adc":
                overflow = op("and", (op("xor", (a, result), 16), op("xor", (b, result), 16)), 16)
            else:
                overflow = op("and", (op("xor", (a, b), 16), op("xor", (a, result), 16)), 16)
            self.set_flag("ov", ir.bit(overflow, 15))
        self.set_reg(register, result)

    def lift_add(self, ins, ops): self.alu_lift(ins, ops, "add")
    def lift_adc(self, ins, ops): self.alu_lift(ins, ops, "adc")
    def lift_sub(self, ins, ops): self.alu_lift(ins, ops, "sub")
    def lift_sbc(self, ins, ops): self.alu_lift(ins, ops, "sbc")
    def lift_and(self, ins, ops): self.alu_lift(ins, ops, "and")
    def lift_xor(self, ins, ops): self.alu_lift(ins, ops, "xor")
    def lift_or(self, ins, ops): self.alu_lift(ins, ops, "or")
    def lift_cp(self, ins, ops): self.alu_lift(ins, ops, "cp")

    def incdec(self, ins, ops, decrement):
        self.m1()
        target = ops[0]
        if target.mode == "reg" and target.reg in PAIRS:
            self.internal(2)
            register = PAIRS[target.reg]
            self.set_reg(register, op("sub" if decrement else "add", (self.reg(register), c(1, 16)), 16))
            return
        if target.mode == "reg":
            self.set8(target.reg, self.inc_dec(self.get8(target.reg), decrement))
            return
        if target.mode == "reg_indirect":
            address = self.bind(self.reg(PAIRS[target.reg]), "ea")
        elif target.mode == "indexed":
            self.arg()
            self.internal(5)
            address = self.bind(self.displaced(target.reg, target.disp), "ea")
        else:
            raise Unsupported(f"{self.address:04X}: {ins.text}")
        value = self.read(address)
        self.internal(1)
        self.write(address, self.inc_dec(value, decrement))

    def lift_inc(self, ins, ops): self.incdec(ins, ops, False)
    def lift_dec(self, ins, ops): self.incdec(ins, ops, True)

    def accumulator_rotate(self, name):
        self.m1()
        a = self.bind(self.reg(A), "a")
        carry = ir.zext(self.flag("cy"), 8)
        if name in ("rlca", "rla"):
            out = ir.bit(a, 7)
            low = ir.zext(out, 8) if name == "rlca" else carry
            result = op("or", (op("shl", (a, c(1, 8)), 8), low), 8)
        else:
            out = ir.bit(a, 0)
            high = op("shl", (ir.zext(out, 8) if name == "rrca" else carry, c(7, 8)), 8)
            result = op("or", (op("shr", (a, c(1, 8)), 8), high), 8)
        result = self.bind(result, "r")
        self.set_flag("h", c(0, 1))
        self.set_flag("n", c(0, 1))
        self.set_flag("cy", out)
        self.set_reg(A, result)

    def lift_rlca(self, ins, ops): self.accumulator_rotate("rlca")
    def lift_rrca(self, ins, ops): self.accumulator_rotate("rrca")
    def lift_rla(self, ins, ops): self.accumulator_rotate("rla")
    def lift_rra(self, ins, ops): self.accumulator_rotate("rra")

    def lift_cpl(self, ins, ops):
        self.m1()
        self.set_reg(A, op("not", (self.reg(A),), 8))
        self.set_flag("h", c(1, 1))
        self.set_flag("n", c(1, 1))

    def lift_scf(self, ins, ops):
        self.m1()
        self.set_flag("h", c(0, 1))
        self.set_flag("n", c(0, 1))
        self.set_flag("cy", c(1, 1))

    def lift_ccf(self, ins, ops):
        self.m1()
        carry = self.bind(ir.zext(self.flag("cy"), 8), "c")
        self.set_flag("h", ir.trunc(carry, 1))
        self.set_flag("n", c(0, 1))
        self.set_flag("cy", lnot(ir.trunc(carry, 1)))

    # the CB group
    def cb(self, ins, ops, name):
        self.m1()
        self.m1()
        target = ops[-1]
        bit = ops[0].value if name in ("bit", "set", "res") else None
        if target.mode == "reg":
            value = self.get8(target.reg)
            result = self.cb_operation(name, value, bit)
            if result is not None:
                self.set8(target.reg, result)
            return
        address = self.bind(self.reg(HL), "ea")
        value = self.read(address)
        self.internal(1)
        result = self.cb_operation(name, value, bit)
        if result is not None:
            self.write(address, result)

    def cb_operation(self, name, value, bit):
        if name in SHIFTS:
            return self.shift(name, value)
        if name == "bit":
            value = self.bind(value, "o")
            on = ir.bit(value, bit)
            self.set_flag("z", lnot(on))
            self.set_flag("ov", lnot(on))
            self.set_flag("s", on if bit == 7 else c(0, 1))
            self.set_flag("h", c(1, 1))
            self.set_flag("n", c(0, 1))
            return None
        mask = 1 << bit
        if name == "set":
            return op("or", (value, c(mask, 8)), 8)
        return op("and", (value, c(~mask, 8)), 8)

    def indexed_cb(self, ins):
        """DD CB d op / FD CB d op: CB an M1 (4 cycles), the displacement and the opcode read as memory (3 each)"""
        self.m1()               # CB
        self.arg()              # d
        self.arg()              # the operation
        self.internal(2)
        target = next(o for o in ins.operands if o.mode == "indexed")
        name = ins.mnemonic
        bit = ins.operands[0].value if name in ("bit", "set", "res") else None
        address = self.bind(self.displaced(target.reg, target.disp), "ea")
        value = self.read(address)
        self.internal(1)
        result = self.cb_operation(name, value, bit)
        if result is not None:
            result = self.bind(result, "r")
            self.write(address, result)
            if ins.copy_to:
                self.set8(ins.copy_to, result)

    def indexed_or_cb(self, ins, ops, name):
        if any(o.mode == "indexed" for o in ops):
            raise Unsupported(f"{self.address:04X}: {ins.text}")    # (indexed_cb takes these)
        self.cb(ins, ops, name)

    def lift_rlc(self, ins, ops): self.indexed_or_cb(ins, ops, "rlc")
    def lift_rrc(self, ins, ops): self.indexed_or_cb(ins, ops, "rrc")
    def lift_rl(self, ins, ops): self.indexed_or_cb(ins, ops, "rl")
    def lift_rr(self, ins, ops): self.indexed_or_cb(ins, ops, "rr")
    def lift_sla(self, ins, ops): self.indexed_or_cb(ins, ops, "sla")
    def lift_sra(self, ins, ops): self.indexed_or_cb(ins, ops, "sra")
    def lift_sll(self, ins, ops): self.indexed_or_cb(ins, ops, "sll")
    def lift_srl(self, ins, ops): self.indexed_or_cb(ins, ops, "srl")
    def lift_bit(self, ins, ops): self.indexed_or_cb(ins, ops, "bit")
    def lift_set(self, ins, ops): self.indexed_or_cb(ins, ops, "set")
    def lift_res(self, ins, ops): self.indexed_or_cb(ins, ops, "res")

    # block moves: one round (the instruction runs again while BC is not 0)
    def lift_ldir(self, ins, ops):
        self.block_move(1, True)

    def lift_lddr(self, ins, ops):
        self.block_move(-1, True)

    def lift_ldi(self, ins, ops):
        self.block_move(1, False)

    def lift_ldd(self, ins, ops):
        self.block_move(-1, False)

    def block_move(self, step, repeat):
        self.m1()
        self.m1()
        hl = self.bind(self.reg(HL), "hl")
        de = self.bind(self.reg(DE), "de")
        value = self.read(hl)
        self.write(de, value)
        self.internal(2)
        self.set_reg(HL, op("add", (hl, c(step, 16)), 16))
        self.set_reg(DE, op("add", (de, c(step, 16)), 16))
        bc = self.bind(op("sub", (self.reg(BC), c(1, 16)), 16), "bc")
        self.set_reg(BC, bc)
        self.set_flag("h", c(0, 1))
        self.set_flag("n", c(0, 1))
        self.set_flag("ov", op("ne", (bc, c(0, 16)), 1))
        if repeat:
            self.extras["taken"] = 5
            self.control = Control("branch", target=self.address, condition=op("ne", (bc, c(0, 16)), 1))

    # control
    def lift_nop(self, ins, ops):
        self.m1()

    def lift_halt(self, ins, ops):
        self.m1()
        self.control = Control("halt", target=self.next_address)

    def lift_di(self, ins, ops):
        self.m1()
        self.emit(ir.Effect("di"))

    def lift_ei(self, ins, ops):
        self.m1()
        self.emit(ir.Effect("ei"))

    def lift_im(self, ins, ops):
        self.m1()
        self.m1()
        self.emit(ir.Effect("im", (c(ops[0].value, 8),)))

    def lift_jp(self, ins, ops):
        self.m1()
        if ins.target is None:     # JP (HL) / (IX) / (IY)
            self.control = Control("jump", address=self.reg(PAIRS[ops[0].reg]))
            return
        self.arg()
        self.arg()
        if ins.condition:
            self.control = Control("branch", target=ins.target, condition=self.condition(ins.condition))
        else:
            self.control = Control("goto", target=ins.target)

    def lift_jr(self, ins, ops):
        self.m1()
        self.arg()
        if ins.condition:
            self.extras["taken"] = 5
            self.control = Control("branch", target=ins.target, condition=self.condition(ins.condition))
        else:
            self.internal(5)
            self.control = Control("goto", target=ins.target)

    def lift_djnz(self, ins, ops):
        self.m1()
        self.internal(1)
        self.arg()
        b = self.bind(op("sub", (self.get8("b"), c(1, 8)), 8), "b")
        self.set8("b", b)
        self.extras["taken"] = 5
        self.control = Control("branch", target=ins.target, condition=op("ne", (b, c(0, 8)), 1))

    def lift_call(self, ins, ops):
        self.m1()
        self.arg()
        self.arg()
        if ins.condition:
            self.taken = True
            self.internal(1)
            self.push16(c(self.next_address, 16))
            self.taken = False
            self.extras["taken"] = 7
            self.cycle -= 7
            self.control = Control("call_if", target=ins.target, condition=self.condition(ins.condition))
            return
        self.internal(1)
        self.push16(c(self.next_address, 16))
        self.control = Control("call", target=ins.target)

    def lift_rst(self, ins, ops):
        self.m1()
        self.internal(1)
        self.push16(c(self.next_address, 16))
        self.control = Control("call", target=ins.target)

    def lift_ret(self, ins, ops):
        self.m1()
        if ins.condition:
            self.internal(1)
            self.taken = True
            start = self.cycle
            address = self.pop16()
            self.taken = False
            self.extras["taken"] = self.cycle - start
            self.cycle = start
            self.control = Control("return_if", address=address, condition=self.condition(ins.condition))
            return
        self.control = Control("return", address=self.pop16())


class IrqEntry(Lifter):
    """The IRQ's entry in interrupt mode 1 (z80.lst's take_interrupt): IFF1 and IFF2 cleared, the
    acknowledge cycle, the PC pushed, on to 0038"""

    def __init__(self, address, prefix="irq"):
        self.instruction = None
        self.read_byte = lambda a: 0
        self.address = address
        self.next_address = address
        self.statements = []
        self.accesses = []
        self.taken_statements = []
        self.taken_accesses = []
        self.control = Control()
        self.cycle = 0
        self.extras = {}
        self.prefix = prefix
        self.count = 0
        self.pc = address
        self.taken = False
        self.emit(ir.Effect("di"))
        self.internal(7)          # the acknowledge cycle and the core's "+ 2", "+ 5"
        self.push16(c(address, 16))
        self.control = Control("goto", target=0x0038)


def lift(instruction, read, prefix="t"):
    return Lifter(instruction, read, prefix)
