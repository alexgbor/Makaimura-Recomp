"""
Name:        lift.py
Description: translates decoded 6809 instructions into the core's IR (tools/core/v60/ir.py), doing what MAME's
             MC6809 core does: its bus cycles in its order, each at its cycle, and every flag as it sets them
Version:     0.1.0
Created:     19-11-2026

license:BSD-3-Clause
After MAME's src/devices/cpu/m6809 (BSD-3-Clause, Nathan Woods and others): m6809.lst and base6x09.lst, the
core's microcode, where every '@' line is a bus cycle, m6809inl.h's set_flags, rotate_left/right, daa and mul,
and m6809.cpp's registers of TFR and EXG.

lift(instruction) gives a Lifter: its statements (IR), its control (how execution goes on), its cycles, and its
bus cycles in order (accesses): each an Access of kind
    O   the opcode space: the opcode, its operands, the dummy reads of the next byte (dummy_read_opcode_arg) and
        the VMA cycles (dummy_vma, a read of FFFF): their address is known here, the IR has none of them;
    R   a data read: an ir.Load in the statements, in the same order;
    W   a write: an ir.Store, in the same order.
An access with when="taken" happens only when the branch is taken (a long conditional branch's extra cycle,
extras["taken"] = 1). Reads that only throw their value away (CLR's of its operand, PSH's of the stack's top,
PUL's and RTS's after the last byte) are Loads bound to a temporary nothing reads, as MAME's core makes them.

Registers: A, B, DP (8 bits), X, Y, U, S (16): indexes 0-6; D is A:B. The flags are CC's bits as the core's
IR names them, e f h i s (N) z ov (V) cy (C). The PC is the instruction's own address: what reads it (PC-relative
operands, TFR and EXG from PC, a call's return address) gets its value here, at the point the microcode has it.

Control: next; goto (BRA, LBRA, JMP extended); branch (Bcc, LBcc: condition, target); call (BSR, LBSR, JSR: target,
or a computed address); jump (JMP through an address computed, TFR or EXG into PC, PULS or PULU with PC but S's
return, ...); return (RTS, PULS with PC: the popped address); rti (RTI: the popped address); wait (SYNC, CWAI:
the runtime waits for the interrupt; CWAI pushes the whole state first, and takes the interrupt without pushing it
again). Undocumented opcodes (MAME runs them; the disassembler calls them illegal), SWI, SWI2 and SWI3 are not
lifted: the program has none.
"""
from dataclasses import dataclass, field
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "core"))

from v60 import ir  # noqa: E402  (the core's IR)

A, B, DP, X, Y, U, S = range(7)
REGISTERS = ("A", "B", "DP", "X", "Y", "U", "S")
WIDTHS = (8, 8, 8, 16, 16, 16, 16)
INDEX = {"X": X, "Y": Y, "U": U, "S": S}
# CC's bits, from bit 0
CC_FLAGS = ("cy", "ov", "z", "s", "i", "h", "f", "e")
CC_E, CC_F, CC_H, CC_I, CC_N, CC_Z, CC_V, CC_C = 0x80, 0x40, 0x20, 0x10, 0x08, 0x04, 0x02, 0x01
VECTOR_IRQ = 0xFFF8


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
    kind: str           # O, R or W
    cycle: int          # from 0, the instruction's first cycle
    address: int = None  # an O access's
    when: str = None     # "taken": only when the branch is taken


def c8(value):
    return ir.Const(value & 0xFF, 8)


def c16(value):
    return ir.Const(value & 0xFFFF, 16)


def c32(value):
    return ir.Const(value & 0xFFFFFFFF, 32)


def op(name, args, bits):
    return ir.Op(name, tuple(args), bits)


def bit_of(value, number):
    return ir.bit(value, number)


def flag(name):
    return ir.Flag(name)


def lnot(value):
    return op("eq", (value, ir.Const(0, value.bits)), 1)


def land(a, b):
    return op("and", (a, b), 1)


def lor(a, b):
    return op("or", (a, b), 1)


