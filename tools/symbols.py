"""
Name:        symbols.py
Description: symbols.txt read: the names of the 6809 program's routines, labels, data, tables and RAM
Version:     0.1.0
Created:     19-11-2026

A line is   ADDRESS  KIND  NAME  [key=value ...]   [# comment]
    ADDRESS  hex, 0000-FFFF; in the banked window (4000-5FFF) "B:XXXX" with the bank 0-4
    KIND     func (a routine), code (a label the analysis should start from), data, var (RAM),
             table (a jump table of 16-bit code addresses: count=N), file (where the C's files begin),
             overlay (RAM the program copies code to and runs: from=B:XXXX size=N, the bytes it copies)
    keys     count=N (table), noreturn (a func that never returns to its caller), bank=N (a table's targets'
             bank, where they are in 4000-5FFF); a table in the window itself is "B:XXXX" (the bank it is read in);
             jump=XXXX (a table's: the jump or call through a register that reads it, where the analysis cannot
             see which tables that is: tools/m6809/function.py makes the table's entries its choices);
             return=Y (a func entered by a jump with its return address in Y, which it pushes and returns
             through: a call in the C); idle (a code label where the main loop waits for work, with nothing of
             the C's on its stack but itself: where the readable C saves a state, runtime/src/m6809_program.c)
    struct, field, local, param lines are the shared core's (tools/core/disassemble.py: load_names), for the readable
    C: "struct NAME SIZE", "field STRUCT OFFSET NAME TYPE" (OFFSET hex, from where the code's pointer points), "local FUNCTION ADDRESS rN NAME STRUCT" (the register the instruction at ADDRESS writes points at one);
    "below STRUCT SIZE UNDER" (the SIZE bytes right before where a pointer to STRUCT points are an UNDER: the driver's);
    "instance ADDRESS STRUCT NAME" (a STRUCT at that address, NAME its address in the C: the driver's);
    "scoped STRUCT FAMILY OFFSET NAME TYPE" (a field named its own way in the functions named FAMILY or FAMILY_...)
Addresses and names only: no game data lives in symbols.txt.
"""
from dataclasses import dataclass, field


@dataclass
class Symbol:
    address: int
    bank: object
    kind: str
    name: str
    keys: dict = field(default_factory=dict)
    comment: str = ""


# lines for the shared core's decompiler as they are (tools/core/disassemble.py's load_names): the structures the
# readable C reads through pointers, their fields, which register holds one where (tools/m6809/decompile.py)
CORE_KINDS = ("struct", "field", "local", "param", "below", "instance", "scoped")


class Symbols:
    def __init__(self, entries, core_lines=()):
        self.entries = entries
        self.core_lines = list(core_lines)

    def code(self):
        return [s for s in self.entries if s.kind in ("func", "code")]

    def tables(self):
        result = []
        for s in self.entries:
            if s.kind == "table":
                target_bank = s.keys.get("bank")
                result.append((None if target_bank is None else int(target_bank), s.address,
                               int(s.keys.get("count", "1"), 0), s.name, s.bank))
        return result

    def overlays(self):
        """(RAM address, (bank, source address), size) of each overlay"""
        result = []
        for s in self.entries:
            if s.kind == "overlay":
                source, bank = parse_address(s.keys["from"])
                result.append((s.address, (bank, source), int(s.keys["size"], 16)))
        return result

    def ram_code(self, program):
        """{RAM address: byte} of the overlays, read from the ROM they are copied from"""
        code = {}
        for address, (bank, source), size in self.overlays():
            read = program.reader(bank)
            for offset in range(size):
                code[address + offset] = read(source + offset)
        return code

    def no_return(self):
        return [s.address for s in self.entries if s.kind == "func" and "noreturn" in s.keys]

    def names(self):
        return {(s.bank, s.address): s.name for s in self.entries if s.kind in ("func", "code", "data", "table")}


def parse_address(text):
    if ":" in text:
        bank, address = text.split(":", 1)
        return int(address, 16), int(bank)
    return int(text, 16), None


def load(path):
    entries = []
    core_lines = []
    try:
        handle = open(path, encoding="utf-8")
    except FileNotFoundError:
        return Symbols(entries)
    with handle:
        for number, line in enumerate(handle, 1):
            text, _, comment = line.partition("#")
            words = text.split()
            if not words:
                continue
            if words[0] in CORE_KINDS:
                core_lines.append(" ".join(words))
                continue
            if len(words) < 3:
                raise ValueError(f"{path}:{number}: ADDRESS KIND NAME expected")
            address, bank = parse_address(words[0])
            keys = {}
            for word in words[3:]:
                k, _, v = word.partition("=")
                keys[k] = v
            entries.append(Symbol(address, bank, words[1], words[2], keys, comment.strip()))
    return Symbols(entries, core_lines)
