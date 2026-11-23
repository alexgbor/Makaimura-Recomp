"""
Name:        check_functions.py
Description: each function of the readable C (src/) against the original program on MAME's 6809 core, alone:
             from the states the game calls it in (an input script's run) and from those states changed (its
             registers, the work RAM it reads), the work RAM, the board, the registers it hands back and its cycles
             compared (tools/reference/fcheck.cpp)
Version:     0.1.0
Created:     23-11-2026

license:BSD-3-Clause
    python tools/m6809/check_functions.py --write        build/fcheck/calls.c from src/functions.txt (then build)
    python tools/m6809/check_functions.py [--scripts A,B] [--variants N] [--samples N] [--function NAME]

The checks of the whole game (tools/check_translation.py --decompiled) see only what the scripts play; here every
function is run on its own, from many states, and what it does is compared with what the original program does
from the same state. A function the scripts never call is run from the states other functions are called in (its
registers made up: the objects' records, the player's, random). src/functions.txt is written by
tools/m6809/decompile.py: what each function takes and hands back, by register.
"""
import argparse
import os
import subprocess
import sys
from collections import defaultdict

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SIGNATURES = os.path.join(ROOT, "src", "functions.txt")
CALLS = os.path.join(ROOT, "build", "fcheck", "calls.c")
CODE = os.path.join(ROOT, "build", "fcheck", "code.txt")
FCHECK = os.path.join(ROOT, "build", "bin", "fcheck.exe" if os.name == "nt" else "fcheck")
SCRIPTS = ("walk_right", "long_play", "stage1", "stage2", "stage3", "stage4", "stage5", "stage6", "midway1",
           "midway3", "midway4", "tower5", "satan4", "loop2_stage0", "loop2_stage6", "ending", "service_mode",
           "weapons", "cocktail", "dips", "clears")

# the flags of CC by the core's names, their bit
FLAGS = {"carry": 0, "overflow": 1, "zero": 2, "negative": 3, "half_carry": 5, "firq_mask": 6, "entire": 7}
# the registers by the core's names: how the C reads one from fc_registers, its FC_* bits
REGISTERS = {"a": ("r->a", "FC_A"), "b": ("r->b", "FC_B"), "dp": ("r->dp", "FC_DP"), "x": ("r->x", "FC_X"),
             "y": ("r->y", "FC_Y"), "u": ("r->u", "FC_U"), "s": ("r->s", "FC_S")}
# functions the check cannot call alone: in RAM (the boot's test), the reset's, the main loop's (no return), the
# round robin's (it changes stacks)
KINDS = ("real", "plausible", "wild", "unowned")    # (tools/reference/fcheck.cpp)
LEFT_OUT = {"rom_test_in_ram", "reset", "reset_after_ram_test", "main_loop_start", "main_loop", "irq_round_robin"}


def signatures():
    found = []
    with open(SIGNATURES) as lines:
        for line in lines:
            words = line.split()
            if not words:
                continue
            key, name, returned, taken, given, *rest = words
            flags = [word for word in rest if "=" not in word]
            kinds = dict(word.split("=", 1) for word in rest if "=" in word)
            split = (lambda text: [] if text == "-" else [(part.split(":")[0], int(part.split(":")[1]))
                                                         for part in text.split(",")])
            found.append({"key": int(key, 16), "name": name,
                          "returned": None if returned == "-" else split(returned)[0],
                          "taken": split(taken), "given": split(given), "flags": flags, "kinds": kinds})
    return found


def read_value(name, bits):
    if name in FLAGS:
        return f"(r->cc >> {FLAGS[name]} & 1u)"
    if name == "d":
        value = "(r->a << 8 | r->b)"
    else:
        value = REGISTERS[name][0]
    return f"(uint{bits}_t){value}" if bits in (8, 16) else value


def write_value(name, bits, value):
    if name in FLAGS:
        bit = FLAGS[name]
        return [f"r->cc = (r->cc & ~(1u << {bit})) | (({value}) & 1u) << {bit};"]
    if name == "d":
        if bits == 8:
            return [f"r->b = (uint8_t)({value});"]
        return [f"r->a = (uint8_t)(({value}) >> 8);", f"r->b = (uint8_t)({value});"]
    return [f"{REGISTERS[name][0]} = ({value}) & 0x{(1 << (8 if name in ('a', 'b', 'dp') else 16)) - 1:X}u;"]


