"""
Name:        function.py
Description: the 6809 program split into functions, and each one lifted into the core's decompiler blocks
             (decompiler/function.py), with the board's time kept instruction by instruction (milestone M6)
Version:     0.1.0
Created:     20-11-2026

license:BSD-3-Clause
Program takes what the static analysis finds (tools/m6809/analysis.py, symbols.txt and its overlays, no traces)
and splits it into functions: one starts at each vector's handler, at each call's target, at each place a jump
table leads to (a jump through one is a switch whose cases call their code as tail calls), and at each func or
code line of symbols.txt. A function's body is what its branches and jumps reach without entering another
function; a jump into another one is a tail call.

Code in the banked window (4000-5FFF) is a different function in each bank it runs in: the decompiler knows a
place by a number, the address in the fixed ROM (6000-FFFF) and in RAM (the overlay), and (bank + 1) << 16 |
address in the window (key()). A call from the window into the window stays in its bank (the program never
switches the bank under the code it runs); a call from the fixed ROM into the window goes to the bank the latch
selects there: the analysis' banks (Program.window_banks), a choice the C makes by the latch where there are more.

Builder lifts every instruction (tools/m6809/lift.py, MAME's MC6809 cycle by cycle) and makes it what the decompiler
works on:
  - JSR and BSR are calls (ir.Call), the return address they push left out (the decompiler's stack has it in its
    slot 0); the bytes any instruction puts on the stack are written to the work RAM all the same,
    Effect("m6809_stack_byte", offset from S, value), and S's step at its end Effect("m6809_stack_moved", step) (LDS:
    "m6809_stack_set"): the program reads its stack as data once (633F, TODO.md M6); RTS, and PULS with PC, are returns (the registers PULS takes are the stack's slots); RTI is the
    return of the IRQ's handler, which the runtime calls (it always returns where it was taken: TODO.md, M6);
  - JMP through a table (JMP [B,X]...) is a switch on the address read from the table, each case a tail call;
    JSR through one is a call with those choices;
  - the stack's pushes and pulls address S itself (the lift binds S - 1 in a temporary first): the decompiler's
    stack analysis (decompiler/stack.py) turns them into slots;
  - the interrupt mask (CC's I) is the runtime's, not a flag: what sets it is Effect("m6809_mask", I), what reads
    it the helper m6809_masked() (PSHS CC, TFR CC); SYNC is Effect("m6809_sync").
The time: after each instruction (before a call or a return, which leave it) Effect("m6809_block", cycles), where
the runtime lets the board's time pass and takes the IRQ (decompiler/cpu.py: an interrupt effect), as the
translation's M6809_CHECK does; a long branch's extra cycle when it is taken is Effect("m6809_cycles", taken), and
an access that may reach the board's devices, its video or sprite RAM or the palette (1E00-3FFF: their time is
seen) has Effect("m6809_at", cycle) before it, the end of its bus cycle from the instruction's start, as the
translation sets cpu.cycles there. The work RAM's accesses (constant addresses below 1E00, the direct page, the
stack) need no time: nothing but the 6809 reads it.
"""
import os
import sys
from collections import deque

TOOLS = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, TOOLS)
sys.path.insert(0, os.path.join(TOOLS, "core"))

from decompiler import stack  # noqa: E402
from decompiler.function import Block, Function  # noqa: E402
from v60 import ir  # noqa: E402

from m6809 import analysis  # noqa: E402
from m6809 import lift  # noqa: E402

WINDOW = (0x4000, 0x6000)
SP = lift.S
COPY_POINTER = 7             # S as the round robin's copies use it, a pointer to data (Program.find_copy_stretches)
READABLE_FLAGS = True        # the lift's flags in the forms the core reads as comparisons (DecompileLifter)
TIMED = (0x1E00, 0x4000)     # the sprite RAM, the video RAMs, the palette and the devices: accesses whose time shows


def key(bank, address):
    """The decompiler's number of the instruction at 'address' in 'bank' (None outside the window)"""
    bank, address = analysis.key(bank, address)
    return address if bank is None else (bank + 1) << 16 | address


def split_key(number):
    """(bank or None, address) of a key()"""
    return (None, number) if number < 0x10000 else ((number >> 16) - 1, number & 0xFFFF)


def label(number):
    bank, address = split_key(number)
    return f"{address:04X}" if bank is None else f"{bank}:{address:04X}"


def in_window(address):
    return WINDOW[0] <= address < WINDOW[1]


