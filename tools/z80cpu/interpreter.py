"""
Name:        interpreter.py
Description: runs the IR of lifted Z80 instructions (tools/z80cpu/lift.py) on a model of the CPU and its memory, bus
             cycle by bus cycle: the half of the sound lift's check that is not MAME's
Version:     0.1.0
Created:     19-11-2026

license:BSD-3-Clause
The expressions are evaluated as tools/m6809/interpreter.py does (the same IR); the registers are lift.py's, the
flags its six and their other bank's, and IFF1, IFF2 and IM what the effects di, ei and im set. Memory is the bytes
given and elsewhere pattern() (tools/reference/sound/oracle_z80_step.cpp's). The fetches' accesses come from the
lifter's list, in their place between the IR's loads and stores.
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from m6809 import interpreter as base    # noqa: E402  (the IR's evaluator)
from z80cpu import lift                    # noqa: E402
from z80cpu.lift import ir                 # noqa: E402


class Machine(base.Machine):
    def __init__(self, registers, flags, memory=None, iff=(0, 0), im=0):
        super().__init__(registers, flags, memory)
        self.iff1, self.iff2 = iff
        self.im = im

    def run(self, lifter):
        temps = {}
        self.accesses = []
        common = list(lifter.accesses)
        taken_list = list(lifter.taken_accesses)
        state = {"list": common, "position": 0}

        def flush(taken=False):
            items, position = state["list"], state["position"]
            while position < len(items) and items[position].address is not None:
                access = items[position]
                position += 1
                if access.when == "taken" and not taken:
                    continue
                self.accesses.append(("R", access.address, self.byte(access.address), access.cycle))
            state["position"] = position

        def data(kind, address, value):
            flush()
            items = state["list"]
            access = items[state["position"]]
            assert access.kind == kind and access.address is None, \
                f"{lifter.address:04X}: the lift's access {state['position']} is not a {kind}"
            state["position"] += 1
            self.accesses.append((kind, address & 0xFFFF, value, access.cycle))

        def execute(statements):
            for statement in statements:
                if isinstance(statement, ir.SetTemp):
                    if isinstance(statement.value, ir.Load):
                        address = self.evaluate(statement.value.address, temps) & 0xFFFF
                        value = self.byte(address)
                        data("R", address, value)
                        temps[statement.temp.name] = value
                    else:
                        temps[statement.temp.name] = self.evaluate(statement.value, temps)
                elif isinstance(statement, ir.SetReg):
                    self.regs[statement.index] = self.evaluate(statement.value, temps) & \
                        base.MASK[lift.WIDTHS[statement.index]]
                elif isinstance(statement, ir.SetFlag):
                    self.flags[statement.name] = self.evaluate(statement.value, temps) & 1
                elif isinstance(statement, ir.Store):
                    address = self.evaluate(statement.address, temps) & 0xFFFF
                    value = self.evaluate(statement.value, temps) & 0xFF
                    data("W", address, value)
                    self.written[address] = value
                elif isinstance(statement, ir.Effect):
                    if statement.name == "di":
                        self.iff1 = self.iff2 = 0
                    elif statement.name == "ei":
                        self.iff1 = self.iff2 = 1
                    elif statement.name == "im":
                        self.im = self.evaluate(statement.args[0], temps)
                    else:
                        raise ValueError(statement.name)
                else:
                    raise TypeError(statement)

        execute(lifter.statements)
        control = lifter.control
        taken = False
        if control.kind in ("call_if", "return_if"):
            taken = bool(self.evaluate(control.condition, temps))
            flush()
            if taken:
                state["list"], state["position"] = taken_list, 0
                execute(lifter.taken_statements)
                flush()
        if control.kind in ("next", "halt"):
            next_pc = lifter.next_address
        elif control.kind == "goto":
            next_pc = control.target
        elif control.kind == "branch":
            taken = bool(self.evaluate(control.condition, temps))
            next_pc = control.target if taken else lifter.next_address
        elif control.kind == "call":
            next_pc = control.target
        elif control.kind == "call_if":
            next_pc = control.target if taken else lifter.next_address
        elif control.kind == "return_if":
            next_pc = self.evaluate(control.address, temps) if taken else lifter.next_address
        else:
            next_pc = self.evaluate(control.address, temps)
        flush(taken)
        self.cycles = lifter.cycles + (lifter.extras.get("taken", 0) if taken else 0)
        return next_pc & 0xFFFF