class Lifter:
    def __init__(self, instruction, prefix="t"):
        self.instruction = instruction
        self.address = instruction.address
        self.next_address = (instruction.address + instruction.size) & 0xFFFF
        self.raw = instruction.raw
        self.statements = []
        self.accesses = []
        self.control = Control()
        self.cycle = 0
        self.extras = {}
        self.prefix = prefix
        self.count = 0
        self.pc = instruction.address    # the core's PC as its fetches move it
        self.position = 0                # the next byte of the instruction to fetch
        self.mode = None                 # the operand: "ea", "imm", "A", "B"
        self.ea = None
        self.lift()

    @property
    def cycles(self):
        return self.cycle

    # ---- the bus ----

    def fetch(self):
        """read_opcode / read_opcode_arg: the instruction's next byte"""
        value = self.raw[self.position]
        self.accesses.append(Access("O", self.cycle, self.pc))
        self.cycle += 1
        self.position += 1
        self.pc = (self.pc + 1) & 0xFFFF
        return value

    def dummy_arg(self, delta=0):
        """dummy_read_opcode_arg: a read of the byte after the instruction (or PC + delta)"""
        self.accesses.append(Access("O", self.cycle, (self.pc + delta) & 0xFFFF))
        self.cycle += 1

    def vma(self, count, when=None):
        """dummy_vma: cycles with no access of the program's: the core reads FFFF in the opcode space"""
        for _ in range(count):
            self.accesses.append(Access("O", self.cycle, 0xFFFF, when))
            if when is None:
                self.cycle += 1

    def temp(self, bits, name=None):
        self.count += 1
        return ir.Temp(f"{self.prefix}{name or 'v'}{self.count}", bits)

    def bind(self, value, name=None, keep=False):
        """'value' as a temporary of its own (a register too with keep: its value now, before it changes)"""
        if isinstance(value, (ir.Const, ir.Temp)) or (not keep and isinstance(value, (ir.Reg, ir.Flag))):
            return value
        temp = self.temp(value.bits, name)
        self.statements.append(ir.SetTemp(temp, value))
        return temp

    def read(self, address, name="m"):
        """read_memory: a data read"""
        temp = self.temp(8, name)
        self.statements.append(ir.SetTemp(temp, ir.Load(address, 8)))
        self.accesses.append(Access("R", self.cycle))
        self.cycle += 1
        return temp

    def write(self, address, value):
        """write_memory"""
        self.statements.append(ir.Store(address, value, 8))
        self.accesses.append(Access("W", self.cycle))
        self.cycle += 1

    # ---- registers and flags ----

    def reg(self, index):
        return ir.Reg(index, WIDTHS[index])

    def set_reg(self, index, value):
        self.statements.append(ir.SetReg(index, value))

    def d(self):
        return op("or", (op("shl", (ir.zext(self.reg(A), 16), c8(8)), 16), ir.zext(self.reg(B), 16)), 16)

    def set_d(self, value):
        value = self.bind(value, "d")
        self.set_reg(A, ir.trunc(op("shr", (value, c8(8)), 16), 8))
        self.set_reg(B, ir.trunc(value, 8))

    def reg16(self, name):
        if name == "D":
            return self.d()
        return self.reg(INDEX[name])

    def set_reg16(self, name, value):
        if name == "D":
            self.set_d(value)
        else:
            self.set_reg(INDEX[name], value)

    def set_flag(self, name, value):
        self.statements.append(ir.SetFlag(name, value))

    def cc(self):
        value = None
        for number, name in enumerate(CC_FLAGS):
            part = ir.zext(flag(name), 8)
            if number:
                part = op("shl", (part, c8(number)), 8)
            value = part if value is None else op("or", (value, part), 8)
        return value

    def set_cc(self, value):
        value = self.bind(value, "cc")
        for number, name in enumerate(CC_FLAGS):
            self.set_flag(name, bit_of(value, number))

    # ---- set_flags (m6809inl.h): a and b the operands, r the result as the core's uint32_t ----

    def flags(self, mask, a, b, r, width):
        """set_flags<T>(mask, a, b, r) with T of 'width' bits; a, b of 'width' bits, r of 32"""
        top = width - 1
        r = self.bind(r, "r")    # (once: what r reads, the carry among them, before the flags change)
        a32, b32 = ir.zext(a, 32), ir.zext(b, 32)
        if mask & CC_H:
            self.set_flag("h", bit_of(op("xor", (op("xor", (a32, b32), 32), r), 32), 4))
        if mask & CC_N:
            self.set_flag("s", bit_of(r, top))
        if mask & CC_Z:
            self.set_flag("z", op("eq", (ir.trunc(r, width), ir.Const(0, width)), 1))
        if mask & CC_V:
            mixed = op("xor", (op("xor", (op("xor", (a32, b32), 32), r), 32), op("shr", (r, c8(1)), 32)), 32)
            self.set_flag("ov", bit_of(mixed, top))
        if mask & CC_C:
            self.set_flag("cy", bit_of(r, width))
        return ir.trunc(r, width)

    def flags_of(self, mask, value):
        """set_flags<T>(mask, r): a = 0, b = r"""
        width = value.bits
        return self.flags(mask, ir.Const(0, width), value, ir.zext(value, 32), width)

    # ---- addressing modes ----

    def direct(self):
        low = self.fetch()
        self.ea = op("or", (op("shl", (ir.zext(self.reg(DP), 16), c8(8)), 16), c16(low)), 16)
        self.mode = "ea"
        self.vma(1)

    def extended(self):
        high = self.fetch()
        low = self.fetch()
        self.ea = c16(high << 8 | low)
        self.mode = "ea"
        self.vma(1)

    def indexed(self):
        post = self.fetch()
        register = INDEX[("X", "Y", "U", "S")[(post >> 5) & 3]]
        r = self.reg(register)
        if not post & 0x80:
            offset = (post & 0x0F) - (0x10 if post & 0x10 else 0)
            address = op("add", (r, c16(offset)), 16)
            self.dummy_arg(0)
            self.vma(1)
        else:
            kind = post & 0x0F
            if kind == 0x0:
                address = self.bind(r, "ea", keep=True)
                self.set_reg(register, op("add", (r, c16(1)), 16))
                self.dummy_arg(0)
                self.vma(2)
            elif kind == 0x1:
                address = self.bind(r, "ea", keep=True)
                self.set_reg(register, op("add", (r, c16(2)), 16))
                self.dummy_arg(0)
                self.vma(3)
            elif kind == 0x2:
                address = self.bind(op("sub", (r, c16(1)), 16), "ea")
                self.set_reg(register, address)
                self.dummy_arg(0)
                self.vma(2)
            elif kind == 0x3:
                address = self.bind(op("sub", (r, c16(2)), 16), "ea")
                self.set_reg(register, address)
                self.dummy_arg(0)
                self.vma(3)
            elif kind == 0x4:
                address = r
                self.dummy_arg(0)
            elif kind in (0x5, 0x6):
                accumulator = self.reg(B if kind == 0x5 else A)
                address = op("add", (r, ir.sext(accumulator, 16)), 16)
                self.dummy_arg(0)
                self.vma(1)
            elif kind == 0x8:
                offset = self.fetch()
                address = op("add", (r, c16(offset - (0x100 if offset & 0x80 else 0))), 16)
                self.dummy_arg(0)
            elif kind == 0x9:
                high = self.fetch()
                low = self.fetch()
                address = op("add", (r, c16(high << 8 | low)), 16)
                self.vma(3)
            elif kind == 0xB:
                address = op("add", (r, self.d()), 16)
                self.dummy_arg(0)
                self.dummy_arg(1)
                self.vma(3)
            elif kind == 0xC:
                offset = self.fetch()
                address = c16(self.pc + offset - (0x100 if offset & 0x80 else 0))
                self.vma(1)
            elif kind == 0xD:
                high = self.fetch()
                low = self.fetch()
                address = c16(self.pc + (high << 8 | low))
                self.vma(4)
            elif kind == 0xF and post & 0x10:
                high = self.fetch()
                low = self.fetch()
                address = c16(high << 8 | low)
                self.vma(1)
            else:
                raise Unsupported(f"{self.address:04X}: indexed postbyte {post:02X}")
            if post & 0x10:
                address = self.bind(address, "ea")
                high = self.read(address, "ih")
                low = self.read(op("add", (address, c16(1)), 16), "il")
                self.vma(1)
                address = op("or", (op("shl", (ir.zext(high, 16), c8(8)), 16), ir.zext(low, 16)), 16)
        self.ea = self.bind(address, "ea", keep=True)    # (the address as it is now: JSR ,S pushes onto S after)
        self.mode = "ea"

    def operand_mode(self, mode):
        if mode == "direct":
            self.direct()
        elif mode == "extended":
            self.extended()
        elif mode == "indexed":
            self.indexed()
        elif mode == "immediate":
            self.mode = "imm"
        else:
            raise Unsupported(f"{self.address:04X}: mode {mode}")

    def read_operand(self, ordinal=0):
        """read_operand(ordinal): the operand's byte (an immediate is fetched, a register is read)"""
        if self.mode == "imm":
            return c8(self.fetch())
        if self.mode == "ea":
            address = self.ea if ordinal == 0 else op("add", (self.ea, c16(ordinal)), 16)
            return self.read(address)
        if self.mode in ("A", "B"):
            return self.reg(A if self.mode == "A" else B)
        raise Unsupported(f"{self.address:04X}: no operand")

    def read_operand16(self):
        high = self.read_operand(0)
        low = self.read_operand(1)
        if isinstance(high, ir.Const) and isinstance(low, ir.Const):
            return c16(high.value << 8 | low.value)
        return self.bind(op("or", (op("shl", (ir.zext(high, 16), c8(8)), 16), ir.zext(low, 16)), 16), "w")

    def write_operand(self, value, ordinal=0):
        if self.mode == "ea":
            address = self.ea if ordinal == 0 else op("add", (self.ea, c16(ordinal)), 16)
            self.write(address, value)
        elif self.mode in ("A", "B"):
            self.set_reg(A if self.mode == "A" else B, value)
        elif self.mode == "imm":
            pass
        else:
            raise Unsupported(f"{self.address:04X}: no operand to write")

    # ---- the stack ----

    def push(self, stack, value):
        """write_memory(--stack, value)"""
        address = self.bind(op("sub", (self.reg(stack), c16(1)), 16), "sp")
        self.set_reg(stack, address)
        self.write(address, value)

    def pull(self, stack, name="p"):
        """read_memory(stack++)"""
        address = self.bind(self.reg(stack), "sp")
        value = self.read(address, name)
        self.set_reg(stack, op("add", (address, c16(1)), 16))
        return value

    def push_registers(self, stack, mask, pc):
        other = U if stack == S else S
        if mask & 0x80:
            self.push(stack, c8(pc))
            self.push(stack, c8(pc >> 8))
        if mask & 0x40:
            value = self.bind(self.reg(other), "o")
            self.push(stack, ir.trunc(value, 8))
            self.push(stack, ir.trunc(op("shr", (value, c8(8)), 16), 8))
        for bit, register in ((0x20, Y), (0x10, X)):
            if mask & bit:
                value = self.bind(self.reg(register), "o")
                self.push(stack, ir.trunc(value, 8))
                self.push(stack, ir.trunc(op("shr", (value, c8(8)), 16), 8))
        if mask & 0x08:
            self.push(stack, self.reg(DP))
        if mask & 0x04:
            self.push(stack, self.reg(B))
        if mask & 0x02:
            self.push(stack, self.reg(A))
        if mask & 0x01:
            self.push(stack, self.bind(self.cc(), "cc"))

    def pull_registers(self, stack, mask):
        """PULL_REGISTERS: the registers in their order, then the read of the stack's top; the PC pulled, if any"""
        other = U if stack == S else S
        if mask & 0x01:
            self.set_cc(self.pull(stack, "cc"))
        if mask & 0x02:
            self.set_reg(A, self.pull(stack))
        if mask & 0x04:
            self.set_reg(B, self.pull(stack))
        if mask & 0x08:
            self.set_reg(DP, self.pull(stack))
        for bit, register in ((0x10, X), (0x20, Y), (0x40, other)):
            if mask & bit:
                high = self.pull(stack)
                low = self.pull(stack)
                self.set_reg(register, op("or", (op("shl", (ir.zext(high, 16), c8(8)), 16), ir.zext(low, 16)), 16))
        pc = None
        if mask & 0x80:
            high = self.pull(stack, "pch")
            low = self.pull(stack, "pcl")
            pc = self.bind(op("or", (op("shl", (ir.zext(high, 16), c8(8)), 16), ir.zext(low, 16)), 16), "pc")
        self.read(self.reg(stack), "x")
        return pc

    def subroutine(self, target_address):
        """GOTO_SUBROUTINE: the return address pushed, low byte first"""
        self.push(S, c8(self.pc))
        self.push(S, c8(self.pc >> 8))

    # ---- conditions ----

    def condition(self, code):
        """The branch condition of the low nibble of a branch opcode (m6809.h's cond_*)"""
        c, z, v, n = flag("cy"), flag("z"), flag("ov"), flag("s")
        hi = lnot(lor(c, z))
        ge = op("eq", (n, v), 1)
        base = (ir.Const(1, 1), hi, lnot(c), lnot(z), lnot(v), lnot(n), ge, land(ge, lnot(z)))[code >> 1]
        if code & 1:
            return ir.Const(0, 1) if code == 1 else lnot(base)
        return base

    # ---- the instructions ----

    def lift(self):
        first = self.fetch()
        page = 0
        opcode = first
        if first in (0x10, 0x11):
            page = first
            opcode = self.fetch()
        instruction = self.instruction
        mnemonic = instruction.mnemonic
        if mnemonic == "ILLEGAL":
            raise Unsupported(f"{self.address:04X}: illegal opcode")
        if page == 0x10 and 0x20 <= opcode <= 0x2F:
            return self.long_branch(opcode)
        if page:
            return self.page23(page, opcode)
        high, low = opcode >> 4, opcode & 0x0F
        if high in (0x0, 0x4, 0x5, 0x6, 0x7):
            return self.read_modify_write(high, low)
        if high == 0x1:
            return self.page0_1x(opcode)
        if high == 0x2:
            return self.short_branch(opcode)
        if high == 0x3:
            return self.page0_3x(opcode)
        return self.accumulator_group(opcode)

    def read_modify_write(self, high, low):
        if high == 0x0:
            self.direct()
        elif high == 0x4:
            self.mode = "A"
        elif high == 0x5:
            self.mode = "B"
        elif high == 0x6:
            self.indexed()
        else:
            self.extended()
        name = self.instruction.mnemonic.rstrip("AB") if high in (0x4, 0x5) else self.instruction.mnemonic
        if name == "JMP":
            if isinstance(self.ea, ir.Const):
                self.control = Control("goto", target=self.ea.value)
            else:
                self.control = Control("jump", address=self.ea)
            return
        if name == "CLR":
            if self.mode == "ea":
                self.read_operand()
            for flag_name, value in (("s", 0), ("z", 1), ("ov", 0), ("cy", 0)):
                self.set_flag(flag_name, ir.Const(value, 1))
            self.dummy_arg(0)
            self.write_operand(c8(0))
            return
        value = self.read_operand()
        value = self.bind(value, "t")
        if name == "TST":
            self.flags_of(CC_N | CC_Z | CC_V, value)
            self.vma(1)
            if self.mode == "ea":
                self.vma(1)
            return
        v32 = ir.zext(value, 32)
        if name == "NEG":
            result = self.flags(CC_N | CC_Z | CC_V | CC_C, c8(0), value, op("neg", (v32,), 32), 8)
        elif name == "COM":
            self.set_flag("ov", ir.Const(0, 1))
            self.set_flag("cy", ir.Const(1, 1))
            result = self.flags_of(CC_N | CC_Z, op("not", (value,), 8))
        elif name == "LSR":
            self.set_flag("cy", bit_of(value, 0))
            result = self.flags_of(CC_N | CC_Z, op("shr", (value, c8(1)), 8))
        elif name == "ROR":
            rotated = op("or", (op("shr", (value, c8(1)), 8), op("shl", (ir.zext(flag("cy"), 8), c8(7)), 8)), 8)
            rotated = self.bind(rotated, "r")
            self.set_flag("cy", bit_of(value, 0))
            result = self.flags_of(CC_N | CC_Z, rotated)
        elif name == "ASR":
            self.set_flag("cy", bit_of(value, 0))
            result = self.flags_of(CC_N | CC_Z, op("sar", (value, c8(1)), 8))
        elif name == "ASL":
            result = self.flags(CC_N | CC_Z | CC_V | CC_C, value, value, op("shl", (v32, c8(1)), 32), 8)
        elif name == "ROL":
            rotated = self.bind(op("or", (op("shl", (v32, c8(1)), 32), ir.zext(flag("cy"), 32)), 32), "r")
            self.set_flag("cy", bit_of(value, 7))
            result = self.flags(CC_N | CC_Z | CC_V, value, value, rotated, 8)
        elif name == "DEC":
            result = self.flags(CC_N | CC_Z | CC_V, value, c8(1), op("sub", (v32, c32(1)), 32), 8)
        elif name == "INC":
            result = self.flags(CC_N | CC_Z | CC_V, value, c8(1), op("add", (v32, c32(1)), 32), 8)
        else:
            raise Unsupported(f"{self.address:04X}: {self.instruction.mnemonic}")
        result = self.bind(result, "r")
        self.dummy_arg(0)
        self.write_operand(result)

    def page0_1x(self, opcode):
        name = self.instruction.mnemonic
        if opcode == 0x12:    # NOP
            self.dummy_arg(0)
        elif opcode == 0x13:  # SYNC
            self.dummy_arg(0)
            self.control = Control("wait", target=self.next_address)
            # (then the wait, and one more cycle: the runtime's)
            self.extras["sync"] = 1
        elif opcode == 0x16:  # LBRA
            self.fetch()
            self.fetch()
            self.vma(1)
            self.vma(1)
            self.control = Control("goto", target=self.instruction.target)
        elif opcode == 0x17:  # LBSR
            self.fetch()
            self.fetch()
            self.vma(4)
            self.subroutine(self.instruction.target)
            self.control = Control("call", target=self.instruction.target)
        elif opcode == 0x19:  # DAA
            self.daa()
            self.dummy_arg(0)
        elif opcode in (0x1A, 0x1C):    # ORCC, ANDCC
            value = c8(self.fetch())
            combined = op("or" if opcode == 0x1A else "and", (self.cc(), value), 8)
            self.set_cc(combined)
            self.dummy_arg(0)
        elif opcode == 0x1D:  # SEX
            self.set_d(self.flags_of(CC_N | CC_Z, ir.sext(self.reg(B), 16)))
            self.dummy_arg(0)
        elif opcode == 0x1E:  # EXG
            self.exchange(self.fetch())
            self.vma(6)
        elif opcode == 0x1F:  # TFR
            self.transfer(self.fetch())
            self.vma(4)
        else:
            raise Unsupported(f"{self.address:04X}: {name}")

    def short_branch(self, opcode):
        self.fetch()
        self.vma(1)
        target = self.instruction.target
        if opcode == 0x20:
            self.control = Control("goto", target=target)
        elif opcode == 0x21:
            self.control = Control("next")
        else:
            self.control = Control("branch", target=target, condition=self.condition(opcode & 0x0F))

    def long_branch(self, opcode):
        self.fetch()
        self.fetch()
        self.vma(1)
        target = self.instruction.target
        if opcode == 0x20:
            self.vma(1)
            self.control = Control("goto", target=target)
        elif opcode == 0x21:
            self.control = Control("next")
        else:
            self.vma(1, when="taken")
            self.extras["taken"] = 1
            self.control = Control("branch", target=target, condition=self.condition(opcode & 0x0F))

    def page0_3x(self, opcode):
        name = self.instruction.mnemonic
        if opcode in (0x30, 0x31, 0x32, 0x33):    # LEAX, LEAY, LEAS, LEAU
            register = (X, Y, S, U)[opcode & 3]
            self.indexed()
            if opcode in (0x30, 0x31):
                self.set_flag("z", op("eq", (self.ea, c16(0)), 1))
            self.set_reg(register, self.ea)
            self.vma(1)
        elif opcode in (0x34, 0x36):    # PSHS, PSHU
            mask = self.fetch()
            stack = S if opcode == 0x34 else U
            self.vma(2)
            self.read(self.reg(stack), "x")
            self.push_registers(stack, mask, self.next_address)
        elif opcode in (0x35, 0x37):    # PULS, PULU
            mask = self.fetch()
            stack = S if opcode == 0x35 else U
            self.vma(2)
            pc = self.pull_registers(stack, mask)
            if pc is not None:
                self.control = Control("return" if stack == S else "jump", address=pc)
        elif opcode == 0x39:    # RTS
            self.dummy_arg(0)
            self.control = Control("return", address=self.pull_registers(S, 0x80))
        elif opcode == 0x3A:    # ABX
            self.set_reg(X, op("add", (self.reg(X), ir.zext(self.reg(B), 16)), 16))
            self.dummy_arg(0)
            self.vma(1)
        elif opcode == 0x3B:    # RTI: the whole state (the program's interrupts are all IRQs: E set)
            self.dummy_arg(0)
            self.set_cc(self.pull(S, "cc"))
            self.statements.append(ir.Effect("rti_entire_state", (flag("e"),)))
            self.control = Control("rti", address=self.pull_registers(S, 0xFE))
        elif opcode == 0x3C:    # CWAI
            value = c8(self.fetch())
            self.set_cc(op("and", (self.cc(), value), 8))
            self.dummy_arg(0)
            self.vma(1)
            self.set_flag("e", ir.Const(1, 1))
            self.push_registers(S, 0xFF, self.next_address)
            self.control = Control("wait", target=self.next_address)
            self.extras["cwai"] = 1
        elif opcode == 0x3D:    # MUL
            product = self.bind(op("mul", (ir.zext(self.reg(A), 16), ir.zext(self.reg(B), 16)), 16), "mul")
            self.set_d(product)
            self.set_flag("z", op("eq", (product, c16(0)), 1))
            self.set_flag("cy", bit_of(product, 7))
            self.dummy_arg(0)
            self.vma(9)
        else:
            raise Unsupported(f"{self.address:04X}: {name}")

    def daa(self):
        a = self.bind(self.reg(A), "a")
        msn = op("and", (a, c8(0xF0)), 8)
        lsn = op("and", (a, c8(0x0F)), 8)
        low_adjust = lor(op("ult", (c8(9), lsn), 1), flag("h"))
        high_adjust = lor(lor(land(op("ult", (c8(0x80), msn), 1), op("ult", (c8(9), lsn), 1)),
                              op("ult", (c8(0x90), msn), 1)), flag("cy"))
        cf = op("or", (op("select", (low_adjust, c16(0x06), c16(0)), 16),
                       op("select", (high_adjust, c16(0x60), c16(0)), 16)), 16)
        t = self.bind(op("add", (ir.zext(a, 16), cf), 16), "daa")
        self.set_flag("ov", ir.Const(0, 1))
        self.set_flag("cy", lor(flag("cy"), bit_of(t, 8)))
        self.set_reg(A, self.flags_of(CC_N | CC_Z, ir.trunc(t, 8)))

    # TFR and EXG's registers (m6809.cpp): reads give 16 bits
    def read_816(self, code):
        code &= 0x0F
        if code == 0:
            return self.d()
        if code in (1, 2, 3, 4):
            return self.reg((X, Y, U, S)[code - 1])
        if code == 5:
            return c16(self.next_address)
        if code in (8, 9):
            return op("or", (c16(0xFF00), ir.zext(self.reg(A if code == 8 else B), 16)), 16)
        if code == 10:
            cc = ir.zext(self.bind(self.cc(), "cc"), 16)
            return op("or", (op("shl", (cc, c8(8)), 16), cc), 16)
        if code == 11:
            dp = ir.zext(self.reg(DP), 16)
            return op("or", (op("shl", (dp, c8(8)), 16), dp), 16)
        return c16(0xFFFF)

    def read_168(self, code):
        code &= 0x0F
        if code == 10:
            return op("or", (c16(0xFF00), ir.zext(self.bind(self.cc(), "cc"), 16)), 16)
        if code == 11:
            return op("or", (c16(0xFF00), ir.zext(self.reg(DP), 16)), 16)
        return self.read_816(code)

    def write_register(self, code, value):
        """write_exgtfr_register; a write of PC is the instruction's jump"""
        code &= 0x0F
        if code == 0:
            self.set_d(value)
        elif code in (1, 2, 3, 4):
            self.set_reg((X, Y, U, S)[code - 1], value)
        elif code == 5:
            self.control = Control("jump", address=self.bind(value, "pc"))
        elif code in (8, 9):
            self.set_reg(A if code == 8 else B, ir.trunc(value, 8))
        elif code == 10:
            self.set_cc(ir.trunc(value, 8))
        elif code == 11:
            self.set_reg(DP, ir.trunc(value, 8))

    def transfer(self, post):
        value = self.bind(self.read_816(post >> 4), "tfr")
        self.write_register(post, value)

    def exchange(self, post):
        if post & 0x80:
            first = self.bind(self.read_816(post >> 4), "x1", keep=True)
            second = self.bind(self.read_816(post), "x2", keep=True)
        else:
            first = self.bind(self.read_168(post >> 4), "x1", keep=True)
            second = self.bind(self.read_168(post), "x2", keep=True)
        self.write_register(post, first)
        self.write_register(post >> 4, second)

    def accumulator_group(self, opcode):
        high, low = opcode >> 4, opcode & 0x0F
        accumulator = A if high < 0xC else B
        mode = ("immediate", "direct", "indexed", "extended")[(high >> 0) & 3]
        if opcode == 0x8D:    # BSR
            self.fetch()
            self.vma(3)
            self.subroutine(self.instruction.target)
            self.control = Control("call", target=self.instruction.target)
            return
        name = self.instruction.mnemonic
        if low == 0x3:
            self.op16("SUB" if high < 0xC else "ADD", "D", mode)
        elif low == 0xC:
            if high < 0xC:
                self.op16("CMP", "X", mode)
            else:
                self.load16("D", mode)
        elif low == 0xD:
            if high < 0xC:
                if mode == "immediate":
                    raise Unsupported(f"{self.address:04X}: {name}")
                self.operand_mode(mode)
                self.dummy_arg(0)
                self.vma(1)
                self.subroutine(None)
                if isinstance(self.ea, ir.Const):
                    self.control = Control("call", target=self.ea.value)
                else:
                    self.control = Control("call", address=self.ea)
            else:
                self.store16("D", mode)
        elif low == 0xE:
            self.load16("X" if high < 0xC else "U", mode)
        elif low == 0xF:
            self.store16("X" if high < 0xC else "U", mode)
        else:
            self.op8(low, accumulator, mode)

    def op8(self, low, accumulator, mode):
        if low == 0x7 and mode == "immediate":
            raise Unsupported(f"{self.address:04X}: {self.instruction.mnemonic}")
        self.operand_mode(mode)
        register = self.reg(accumulator)
        if low == 0x7:    # ST8
            value = self.bind(register, "st")
            self.flags_of(CC_N | CC_Z | CC_V, value)
            self.write(self.ea, value)
            return
        operand = self.bind(self.read_operand(), "o")
        r32, o32 = ir.zext(register, 32), ir.zext(operand, 32)
        carry = ir.zext(flag("cy"), 32)
        if low == 0x0:      # SUB
            result = self.flags(CC_N | CC_Z | CC_V | CC_C, register, operand, op("sub", (r32, o32), 32), 8)
        elif low == 0x1:    # CMP
            self.flags(CC_N | CC_Z | CC_V | CC_C, register, operand, op("sub", (r32, o32), 32), 8)
            return
        elif low == 0x2:    # SBC
            result = self.flags(CC_N | CC_Z | CC_V | CC_C, register, operand,
                                op("sub", (op("sub", (r32, o32), 32), carry), 32), 8)
        elif low in (0x4, 0x5, 0x8, 0xA):    # AND, BIT, EOR, OR
            self.set_flag("ov", ir.Const(0, 1))
            name = {0x4: "and", 0x5: "and", 0x8: "xor", 0xA: "or"}[low]
            value = op(name, (register, operand), 8)
            result = self.flags(CC_N | CC_Z, c8(0), register, ir.zext(value, 32), 8)
            if low == 0x5:
                return
        elif low == 0x6:    # LD
            result = self.flags_of(CC_N | CC_Z | CC_V, operand)
        elif low == 0x9:    # ADC
            result = self.flags(CC_H | CC_N | CC_Z | CC_V | CC_C, register, operand,
                                op("add", (op("add", (r32, o32), 32), carry), 32), 8)
        elif low == 0xB:    # ADD
            result = self.flags(CC_H | CC_N | CC_Z | CC_V | CC_C, register, operand, op("add", (r32, o32), 32), 8)
        else:
            raise Unsupported(f"{self.address:04X}: {self.instruction.mnemonic}")
        self.set_reg(accumulator, self.bind(result, "r"))

    def op16(self, name, register, mode):
        self.operand_mode(mode)
        operand = self.read_operand16()
        value = self.bind(self.reg16(register), "a")
        a32, o32 = ir.zext(value, 32), ir.zext(operand, 32)
        result = op("add" if name == "ADD" else "sub", (a32, o32), 32)
        result = self.flags(CC_N | CC_Z | CC_V | CC_C, value, operand, result, 16)
        if name != "CMP":
            self.set_reg16(register, self.bind(result, "r"))
        self.dummy_arg(0)

    def load16(self, register, mode):
        self.operand_mode(mode)
        value = self.read_operand16()
        self.set_reg16(register, value)
        self.flags_of(CC_N | CC_Z | CC_V, value)

    def store16(self, register, mode):
        if mode == "immediate":
            raise Unsupported(f"{self.address:04X}: {self.instruction.mnemonic}")
        self.operand_mode(mode)
        value = self.bind(self.reg16(register), "st")
        self.write_operand(ir.trunc(op("shr", (value, c8(8)), 16), 8), 0)
        self.write_operand(ir.trunc(value, 8), 1)
        self.flags_of(CC_N | CC_Z | CC_V, value)

    def page23(self, page, opcode):
        high, low = opcode >> 4, opcode & 0x0F
        name = self.instruction.mnemonic
        mode = ("immediate", "direct", "indexed", "extended")[high & 3] if high >= 0x8 else None
        if page == 0x10:
            if low == 0x3 and high < 0xC:
                return self.op16("CMP", "D", mode)
            if low == 0xC and high < 0xC:
                return self.op16("CMP", "Y", mode)
            if low == 0xE:
                return self.load16("Y" if high < 0xC else "S", mode)
            if low == 0xF:
                return self.store16("Y" if high < 0xC else "S", mode)
        else:
            if low == 0x3 and high < 0xC:
                return self.op16("CMP", "U", mode)
            if low == 0xC and high < 0xC:
                return self.op16("CMP", "S", mode)
        raise Unsupported(f"{self.address:04X}: {name}")


