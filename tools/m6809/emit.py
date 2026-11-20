"""
Name:        emit.py
Description: writes the 6809 program's functions as C through the core's writer (decompiler/emit.py), with the
             board's memory and the timing's effects (milestone M6)
Version:     0.1.0
Created:     20-11-2026

license:BSD-3-Clause
The decompiler's FunctionWriter does the work; this gives it the 6809's map (gng.cpp's gng_main_map) for
decompiler/memory.py and decompiler/pointers.py: the work RAM (0000-1DFF) holds the variables, read and written as
lvalues over the board's bytes (RAM8(address): runtime/include/m6809/program.h); the fixed program ROM (6000-FFFF)
the tables, read through ROM8(address); the sprite RAM, the video RAMs, the inputs, the palette, the devices and the
banked window (1E00-5FFF) are the board's, reached through its bus by name (mem_read8, mem_write8: a device's time
is the one the m6809_at before the access set). A computed address no name explains goes over the bus too.

The timing's effects (tools/m6809/function.py) print as calls of the runtime: m6809_block(cycles) after each
instruction, m6809_cycles(n) on a long branch's longer way, m6809_at(cycle) before an access whose time shows,
m6809_mask(i) where the program sets the interrupt mask, m6809_sync() for SYNC, m6809_idle(u) where the main loop
waits for work; the mask read is m6809_masked().
"""
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "core"))

from decompiler import cprint  # noqa: E402
from decompiler import emit  # noqa: E402
from decompiler import memory  # noqa: E402
from decompiler import pointers  # noqa: E402
from v60 import ir  # noqa: E402

WORK_RAM = (0x0000, 0x1E00)
PROGRAM_ROM = (0x6000, 0x10000)
# the board's places the code reaches through the bus (gng.cpp's gng_main_map)
HARDWARE = [(0x1E00, 0x2000, "SPRITE_RAM"), (0x2000, 0x2800, "TEXT_RAM"), (0x2800, 0x3000, "BACKGROUND_RAM"),
            (0x3000, 0x3800, "INPUT"), (0x3800, 0x3A00, "PALETTE"), (0x3A00, 0x4000, "DEVICE"),
            (0x4000, 0x6000, "BANKED_ROM")]
NOTES = {
    "variables": ["/* the game's variables in work RAM */"],
    "scoped": ["/* work RAM as one family of functions uses it */"],
    "tables": ["/* data in the program ROM the code reads */"],
    "hardware": ["/* the board's places, read and written through its bus */"],
}


def use_memory_map(names):
    """Makes the 6809's memory the one decompiler/memory.py and decompiler/pointers.py work on: 'names'
    (disassemble.Names) gives the structures"""
    memory.WORK_RAM = WORK_RAM
    memory.ROM_AREAS = (PROGRAM_ROM,)
    memory.ADDRESS_MASK = 0xFFFF
    memory.ADDRESS_DIGITS = 4
    memory.RAM_MACRO, memory.ROM_MACRO = "RAM", "ROM"
    memory.RAM_FUNCTION, memory.ROM_FUNCTION = "ram_at", "rom_at"
    memory.LOADED_TABLES = False
    memory.COUNTED_FROM_ONE = False
    memory.NAMES_BEFORE_TABLES = 0
    memory.PAGED = False
    memory.PLAYER_FIELDS = None
    memory.CANONICAL = lambda address: address
    memory.ROM_NAMES_FROM = PROGRAM_ROM[0]
    memory.VARIABLES = None
    memory.HARDWARE = list(HARDWARE)
    memory.PLAIN_HARDWARE = ()
    memory.BUS_ONLY = []
    memory.BYTE_INDEXES = True
    memory.NAMED_BASES = {}
    memory.NOTES = dict(NOTES)
    pointers.STRUCTS = dict(names.structs)
    pointers.PLAYERS = (0, 0, 0)
    pointers.VARIABLES = None
    pointers.CONTEXT_KINDS = {}
    pointers.OBJECT_REGISTERS = ()
    pointers.HANDLER_FIELD = None
    pointers.WORK_RAM = WORK_RAM
    pointers.ADDRESS_MASK = 0xFFFF
    pointers.POINTER_BITS = 16
    pointers.PAGE_LINKS = False
    pointers.ROM_DATA = PROGRAM_ROM
    pointers.ARRAYS = {}
    pointers.INSTANCES = {}
    pointers.VARIABLE_BLOCKS = {}
    pointers.BASE_KINDS = {}
    pointers.PAGE_LINK_KINDS = {}
    pointers.NESTED = {}
    pointers.BELOW = {}
    pointers.OWN_REGISTERS = ()
    pointers.STRUCT_FUNCTION = "ram_at"
    pointers.ROM_STRUCTS = set(names.rom_structs)
    pointers.RUNS = set(names.rom_structs)
    pointers.ELEMENTS = dict(names.arrays)
    pointers.RECORD_TABLES = dict(names.record_tables)
    pointers.FIELD_ARRAYS = {key: (int(names.fields[key][1][1:]), count) for key, count in names.field_arrays.items()}
    pointers.ROM_FUNCTION = "rom_at"
    pointers.CONSTANTS = True
    pointers.PAGED = False