class Program:
    """The analysed program (translate.load_code), split into functions"""

    def __init__(self, program, syms, code, ram_code, vectors):
        self.rom = program
        self.syms = syms
        self.code = code
        self.ram_code = ram_code
        self.instructions = {key(*k): ins for k, ins in code.instructions.items()}
        self.vectors = [key(bank, address) for bank, address, _ in vectors]
        by_name = {why.split()[-1]: key(bank, address) for bank, address, why in vectors}
        self.reset, self.irq = by_name["RESET"], by_name["IRQ"]
        self.bank_writers = {key(bank, ins.address) for bank, ins, _ in code.bank_switches}
        # what each jump or call through a table can reach: {instruction's key: [targets' keys]}
        self.table_targets = {}
        for (bank, address), (_, targets) in code.jump_tables.items():
            found = self.table_targets.setdefault(key(bank, address), [])
            for target in targets:
                number = key(bank, target)
                if number in self.instructions and number not in found:
                    found.append(number)
        # (symbols.txt's tables with the jump that reads them, where the analysis cannot see it: jump=XXXX)
        for symbol in syms.entries:
            if symbol.kind == "table" and "jump" in symbol.keys:
                read = program.reader(symbol.bank)
                target_bank = symbol.keys.get("bank")
                target_bank = None if target_bank is None else int(target_bank)
                found = self.table_targets.setdefault(key(None, int(symbol.keys["jump"], 16)), [])
                for index in range(int(symbol.keys.get("count", "1"), 0)):
                    target = read(symbol.address + 2 * index) << 8 | read(symbol.address + 2 * index + 1)
                    number = key(target_bank, target)
                    if target and number in self.instructions and number not in found:
                        found.append(number)
        self.window_banks = self.find_window_banks()
        # the functions entered by a jump with their return address in Y (symbols.txt's return=Y)
        self.returns_through_y = {key(s.bank, s.address) for s in syms.entries
                                  if s.kind == "func" and s.keys.get("return") == "Y"}
        self.copy_stretches = self.find_copy_stretches()
        # where the main loop waits for work (symbols.txt's idle): the C's states (runtime/src/m6809_program.c)
        self.idle = {key(s.bank, s.address) for s in syms.entries if "idle" in s.keys}

        entries = set(self.vectors)
        entries |= {key(*k) for k in code.calls}
        for targets in self.table_targets.values():
            entries |= set(targets)
        entries |= {key(s.bank, s.address) for s in syms.code()}
        entries |= {address for address, _, _ in syms.overlays()}
        for transfer in self.window_transfers():
            entries |= set(transfer[1])
        self.entries = {entry for entry in entries if entry in self.instructions}
        self.bodies = {entry: self.walk(entry) for entry in sorted(self.entries)}
        # code no function reaches (only the tables' jumps lead there): an entry of its own
        while True:
            covered = set().union(*self.bodies.values())
            left = sorted(number for number in self.instructions if number not in covered)
            if not left:
                break
            self.entries.add(left[0])
            self.bodies[left[0]] = self.walk(left[0])
        self.names = self.choose_names()

    def find_copy_stretches(self):
        """{instruction: 'save', 'restore' or 'pointer'}: the round robin's copies keep S at <dd (STS <dd), set S
        to the data they copy and pull it with PULS, and put S back (LDS <dd); the C has no S to keep, so the STS
        and the LDS are left out and S between them is a pointer of its own (COPY_POINTER). The IRQ cannot come
        in between: they run in its handler"""
        found = {}
        for number, ins in self.instructions.items():
            if ins.mnemonic != "STS" or ins.operand.mode != "direct":
                continue
            found[number] = "save"
            work = deque([number])
            seen = set()
            while work:
                current = work.popleft()
                if current in seen:
                    continue
                seen.add(current)
                current_ins = self.instructions[current]
                if current_ins.mnemonic == "LDS" and current_ins.operand.mode == "direct" and \
                        current_ins.operand.value == ins.operand.value:
                    found[current] = "restore"
                    continue
                if current != number:
                    found.setdefault(current, "pointer")
                if current_ins.flow in ("return", "rti", "jump", "ijump", "call", "icall"):
                    raise ValueError(f"{label(current)}: S is not put back from <${ins.operand.value:02X} before it")
                for address in ([current_ins.next] if current_ins.flow in ("next", "branch") else []) + \
                        ([current_ins.target] if current_ins.flow in ("branch", "jump") else []):
                    following = key(split_key(current)[0], address)
                    if following in self.instructions:
                        work.append(following)
        return found

    # ---- the banked window ----

    def find_window_banks(self):
        """{address in the window: the banks the analysis found code at it in}"""
        found = {}
        for (bank, address) in self.code.instructions:
            if bank is not None:
                found.setdefault(address, set()).add(bank)
        return found

    def window_targets(self, number, address):
        """The keys a transfer from the instruction 'number' to 'address' can reach: one, but from the fixed ROM
        into the window, where it is every bank the analysis found code at 'address' in"""
        bank, _ = split_key(number)
        if not in_window(address):
            return [key(None, address)]
        if bank is not None and number not in self.bank_writers:
            return [key(bank, address)]
        return [key(choice, address) for choice in sorted(self.window_banks.get(address, ()))]

    def window_transfers(self):
        """(instruction, targets) of the calls and jumps from outside the window into it"""
        found = []
        for number, ins in self.instructions.items():
            if ins.target is None or ins.flow not in ("call", "jump", "branch"):
                continue
            if in_window(ins.target) and (split_key(number)[0] is None or number in self.bank_writers):
                found.append((number, self.window_targets(number, ins.target)))
        return found

    # ---- the functions ----

    def local(self, number, address):
        """The key execution goes on at in the same function after the instruction 'number' (its next one or a
        branch's target), or None when it leaves for another function (the window from outside it)"""
        targets = self.window_targets(number, address & 0xFFFF)
        if len(targets) != 1 or (in_window(address) and (split_key(number)[0] is None or number in self.bank_writers)):
            return None
        return targets[0] if targets[0] in self.instructions else None

    def successors(self, number):
        ins = self.instructions[number]
        found = []
        if ins.flow in ("next", "branch", "call", "icall", "wait"):
            found.append(ins.next)
        if ins.flow in ("branch", "jump") and ins.target is not None:
            found.append(ins.target)
        result = []
        for address in found:
            target = self.local(number, address)
            if target is not None:
                result.append(target)
        return result

    def walk(self, entry):
        body = {}
        work = deque([entry])
        while work:
            number = work.popleft()
            if number in body or (number != entry and number in self.entries):
                continue
            body[number] = self.instructions[number]
            work.extend(self.successors(number))
        return body

    def choose_names(self):
        named = {key(s.bank, s.address): s.name for s in self.syms.entries if s.kind in ("func", "code", "overlay")}
        names = {}
        for entry in sorted(self.entries):
            bank, address = split_key(entry)
            names[entry] = named.get(entry) or (f"sub_{address:04X}" if bank is None else f"sub_{bank}_{address:04X}")
        return names