def compared_bits(name, bits):
    if name in FLAGS:
        return [f"(1u << (FC_CC_SHIFT + {FLAGS[name]}))"]
    if name == "d":
        return ["FC_B"] if bits == 8 else ["FC_A", "FC_B"]
    return [REGISTERS[name][1]]


def c_type(bits):
    return {8: "uint8_t", 16: "uint16_t"}.get(bits, "uint32_t")


def write_calls():
    functions = [f for f in signatures() if f["name"] not in LEFT_OUT and not f["flags"]]
    lines = ["/* Written by tools/m6809/check_functions.py from src/functions.txt (the user's ROM's): never committed */",
             '#include "game.h"', '#include "fcheck.h"', ""]
    # what every caller agrees a register holds at the start, which the C may take as given (fcheck.h)
    for index, function in enumerate(functions):
        kinds = []
        for register, kind in sorted(function["kinds"].items()):
            what, value = kind.split(":")
            if register in ("x", "y", "u", "d") and what in ("global", "object"):
                kinds.append(f"{{ '{register}', '{what[0]}', 0x{int(value, 16):04X}u }}")
        function["kind_count"] = len(kinds)
        if kinds:
            lines.append(f"static const fc_entry_kind kinds_{index}[] = {{ {', '.join(kinds)} }};")
    lines += ["", "const fc_function fc_functions[] = {"]
    for index, function in enumerate(functions):
        bits = []
        for name, width in function["given"] + ([function["returned"]] if function["returned"] else []):
            bits += compared_bits(name, width)
        kinds = f"kinds_{index}, {function['kind_count']}u" if function["kind_count"] else "0, 0u"
        lines.append(f"\t{{ 0x{function['key']:05X}u, \"{function['name']}\", {' | '.join(bits) or '0'}, {kinds} }},")
    lines += ["};", f"const unsigned fc_function_count = {len(functions)}u;", "",
              "void fc_call(unsigned index, fc_registers *r)", "{", "\tswitch (index)", "\t{"]
    for index, function in enumerate(functions):
        body, arguments = [], []
        for name, width in function["taken"]:
            arguments.append(read_value(name, width))
        for name, width in function["given"]:
            body.append(f"{c_type(width)} out_{name} = {read_value(name, width)};")
            arguments.append(f"&out_{name}")
        call = f"{function['name']}({', '.join(arguments)})"
        if function["returned"]:
            name, width = function["returned"]
            body.append(f"const {c_type(width)} returned = {call};")
        else:
            body.append(f"{call};")
        for name, width in function["given"]:
            body += write_value(name, width, f"out_{name}")
        if function["returned"]:
            body += write_value(function["returned"][0], function["returned"][1], "returned")
        lines.append(f"\tcase {index}u:")
        lines.append("\t{")
        lines += ["\t\t" + line for line in body]
        lines.append("\t\tbreak;")
        lines.append("\t}")
    lines += ["\t}", "}", ""]
    os.makedirs(os.path.dirname(CALLS), exist_ok=True)
    with open(CALLS, "w", newline="\n") as out:
        out.write("\n".join(lines))
    print(f"{CALLS}: {len(functions)} functions")
    # every instruction the static analysis found, by the decompiler's number: where MAME's core runs elsewhere
    # (a jump through a table past its end, on a changed state), the C cannot follow, and the run is not compared
    sys.path.insert(0, os.path.join(ROOT, "tools"))
    sys.path.insert(0, os.path.join(ROOT, "tools", "m6809"))
    import makaimura_rom as rom
    import translate
    program, syms, code, _ = translate.load_code(rom.default_paths(ROOT))
    from m6809 import analysis

    def number(bank, address):
        bank, address = analysis.key(bank, address)
        return address if bank is None else (bank + 1) << 16 | address
    numbers = sorted(number(bank, address) for bank, address in code.instructions)
    with open(CODE, "w", newline="\n") as out:
        out.write("".join(f"{n:X}\n" for n in numbers))
        # and each jump through a table, the table and the targets the analysis found ("T jump table count
        # target..."): the original reading past the table (an index past its end, on a changed state) is off the
        # program too
        for (bank, address), (table, targets) in sorted(code.jump_tables.items(), key=lambda item: number(*item[0])):
            out.write(f"T {number(bank, address):X} {table:X} {len(targets)} "
                      + " ".join(f"{number(bank, t):X}" for t in targets) + "\n")
        # and the tables symbols.txt gives a jump (jump=: the table's base comes from a caller), one line each
        for s in syms.entries:
            if s.kind == "table" and "jump" in s.keys:
                read = program.reader(s.bank)
                count = int(s.keys.get("count", "1"), 0)
                target_bank = int(s.keys["bank"]) if "bank" in s.keys else s.bank
                targets = [read(s.address + 2 * i) << 8 | read(s.address + 2 * i + 1) for i in range(count)]
                out.write(f"T {number(s.bank, int(s.keys['jump'], 16)):X} {s.address:X} {count} "
                          + " ".join(f"{number(target_bank, t):X}" for t in targets) + "\n")
    print(f"{CODE}: {len(numbers)} instructions")


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[2])
    parser.add_argument("--write", action="store_true", help="write build/fcheck/calls.c, then stop")
    parser.add_argument("--scripts", help="the input scripts whose runs give the states (default: all)")
    parser.add_argument("--accuracy", default="mame", choices=("mame", "board"))
    parser.add_argument("--samples", type=int, default=8, help="states kept for each function in each script")
    parser.add_argument("--variants", type=int, default=24, help="changed states run from each state kept")
    parser.add_argument("--function", help="only this function (its name)")
    parser.add_argument("--seed", type=int, default=1)
    parser.add_argument("--show", default="real,plausible", help="the kinds whose differences are listed")
    options = parser.parse_args()
    if options.write:
        write_calls()
        return 0
    scripts = options.scripts.split(",") if options.scripts else list(SCRIPTS)
    # name -> per kind (real, plausible, wild, unowned): [runs compared, differing, off the program]
    totals = defaultdict(lambda: [[0, 0, 0] for _ in KINDS])
    details = defaultdict(dict)    # name -> kind -> the first difference
    for script in scripts:
        path = os.path.join(ROOT, "tests", "controls", f"{script}.txt")
        # (as tools/check_translation.py: the script's last frame and 600 more)
        last = max((int(line.split()[0]) for line in open(path) if line.strip() and not line.startswith("#")),
                   default=600)
        command = [FCHECK, "--accuracy", options.accuracy, "--samples", str(options.samples),
                   "--variants", str(options.variants), "--seed", str(options.seed), "--inputs", path,
                   "--frames", str(last + 600)]
        if options.function:
            command += ["--function", options.function]
        print(f"{script}...", flush=True)
        run = subprocess.run(command, cwd=ROOT, capture_output=True, text=True)
        if run.returncode not in (0, 1):
            print(run.stdout[-2000:], run.stderr[-2000:])
            return 2
        for line in run.stdout.splitlines():
            fields = line.split("\t")
            words = fields[0].split()
            if len(words) != 2 + 3 * len(KINDS) or words[0] != "F":
                continue
            name = words[1]
            for k in range(len(KINDS)):
                for i in range(3):
                    totals[name][k][i] += int(words[2 + 3 * k + i])
            for first in fields[1:]:
                kind = first.split()[0]
                details[name].setdefault(kind, f"{script}: {first}")
    print(f"{len(totals)} functions")
    for k, kind in enumerate(KINDS):
        runs = sum(total[k][0] for total in totals.values())
        off = sum(total[k][2] for total in totals.values())
        differing = sorted(name for name, total in totals.items() if total[k][1])
        print(f"{kind}: {runs} runs ({off} more off the program), {len(differing)} functions differ")
        for name in differing if kind in options.show.split(",") else ():
            print(f"  {name}: {totals[name][k][1]} of {totals[name][k][0]}; {details[name].get(kind, '')}")
    never = sorted(name for name, total in totals.items() if not total[0][0])
    print(f"{len(never)} functions the scripts never call (run from the others' states: unowned)")
    return 1 if any(total[0][1] or total[1][1] for total in totals.values()) else 0

if __name__ == "__main__":
    sys.exit(main())