class Names(emit.Names):
    def memory(self, load):
        found = super().memory(load)
        if found is not None:
            return found
        # a computed address: over the board's bus
        return f"mem_read{load.bits}({self.writer.exact(load.address)})", False


class Printer(emit.Printer):
    def __init__(self, writer):
        super().__init__(writer)
        self.names = Names(writer)

    def helper(self, name, args, bits):
        if name in ("m6809_masked", "m6809_bank_code"):
            values = ", ".join(self.exact(arg).text for arg in args)
            return cprint.Value(f"{name}({values})", cprint.PRIMARY, True, "u32")
        return super().helper(name, args, bits)


class Writer(emit.FunctionWriter):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)
        self.printer = Printer(self)

    def statement(self, statement):
        if isinstance(statement, ir.Store):
            target = self.memory(statement.address, statement.bits, id(statement))
            if target is not None and target[2]:
                return [self.assignment(target[0], statement, target[1])]
            return [f"mem_write{statement.bits}({self.exact(statement.address)}, "
                    f"{self.stored(statement.value, statement.bits)});"]
        return super().statement(statement)

    def effect(self, statement):
        name = statement.name
        args = statement.args
        if name in ("m6809_block", "m6809_at"):
            return [f"{name}({args[0].value});"]
        if name in ("m6809_cycles", "m6809_mask", "m6809_idle"):
            return [f"{name}({self.exact(args[0])});"]
        if name == "m6809_sync":
            return ["m6809_sync();"]
        raise emit.EmitError(f"{self.function.name}: effect {name}")


class Emitter(emit.Emitter):
    """emit.Emitter with nothing of the V60 program's: no context registers"""

    def __init__(self, functions, signatures, names, memory_, always, layouts, read):
        super().__init__(functions, signatures, names, (), set(), memory_, always, read)
        self.layouts = layouts
        self.saved_locals = True
        self.flow = None

    def restores_current(self, entry):
        return None, None

    def address_text(self, value):
        """A constant that is a work RAM address is named only when symbols.txt names what is there and it is past
        the direct page: the 6809's work RAM starts at 0, so a count, an offset or a mask is as likely (LDY #$0060,
        LEAX 1,X)"""
        if memory.work_ram(value & 0xFFFF) and value == value & 0xFFFF:
            item = self.memory.variables.get(value)
            if value < 0x100 or item is None or item.name.startswith("unk_"):
                return None
        return super().address_text(value)

    def code_case(self, choice):
        """A call through a table's case: the code's address (the bank is the function's: function.key)"""
        return f"0x{choice & 0xFFFF:04X}u"