# ---- the lift, as the decompiler takes it ----

def mentions(expression, register):
    return any(isinstance(node, ir.Reg) and node.index == register for node in ir.walk(expression))


def timed_address(address, store):
    """Whether an access at 'address' may reach 1E00-3FFF, whose time shows: a constant there; for a write also
    an address worked out from X, Y, U or memory (the direct page and the stack are the work RAM's)"""
    if isinstance(address, ir.Const):
        return TIMED[0] <= address.value < TIMED[1]
    if not store:
        return False
    return not (mentions(address, lift.DP) or mentions(address, SP))


class StackTemps:
    """The lift's pushes bind S - 1 in a temporary, set S to it and store through it: the store's address made S
    itself, so that the stack analysis sees a push"""

    def __init__(self):
        self.offsets = {}     # temporary -> its offset from S as the instruction started
        self.sp = 0           # S's offset from where the instruction started

    def offset(self, expression):
        if isinstance(expression, ir.Reg) and expression.index == SP:
            return self.sp
        if isinstance(expression, ir.Temp):
            return self.offsets.get(expression.name)
        if isinstance(expression, ir.Op) and expression.name in ("add", "sub") and isinstance(expression.args[1], ir.Const):
            inner = self.offset(expression.args[0])
            if inner is None:
                return None
            amount = expression.args[1].value
            amount = amount - 0x10000 if amount & 0x8000 else amount
            return inner + amount if expression.name == "add" else inner - amount
        return None

    def address(self, expression):
        """'expression' with each stack temporary in it made S plus its distance"""
        def change(node):
            if not isinstance(node, ir.Temp):
                return None
            offset = self.offsets.get(node.name)
            if offset is None:
                return None
            delta = offset - self.sp
            reg = ir.Reg(SP, 16)
            if delta == 0:
                return reg
            return ir.Op("add" if delta > 0 else "sub", (reg, ir.Const(abs(delta), 16)), 16)
        return ir.rebuild(expression, change)

    def see(self, statement):
        if isinstance(statement, ir.SetTemp):
            offset = self.offset(statement.value)
            if offset is not None and isinstance(statement.value, (ir.Op, ir.Reg)):
                self.offsets[statement.temp.name] = offset
        elif isinstance(statement, ir.SetReg) and statement.index == SP:
            offset = self.offset(statement.value)
            if offset is not None:
                self.sp = offset


def mask_reads(node):
    """CC's I read: the runtime's mask; DP: 0 (its value at the reset, and what 607F sets it to)"""
    if isinstance(node, ir.Flag) and node.name == "i":
        return ir.Op("helper:m6809_masked", (), 1)
    if isinstance(node, ir.Reg) and node.index == lift.DP:
        return ir.Const(0, 8)
    return None


# the instructions that write DP: TFR B,DP at 602F and 607F, each after LDB #$00 (the direct page is the work RAM's first 256
# bytes all along: DP is the constant 0 in the C)
DP_WRITES = {0x602F, 0x607F}