class IrqEntry(Lifter):
    """The IRQ's entry (m6809.lst's IRQ and INTERRUPT_VECTOR) where the CPU would start the instruction at
    'address': the whole state pushed, I set, the PC from FFF8"""

    def __init__(self, address, prefix="irq"):
        self.instruction = None
        self.address = address
        self.next_address = address
        self.raw = b""
        self.statements = []
        self.accesses = []
        self.control = Control()
        self.cycle = 0
        self.extras = {}
        self.prefix = prefix
        self.count = 0
        self.pc = address
        self.position = 0
        self.mode = None
        self.ea = None
        self.dummy_arg(0)
        self.dummy_arg(0)
        self.vma(1)
        self.set_flag("e", ir.Const(1, 1))
        self.push_registers(S, 0xFF, address)
        self.set_flag("i", ir.Const(1, 1))
        self.vma(1)
        high = self.read(c16(VECTOR_IRQ), "vh")
        low = self.read(c16(VECTOR_IRQ + 1), "vl")
        self.vma(1)
        self.control = Control("jump", address=self.bind(
            op("or", (op("shl", (ir.zext(high, 16), c8(8)), 16), ir.zext(low, 16)), 16), "pc"))


def lift(instruction, prefix="t"):
    return Lifter(instruction, prefix)
