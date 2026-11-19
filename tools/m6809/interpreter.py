"""
Name:        interpreter.py
Description: runs the IR of lifted 6809 instructions (tools/m6809/lift.py) on a model of the CPU and its
             memory, bus cycle by bus cycle: the half of the lift's check that is not MAME's
Version:     0.1.0
Created:     19-11-2026

license:BSD-3-Clause
A Machine holds the registers (lift.py's indexes), the flags and the memory: the bytes given, and elsewhere a
pattern of the address (pattern(), the same as tools/reference/oracle_m6809_step.cpp's). run() lists every bus
cycle as the oracle does, ("O" / "R" / "W", address, value, cycle): the opcode space's cycles come from the
lifter's list, in their place between the IR's loads and stores.
"""
from . import lift as lift_module
from .lift import ir

MASK = {1: 1, 8: 0xFF, 16: 0xFFFF, 32: 0xFFFFFFFF, 64: 0xFFFFFFFFFFFFFFFF}


class NotEntireState(Exception):
    """RTI of a frame without E (the lift has the IRQ's frames only)."""


def pattern(address):
    return ((address * 2654435761) & 0xFFFFFFFF) >> 13 & 0xFF


def signed(value, bits):
    value &= MASK[bits]
    return value - (1 << bits) if value >> (bits - 1) else value


class Machine:
    def __init__(self, registers, flags, memory=None):
        self.regs = list(registers)
        self.flags = dict(flags)
        self.given = dict(memory or {})
        self.written = {}
        self.accesses = []

    def byte(self, address):
        address &= 0xFFFF
        if address in self.written:
            return self.written[address]
        if address in self.given:
            return self.given[address]
        return pattern(address)

    @property
    def cc(self):
        value = 0
        for number, name in enumerate(lift_module.CC_FLAGS):
            value |= self.flags[name] << number
        return value

    # ---- expressions ----

    def evaluate(self, node, temps):
        if isinstance(node, ir.Const):
            return node.value
        if isinstance(node, ir.Reg):
            return self.regs[node.index] & MASK[node.bits]
        if isinstance(node, ir.Flag):
            return self.flags[node.name]
        if isinstance(node, ir.Temp):
            return temps[node.name]
        if isinstance(node, ir.Op):
            return self.operator(node, temps)
        raise TypeError(f"not an expression: {node!r}")

    def operator(self, node, temps):
        name, bits = node.name, node.bits
        if name == "select":
            condition = self.evaluate(node.args[0], temps)
            return self.evaluate(node.args[1] if condition else node.args[2], temps)
        args = [self.evaluate(arg, temps) for arg in node.args]
        full = MASK[bits]
        if name == "add":
            return (args[0] + args[1]) & full
        if name == "sub":
            return (args[0] - args[1]) & full
        if name == "mul":
            return (args[0] * args[1]) & full
        if name == "and":
            return args[0] & args[1] & full
        if name == "or":
            return (args[0] | args[1]) & full
        if name == "xor":
            return (args[0] ^ args[1]) & full
        if name == "not":
            return ~args[0] & full
        if name == "neg":
            return -args[0] & full
        if name in ("shl", "shr", "sar"):
            count = args[1] & (63 if bits == 64 else 31)
            if name == "shl":
                return (args[0] << count) & full
            if name == "shr":
                return (args[0] & full) >> count
            return (signed(args[0], bits) >> count) & full
        if name in ("eq", "ne", "ult", "ule", "lt", "le"):
            width = node.args[0].bits
            a, b = args[0] & MASK[width], args[1] & MASK[width]
            if name in ("lt", "le"):
                a, b = signed(a, width), signed(b, width)
            return int({"eq": a == b, "ne": a != b, "ult": a < b, "ule": a <= b, "lt": a < b, "le": a <= b}[name])
        if name in ("zext", "trunc"):
            return args[0] & full
        if name == "sext":
            return signed(args[0], args[1]) & full
        raise ValueError(f"unknown operator {name}")

    # ---- an instruction ----

    def run(self, lifter):
        """Runs one lifted instruction (or the IRQ's entry); returns the address execution goes on at. Its bus
        cycles are in self.accesses, its cycles in self.cycles."""
        temps = {}
        pending = list(lifter.accesses)
        position = 0
        self.accesses = []

        def flush_opcode_space(taken=False):
            nonlocal position
            while position < len(pending) and pending[position].kind == "O":
                access = pending[position]
                position += 1
                if access.when == "taken" and not taken:
                    continue
                self.accesses.append(("O", access.address, self.byte(access.address), access.cycle))

        def data(kind, address, value):
            nonlocal position
            flush_opcode_space()
            access = pending[position]
            assert access.kind == kind, f"{lifter.address:04X}: the lift's access {position} is {access.kind}, not {kind}"
            position += 1
            self.accesses.append((kind, address & 0xFFFF, value, access.cycle))

        for statement in lifter.statements:
            if isinstance(statement, ir.SetTemp):
                if isinstance(statement.value, ir.Load):
                    address = self.evaluate(statement.value.address, temps) & 0xFFFF
                    value = self.byte(address)
                    data("R", address, value)
                    temps[statement.temp.name] = value
                else:
                    temps[statement.temp.name] = self.evaluate(statement.value, temps)
            elif isinstance(statement, ir.SetReg):
                self.regs[statement.index] = self.evaluate(statement.value, temps) & MASK[lift_module.WIDTHS[statement.index]]
            elif isinstance(statement, ir.SetFlag):
                self.flags[statement.name] = self.evaluate(statement.value, temps) & 1
            elif isinstance(statement, ir.Store):
                address = self.evaluate(statement.address, temps) & 0xFFFF
                value = self.evaluate(statement.value, temps) & 0xFF
                data("W", address, value)
                self.written[address] = value
            elif isinstance(statement, ir.Effect):
                if statement.name == "rti_entire_state":
                    if not self.evaluate(statement.args[0], temps):
                        raise NotEntireState()
                else:
                    raise ValueError(f"unknown effect {statement.name}")
            else:
                raise TypeError(f"not a statement: {statement!r}")

        control = lifter.control
        taken = False
        if control.kind in ("next", "wait"):
            next_pc = lifter.next_address
        elif control.kind == "goto":
            next_pc = control.target
        elif control.kind == "branch":
            taken = bool(self.evaluate(control.condition, temps))
            next_pc = control.target if taken else lifter.next_address
        elif control.kind == "call":
            next_pc = control.target if control.target is not None else self.evaluate(control.address, temps)
        else:
            next_pc = self.evaluate(control.address, temps)
        flush_opcode_space(taken)
        assert position == len(pending), f"{lifter.address:04X}: accesses left over"
        self.cycles = lifter.cycles + (lifter.extras.get("taken", 0) if taken else 0)
        return next_pc & 0xFFFF