class Builder:
    """Lifts the functions of a Program into the decompiler's blocks"""

    def __init__(self, program):
        self.program = program
        self.leaders = self.find_leaders()

    def find_leaders(self):
        """Where blocks start: entries, every branch's and jump's target, and the instruction after anything that
        branches, jumps, calls or returns"""
        found = set(self.program.entries)
        for number, ins in self.program.instructions.items():
            following = self.program.local(number, ins.next)
            if ins.flow in ("branch", "jump") and ins.target is not None:
                target = self.program.local(number, ins.target)
                if target is not None:
                    found.add(target)
            if ins.flow not in ("next",) and following is not None:
                found.add(following)
        return found

    def build(self, entry):
        program = self.program
        body = program.bodies[entry]
        function = Function(entry, program.names[entry])
        self.current_entry = entry
        self.extra = []
        order = sorted(body)
        # (where the code overlaps itself, an instruction's next one is not the one after it in the address order:
        # a block starts there too)
        leaders = set(self.leaders)
        for position, number in enumerate(order):
            for successor in program.successors(number):
                if successor in body and (position + 1 >= len(order) or order[position + 1] != successor):
                    leaders.add(successor)
        current = None
        for position, number in enumerate(order):
            if current is not None and number in leaders:
                if current.end is None:
                    current.end = ir.Goto(number)
                current = None
            if current is None:
                current = Block(number)
                function.blocks[number] = current
            current.addresses.append(number)
            before = len(current.statements)
            if number in program.idle:
                # the main loop waiting for work, before the instruction: Effect("m6809_idle", U), what it keeps
                # across its tasks (a state saved there is the board's and U: m6809_program.c)
                current.statements.append(ir.Effect("m6809_idle", (ir.Reg(lift.U, 16),)))
            self.lift(function, current, number)
            for statement in current.statements[before:]:
                statement.origin = number
            if current.end is not None and not hasattr(current.end, "origin"):
                current.end.origin = number
            if current.end is None:
                following = program.local(number, program.instructions[number].next)
                if position + 1 >= len(order) or order[position + 1] != following:
                    current.end = self.transfer(number, program.instructions[number].next)
                    current.end.origin = number
            if current.end is not None:
                current = None
        for block in self.extra:
            function.blocks[block.label] = block
        self.close(function)
        self.entry_of_its_own(function)
        return function

    @staticmethod
    def entry_of_its_own(function):
        """A function whose first block is a loop's head (a loop back to its start: the main loop, a routine that
        counts down from its entry) gets an empty block before it: what the passes put at the function's start
        (the bytes of D a function takes, z80/pairs.py) runs once, not each time round"""
        entry = function.entry
        if not any(entry in block.successors() for block in function.blocks.values()):
            return
        head = ("head", entry)
        first = function.blocks.pop(entry)
        first.label = head
        for block in function.blocks.values():
            stack.redirect(block.end, entry, head)
        stack.redirect(first.end, entry, head)
        start = Block(entry, end=ir.Goto(head))
        start.end.origin = entry
        function.blocks = {entry: start, head: first, **function.blocks}

    # ---- control ----

    def transfer(self, number, address):
        """How execution gets to 'address' after the instruction 'number': a goto in this function, or a tail
        call of another (by the latch's bank where it may be more than one)"""
        address &= 0xFFFF
        local = self.program.local(number, address)
        if local is not None and local in self.program.bodies.get(self.current_entry, ()):
            return ir.Goto(local)    # (the function's own start too: a loop, the main loop's)
        targets = self.program.window_targets(number, address)
        if len(targets) == 1:
            return ir.TailCall(targets[0])
        return ir.Switch(ir.Op("helper:m6809_bank_code", (ir.Const(address, 16),), 32),
                         {target: ("tail", target) for target in targets})

    def close(self, function):
        """Gotos to code outside the function become tail calls in blocks of their own"""
        def resolve(target):
            if target in function.blocks:
                return target
            if isinstance(target, tuple) and target[0] == "tail":
                if target not in function.blocks:
                    function.blocks[target] = Block(target, end=ir.TailCall(target[1]))
                return target
            if target in self.program.instructions:
                tail = ("tail", target)
                if tail not in function.blocks:
                    function.blocks[tail] = Block(tail, end=ir.TailCall(target))
                return tail
            raise lift.Unsupported(f"function {label(function.entry)}: no code at {target!r}")

        for block in list(function.blocks.values()):
            end = block.end
            if isinstance(end, ir.Goto):
                end.target = resolve(end.target)
            elif isinstance(end, ir.Branch):
                end.target = resolve(end.target)
                end.otherwise = resolve(end.otherwise)
            elif isinstance(end, ir.Switch):
                end.cases = {case: resolve(target) for case, target in end.cases.items()}

    # ---- one instruction ----

    def lift(self, function, block, number):
        program = self.program
        ins = program.instructions[number]
        bank, address = split_key(number)
        prefix = f"t{address:04X}_" if bank is None else f"t{bank}_{address:04X}_"
        lifted = (DecompileLifter if READABLE_FLAGS else lift.Lifter)(ins, prefix=prefix)
        control = lifted.control
        stretch = program.copy_stretches.get(number)
        if stretch == "pointer":
            def to_pointer(node):
                if isinstance(node, ir.Reg) and node.index == SP:
                    return ir.Reg(COPY_POINTER, 16)
                return None
            for statement in lifted.statements:
                ir.map_statement(statement, to_pointer)
            lifted.statements = [ir.SetReg(COPY_POINTER, statement.value)
                                 if isinstance(statement, ir.SetReg) and statement.index == SP else statement
                                 for statement in lifted.statements]
        # (RTI pulls the whole state, which the C has in the interrupted code's variables: none of its statements)
        self.stack_moved = None
        statements = self.timed(lifted) if control.kind != "rti" else []
        # S's step over the instruction, for the runtime's cpu.s: after its statements (a call's before the call, a
        # return's before the return)
        moved = [self.stack_moved] if self.stack_moved is not None else []
        if ins.mnemonic in ("ANDCC", "ORCC"):
            # (with an immediate: the flags it clears or sets, each on its own, not CC rebuilt; I as the mask)
            value = ins.operand.value
            statements = []
            for number, name in enumerate(lift.CC_FLAGS):
                cleared = ins.mnemonic == "ANDCC" and not value >> number & 1
                set_ = ins.mnemonic == "ORCC" and value >> number & 1
                if not (cleared or set_):
                    continue
                if name == "i":
                    statements.append(ir.Effect("m6809_mask", (ir.Const(1 if set_ else 0, 1),)))
                else:
                    statements.append(ir.SetFlag(name, ir.Const(1 if set_ else 0, 1)))
        cycles = ir.Effect("m6809_block", (ir.Const(lifted.cycles, 32),))
        following = ins.next & 0xFFFF
        if stretch in ("save", "restore") or (self.current_entry in program.returns_through_y and
                                              ins.mnemonic == "PSHS" and tuple(ins.operand.registers) == ("Y",)):
            # S kept and put back, or the return address a return=Y function pushes: the C has neither
            statements = []
            moved = []
        called = program.window_targets(number, control.target) if control.kind == "goto" else []
        if len(called) == 1 and called[0] in program.returns_through_y:
            # LDY #back / JMP function: a call that comes back to 'back'
            back = self.loaded_y(number)
            block.statements += statements + moved + [cycles, ir.Call(target=called[0], return_address=back)]
            block.end = self.transfer(number, back)
            return

        kind = control.kind
        if kind in ("call",):
            statements = self.without_pushes(statements, 2)
            statements, moved = self.return_pushed(statements, moved, ins.next & 0xFFFF)
            block.statements += statements + moved + [cycles]
            block.statements.append(self.call(number, ins, control, block))
            block.end = self.transfer(number, following)
            return
        if kind == "return":
            # RTS, PULS with PC: the registers pulled, the PC's two bytes and the read after them left out
            if ins.mnemonic == "RTS":
                # (S's step is the return address' two bytes: one line, m6809_pop_return())
                moved = [ir.Effect("m6809_pop_return", ())]
            block.statements += self.without_return(statements) + moved + [cycles]
            block.end = ir.Return()
            return
        if kind == "rti":
            block.statements.append(cycles)
            block.end = ir.Return()
            return
        if kind == "wait":
            if lifted.extras.get("cwai"):
                raise lift.Unsupported(f"{label(number)}: CWAI")
            # (SYNC's cycles, then the wait for the line and its cycle, then the instruction's end: the translation's
            # order)
            block.statements += statements + moved + [ir.Effect("m6809_cycles", (ir.Const(lifted.cycles, 32),)),
                                              ir.Effect("m6809_sync"), ir.Effect("m6809_block", (ir.Const(0, 32),))]
            block.end = self.transfer(number, following)
            return
        block.statements += statements + moved
        if kind == "next":
            block.statements.append(cycles)
            return
        if kind == "goto":
            block.statements.append(cycles)
            block.end = self.transfer(number, control.target)
            return
        if kind == "branch":
            extra = lifted.extras.get("taken", 0)
            if extra:
                block.statements.append(ir.Effect("m6809_cycles", (ir.Op("mul", (
                    ir.Op("zext", (control.condition,), 32), ir.Const(extra, 32)), 32),)))
            block.statements.append(cycles)
            taken = self.transfer(number, control.target)
            otherwise = self.transfer(number, following)
            block.end = ir.Branch(control.condition, self.way(taken, number, "taken"),
                                  self.way(otherwise, number, "on"))
            return
        if kind == "jump":
            block.statements.append(cycles)
            targets = program.table_targets.get(number)
            if not targets:
                raise lift.Unsupported(f"{label(number)}: {ins.text} with no table")
            indexed = self.table_index(block, number, ins)
            if indexed is not None:
                register, cases = indexed
                # (zero-extended: the core's writer prints a switch's value right only in its low bits, and the
                # accumulator may be a byte of a wider value, D's B: satan's BDE2, ASLB / JMP [B,U] with A set)
                block.end = ir.Switch(ir.Op("zext", (register,), 32),
                                      {offset: ("tail", target) for offset, target in cases.items()})
                return
            block.end = ir.Switch(control.address, {split_key(target)[1]: ("tail", target) for target in targets})
            return
        raise lift.Unsupported(f"{label(number)}: control {kind}")

    def table_index(self, block, number, ins):
        """JMP or JSR [A,r] or [B,r] through a table whose address this block loads into r (LDr #table, r untouched
        since): (the accumulator, {its value: the code that entry leads to}), the switch on the offset into the table
        (the index twice) rather than on the address read from it. None where the table's address is not that sure"""
        operand = ins.operand
        if not (operand.mode == "indexed" and operand.indirect and operand.kind == "acc" and
                operand.base in lift.INDEX and operand.acc in ("A", "B")):
            return None
        base = lift.INDEX[operand.base]
        table = None
        for statement in reversed(block.statements):
            if isinstance(statement, ir.SetReg) and statement.index == base:
                table = statement.value.value if isinstance(statement.value, ir.Const) else None
                break
            if isinstance(statement, (ir.Call, ir.Effect)) and not (isinstance(statement, ir.Effect) and
                                                                     statement.name.startswith("m6809_")):
                return None
        if table is None:
            return None
        bank, _ = split_key(number)
        read = self.program.rom.reader(bank if in_window(table) else None)
        cases = {}
        for target in self.program.table_targets.get(number, ()):
            for offset in range(0, 0x80, 2):
                word = read(table + offset) << 8 | read(table + offset + 1)
                if target in self.program.window_targets(number, word):
                    cases[offset] = ("tail", target)
        found = set(target for _, target in cases.values())
        if found != set(self.program.table_targets.get(number, ())):
            return None
        accumulator = lift.A if operand.acc == "A" else lift.B
        return ir.Reg(accumulator, 8), {offset: target for offset, (_, target) in cases.items()}

    def loaded_y(self, number):
        """The constant the instruction before 'number' loads into Y (LDY #back), where a return=Y function is
        jumped to"""
        bank, address = split_key(number)
        for back in range(1, 5):
            before = self.program.instructions.get(key(bank, address - back))
            if before is not None and before.next == address:
                if before.mnemonic == "LDY" and before.operand.mode == "immediate":
                    return before.operand.value
                break
        raise lift.Unsupported(f"{label(number)}: a jump to a return=Y function without LDY #back before it")

    def way(self, end, number, name):
        """A branch's way as a block label: a goto's target, or a block of its own for a tail call or a switch"""
        if isinstance(end, ir.Goto):
            return end.target
        label_ = (name, number)
        extra = Block(label_, end=end)
        end.origin = number
        self.extra.append(extra)
        return label_

    def call(self, number, ins, control, block=None):
        if control.target is not None:
            targets = self.program.window_targets(number, control.target)
            if len(targets) == 1:
                return ir.Call(target=targets[0], return_address=ins.next & 0xFFFF)
            return ir.Call(address=ir.Op("helper:m6809_bank_code", (ir.Const(control.target, 16),), 32),
                           choices=tuple(targets), return_address=ins.next & 0xFFFF)
        targets = self.program.table_targets.get(number)
        if not targets:
            raise lift.Unsupported(f"{label(number)}: {ins.text} with no table")
        indexed = self.table_index(block, number, ins) if block is not None else None
        if indexed is not None:
            # (a switch on the offset into the table: emit.Writer.call reads the cases from 'offsets')
            register, cases = indexed
            call = ir.Call(address=register, choices=tuple(targets), return_address=ins.next & 0xFFFF)
            call.offsets = {}
            for offset, target in sorted(cases.items()):
                call.offsets.setdefault(target, []).append(offset)
            return call
        return ir.Call(address=control.address, choices=tuple(targets), return_address=ins.next & 0xFFFF)

    @staticmethod
    def without_pushes(statements, count):
        """The statements but the last 'count' pushes (a call's return address, its low byte first)"""
        kept = list(statements)
        removed = 0
        index = len(kept) - 1
        while removed < count and index >= 0:
            statement = kept[index]
            if isinstance(statement, ir.Store) and mentions(statement.address, SP) and \
                    isinstance(statement.value, ir.Const):
                # the store, the S it set before it
                assert isinstance(kept[index - 1], ir.SetReg) and kept[index - 1].index == SP
                del kept[index - 1:index + 1]
                index -= 2
                removed += 1
                continue
            index -= 1
        if removed != count:
            raise lift.Unsupported("a call whose pushes are not its last statements")
        return kept

    @staticmethod
    def return_pushed(statements, moved, back):
        """A call's statements with the two bytes of its return address and S's step made one effect,
        m6809_push_return(back) (the runtime writes them at S - 2 and S - 1 and moves S): one line in the C"""
        def offset(statement):
            return statement.args[0].value if isinstance(statement, ir.Effect) and \
                statement.name == "m6809_stack_byte" else None
        low = [index for index, statement in enumerate(statements) if offset(statement) == 0xFFFF]
        high = [index for index, statement in enumerate(statements) if offset(statement) == 0xFFFE]
        if not (low and high and len(moved) == 1 and moved[0].args[0].value == 0xFFFE):
            raise lift.Unsupported("a call that does not push its return address as JSR does")
        pushed = (statements[low[-1]].args[1], statements[high[-1]].args[1])
        if not (isinstance(pushed[0], ir.Const) and isinstance(pushed[1], ir.Const) and
                pushed[0].value == back & 0xFF and pushed[1].value == back >> 8):
            raise lift.Unsupported(f"a call whose pushed return address is not {back:04X}")
        kept = [statement for index, statement in enumerate(statements) if index not in (low[-1], high[-1])]
        return kept, [ir.Effect("m6809_push_return", (ir.Const(back, 16),))]

    @staticmethod
    def without_return(statements):
        """A return's statements without the PC's pulls: the last three reads of the stack and their S steps"""
        kept = list(statements)
        reads = [index for index, statement in enumerate(kept)
                 if isinstance(statement, ir.SetTemp) and isinstance(statement.value, ir.Load)
                 and mentions(statement.value.address, SP)]
        if len(reads) < 3:
            raise lift.Unsupported("a return that pulls no PC")
        first = reads[-3]
        # (PCH, its S step, PCL, its S step, the PC worked out, the read of the stack's top)
        return kept[:first]

    def timed(self, lifted):
        """The lift's statements with the stack's temporaries made S, the mask as the runtime's, and the accesses
        that may reach 1E00-3FFF preceded by their time"""
        data = [access for access in lifted.accesses if access.kind in ("R", "W")]
        position = 0
        stack = StackTemps()
        result = []
        self.stack_moved = None      # S's step over the instruction (the runtime's cpu.s follows it), if it moves
        for statement in lifted.statements:
            is_store = isinstance(statement, ir.Store)
            is_load = isinstance(statement, ir.SetTemp) and isinstance(statement.value, ir.Load)
            pushed = None
            if is_store:
                offset = stack.offset(statement.address)
                if offset is not None and statement.bits == 8:
                    # a byte into the stack: the work RAM's too, at S + offset (S as the instruction started)
                    pushed = ir.Effect("m6809_stack_byte", (ir.Const(offset & 0xFFFF, 16), statement.value))
                statement = ir.Store(stack.address(statement.address), statement.value, statement.bits)
            elif is_load:
                statement = ir.SetTemp(statement.temp, ir.Load(stack.address(statement.value.address),
                                                               statement.value.bits))
            if is_store or is_load:
                access = data[position]
                position += 1
                where = statement.address if is_store else statement.value.address
                if timed_address(where, is_store):
                    result.append(ir.Effect("m6809_at", (ir.Const(access.cycle + 1, 32),)))
            if isinstance(statement, ir.SetReg) and statement.index == SP and stack.offset(statement.value) is None:
                # S set to a value of its own (LDS #$0300, the boot's): the runtime's from here on, the pushes after
                # it counted from it
                if mentions(statement.value, SP):
                    raise lift.Unsupported(f"{lifted.address:04X}: S set from itself")
                result.append(ir.Effect("m6809_stack_set", (statement.value,)))
                stack.sp = 0
                stack.offsets = {}
            stack.see(statement)
            if pushed is not None:
                ir.map_statement(statement, mask_reads)
                result.append(statement)
                result.append(ir.Effect(pushed.name, (pushed.args[0], ir.rebuild(pushed.args[1], mask_reads))))
                continue
            if isinstance(statement, ir.SetFlag) and statement.name == "i":
                result.append(ir.Effect("m6809_mask", (ir.rebuild(statement.value, mask_reads),)))
                continue
            if isinstance(statement, ir.Effect) and statement.name == "rti_entire_state":
                continue
            if isinstance(statement, ir.SetReg) and statement.index == lift.DP:
                if lifted.address not in DP_WRITES:
                    raise lift.Unsupported(f"{lifted.address:04X}: DP written")
                continue
            ir.map_statement(statement, mask_reads)
            result.append(statement)
        if stack.sp:
            self.stack_moved = ir.Effect("m6809_stack_moved", (ir.Const(stack.sp & 0xFFFF, 16),))
        return result


# ---- the time, where the IRQ cannot come in ----

def callees(function):
    """The functions 'function' calls, tail-calls or dispatches to"""
    found = set()
    for block in function.blocks.values():
        for statement in block.statements + [block.end]:
            if isinstance(statement, ir.Call):
                found |= {statement.target} if statement.target is not None else set(statement.choices)
            elif isinstance(statement, ir.TailCall):
                found.add(statement.target)
            elif isinstance(statement, ir.Switch):
                found |= {target[1] for target in statement.cases.values()
                          if isinstance(target, tuple) and target[0] == "tail"}
    return found


def masked_functions(program, built):
    """The functions that only ever run with the IRQ masked: all but what the main loop runs (its tasks) and what a
    function that lets the IRQ in (ANDCC #$EF: the main loop's setup; PULS CC) runs, and what those call"""
    calls = {entry: callees(function) for entry, function in built.items()}
    roots = [entry for entry, name in program.names.items() if name == "main_loop"]
    for entry, function in built.items():
        for block in function.blocks.values():
            for statement in block.statements:
                if isinstance(statement, ir.Effect) and statement.name == "m6809_mask" and \
                        not (isinstance(statement.args[0], ir.Const) and statement.args[0].value):
                    roots.append(entry)
    unmasked = set()
    work = list(roots)
    while work:
        entry = work.pop()
        if entry in unmasked or entry not in built:
            continue
        unmasked.add(entry)
        work.extend(calls[entry])
    if program.irq in unmasked:
        raise ValueError("the IRQ's handler lets the IRQ in")
    return set(built) - unmasked


def merge_time(function):
    """In a function the IRQ cannot interrupt, the instructions' cycles of a block are one m6809_block at its end (or
    before a call, which starts at the time it is called): nothing else takes the board's time between them but the
    accesses whose time shows, whose m6809_at counts from the same start. The VBLANK the time may pass meanwhile is
    seen at that m6809_block or at such an access, as the translation's is; the work RAM's accesses meanwhile, which
    nothing but the 6809 sees, may come before it or after it alike. How many m6809_block it took out"""
    removed = 0
    for block in function.blocks.values():
        merged, pending = [], 0
        for statement in block.statements:
            if isinstance(statement, ir.Effect) and statement.name == "m6809_block":
                pending += statement.args[0].value
                removed += 1
                continue
            if isinstance(statement, ir.Effect) and statement.name == "m6809_at":
                moved = ir.Effect("m6809_at", (ir.Const(pending + statement.args[0].value, 32),))
                moved.origin = getattr(statement, "origin", None)
                merged.append(moved)
                continue
            if isinstance(statement, ir.Call) or (isinstance(statement, ir.Effect) and
                                                  statement.name in ("m6809_mask", "m6809_sync")):
                if pending:
                    flush = ir.Effect("m6809_block", (ir.Const(pending, 32),))
                    flush.origin = getattr(statement, "origin", None)
                    merged.append(flush)
                    removed -= 1
                    pending = 0
            merged.append(statement)
        if pending:
            flush = ir.Effect("m6809_block", (ir.Const(pending, 32),))
            flush.origin = block.addresses[-1] if block.addresses else None
            merged.append(flush)
            removed -= 1
        block.statements = merged
    return removed


# ---- D as a register of its own ----

D = 9            # the 6809's D (A:B), a register of the decompiler's (decompile.py's profile), A its high byte


def is_reg(node, index):
    return isinstance(node, ir.Reg) and node.index == index


def as_d(node):
    """The lift's D put together from A and B, (zext(A) << 8) | zext(B): D itself"""
    if isinstance(node, ir.Op) and node.name == "or" and node.bits == 16:
        high, low = node.args
        if isinstance(high, ir.Op) and high.name == "shl" and isinstance(high.args[1], ir.Const) and \
                high.args[1].value == 8 and isinstance(high.args[0], ir.Op) and high.args[0].name == "zext" and \
                is_reg(high.args[0].args[0], lift.A) and isinstance(low, ir.Op) and low.name == "zext" and \
                is_reg(low.args[0], lift.B):
            return ir.Reg(D, 16)
    return None


def byte_of_d(node):
    """A and B read as the bytes of D (the form tools/core/z80/pairs.py reads a byte of a pair in)"""
    if is_reg(node, lift.A):
        return ir.trunc(ir.op("shr", (ir.Reg(D, 16), ir.Const(8, 8)), 16), 8)
    if is_reg(node, lift.B):
        return ir.trunc(ir.Reg(D, 16), 8)
    return None


def d_statements(statements):
    """A block's statements with D whole where the lift writes A and B: D read whole where the lift puts it
    together, A and B read as its bytes, a write of A or B a write of that byte of D, and A then B written with
    the two bytes of one value (the lift's set_d) a write of D"""
    result = []
    index = 0
    while index < len(statements):
        statement = statements[index]
        following = statements[index + 1] if index + 1 < len(statements) else None
        if isinstance(statement, ir.SetReg) and statement.index == lift.A and isinstance(following, ir.SetReg) \
                and following.index == lift.B:
            value = following.value.args[0] if isinstance(following.value, ir.Op) and \
                following.value.name == "trunc" else None
            high = statement.value
            if value is not None and value.bits == 16 and isinstance(high, ir.Op) and high.name == "trunc" and \
                    isinstance(high.args[0], ir.Op) and high.args[0].name == "shr" and high.args[0].args[0] == value \
                    and isinstance(high.args[0].args[1], ir.Const) and high.args[0].args[1].value == 8:
                whole = ir.SetReg(D, ir.rebuild(ir.rebuild(value, as_d), byte_of_d))
                whole.origin = getattr(following, "origin", None)
                result.append(whole)
                index += 2
                continue
        ir.map_statement(statement, as_d)
        ir.map_statement(statement, byte_of_d)
        if isinstance(statement, ir.SetReg) and statement.index in (lift.A, lift.B):
            wide = ir.zext(statement.value, 16)
            if statement.index == lift.A:
                value = ir.op("or", (ir.op("and", (ir.Reg(D, 16), ir.Const(0x00FF, 16)), 16),
                                     ir.op("shl", (wide, ir.Const(8, 8)), 16)), 16)
            else:
                value = ir.op("or", (ir.op("and", (ir.Reg(D, 16), ir.Const(0xFF00, 16)), 16), wide), 16)
            replaced = ir.SetReg(D, value)
            replaced.origin = getattr(statement, "origin", None)
            statement = replaced
        result.append(statement)
        index += 1
    return result


def use_d(function):
    """D whole in every block of 'function' (d_statements), its ends too"""
    for block in function.blocks.values():
        block.statements = d_statements(block.statements)
        if block.end is not None:
            ir.map_statement(block.end, as_d)
            ir.map_statement(block.end, byte_of_d)


# ---- the flags as the core's passes read them ----

# the 6809's branch conditions (the low nibble of Bcc's opcode, m6809.h's cond_*) as decompiler.function's names of
# the V60's: the same flags, in the forms decompiler/conditions.py turns into comparisons
BRANCH_CONDITIONS = {0x2: "h", 0x3: "nh", 0x4: "nl", 0x5: "l", 0x6: "ne", 0x7: "e", 0x8: "nv", 0x9: "v",
                     0xA: "p", 0xB: "n", 0xC: "ge", 0xD: "lt", 0xE: "gt", 0xF: "le"}


def zext_of(node, value):
    """True for zext(value) (or value itself, already 32 bits)"""
    return node == value or (isinstance(node, ir.Op) and node.name == "zext" and node.args[0] == value)


class DecompileLifter(lift.Lifter):
    """tools/m6809/lift.py's Lifter with the same values in the forms the core's passes read: a subtraction's or
    an addition's flags as carry_sub, overflow_sub, carry_add and overflow_add of its operands (and its borrow or
    carry), its result as a - b, Z as result == 0 and N as its top bit; a branch's condition as the V60's of the same
    flags (decompiler/function.py). MAME's set_flags computes the same bits (C: bit 'width' of the 32-bit result, the
    borrow or the carry; V: (a ^ b ^ r ^ (r >> 1)) at the top, the signed overflow of a - b or a + b); what this
    does not recognize stays the lift's."""

    def condition(self, code):
        if code in BRANCH_CONDITIONS:
            from decompiler.function import flag_condition
            return flag_condition(BRANCH_CONDITIONS[code])
        return super().condition(code)

    def operation(self, a, b, r):
        """('sub' or 'add', carry or borrow in) for r = a - b (- C) or a + b (+ C), at 32 bits; None otherwise"""
        if not isinstance(r, ir.Op):
            return None
        carry = None
        inner = r
        if r.name in ("add", "sub") and isinstance(r.args[0], ir.Op) and r.args[0].name == r.name and \
                zext_of(r.args[1], ir.Flag("cy")):
            carry, inner = ir.Flag("cy"), r.args[0]
        if inner.name in ("add", "sub") and zext_of(inner.args[0], a) and zext_of(inner.args[1], b):
            return inner.name, carry
        if inner.name in ("add", "sub") and zext_of(inner.args[0], a) and isinstance(inner.args[1], ir.Const) and \
                isinstance(b, ir.Const) and inner.args[1].value == b.value and carry is None:
            return inner.name, None    # (INC, DEC: a +- 1)
        if inner.name == "shl" and a == b and zext_of(inner.args[0], a) and isinstance(inner.args[1], ir.Const) and \
                inner.args[1].value == 1 and carry is None:
            return "add", None    # (ASL: a + a)
        if inner.name == "neg" and isinstance(a, ir.Const) and a.value == 0 and zext_of(inner.args[0], b) and \
                carry is None:
            return "sub", None    # (NEG: 0 - b)
        return None

    def flags(self, mask, a, b, r, width):
        found = self.operation(a, b, r)
        if found is None or a.bits != width or b.bits != width:
            return super().flags(mask, a, b, r, width)
        name, carry = found
        if carry is not None:
            carry = self.bind(carry, "c", keep=True)    # (before C changes)
        if mask & lift.CC_H:
            r = self.bind(r, "r")
            self.set_flag("h", lift.bit_of(op_("xor", (op_("xor", (ir.zext(a, 32), ir.zext(b, 32)), 32), r), 32), 4))
        result = op_(name, (a, b), width)
        if carry is not None:
            result = op_(name, (result, ir.zext(carry, width)), width)
        result = self.bind(result, "r")
        borrow = ir.Const(0, 1) if carry is None else carry
        if mask & lift.CC_N:
            self.set_flag("s", lift.bit_of(result, width - 1))
        if mask & lift.CC_Z:
            self.set_flag("z", op_("eq", (result, ir.Const(0, width)), 1))
        if mask & lift.CC_V:
            self.set_flag("ov", op_(f"overflow_{name}", (a, b, borrow), 1))
        if mask & lift.CC_C:
            self.set_flag("cy", op_(f"carry_{name}", (a, b, borrow), 1))
        return result


def op_(name, args, bits):
    return ir.Op(name, tuple(args), bits)
