# AGENTS.md — working on Makaimura Recomp

Instructions for any agent (or person) working in this repository. Read this first, then `TODO.md`.

## The goal

A native PC port of Capcom's 1985 arcade **Makaimura** (Japan revision G, MAME's `makaimurg`) that is **100%
faithful to the original board**: the game's own 6809 program, translated ahead of time into C, running on a
model of the board, doing exactly what the arcade does, frame for frame — its timing and slowdown, its picture
and its sound included. Not an emulator, not a remake. The same approach as
[DonPachi Recomp](https://github.com/CapsLock4284/DonPachi-Recomp) and
[DoDonPachi Recomp](https://github.com/CapsLock4284/DoDonPachi-Recomp), on a different board.

"Faithful" is measured, never assumed: every claim of fidelity is backed by a check against MAME (its CPU
cores running the original program, and MAME itself for the picture and the sound).

## Hard rules

1. **No ROM data in the repository, ever.** Not the ROMs, not anything extracted or derived from them
   (decompiled C, listings, graphics, samples, tables copied out of the program, hashes of frames are fine).
   `rom/`, `src/`, `build/`, `assets/`, `*.dat`, `*.lst` are git-ignored. `symbols.txt` holds addresses and
   names only. Before committing, check that nothing derived from the ROM is staged.
2. **MAME is the reference.** The hardware behaviour comes from MAME's `src/mame/capcom/gng.cpp` and its
   devices (MC6809, Z80, YM2203, buffered sprite RAM, LS259). When the code and MAME disagree, MAME wins until
   a check against the real board says otherwise. Cite the MAME source a behaviour comes from in the comment.
3. **Verify, then claim.** A change to the translator, the runtime or the board model is checked against the
   reference (frame hashes, `tests/`) before it is called done. Report failures as they are.
4. **Licences.** MAME's and ymfm's code is BSD-3-Clause: it may be vendored into `tools/reference/` (the
   verification tools) with its copyright headers kept. The game's runtime is this project's own code, written
   after MAME's drivers (credited in each file's header).
5. **`tools/core` is a submodule** (arcade-decomp, CapsLock4284's `timing-model` branch). Do not edit it in
   place; the 6809 frontend lives in this repository (`tools/m6809/`) until it is worth contributing upstream.

## The board, in one paragraph

MC6809 at 1.5 MHz (6 MHz crystal input, E = /4) with an IRQ at each VBLANK (MAME: `irq0_line_hold`); 7.5 KB of
work RAM, 512 bytes of sprite RAM copied by a DMA (triggered by a write to 3C00, halting the CPU) into the
video's buffer; an 8x8 text layer (2 bpp) and a 16x16 scrolling background (3 bpp, two priority groups split
front/back by pen); 16x16 sprites (4 bpp); 256 colours of RGBx 444. Program ROM at 6000-FFFF, plus a banked
window 4000-5FFF (banks 0-4, latch at 3E00). Sound: a Z80 at 3 MHz with its own program, IRQ 4 times a frame,
a sound latch from the 6809, and two YM2203 at 1.5 MHz. Screen 256x224 visible of 384x262 at 6 MHz
(59.637 Hz). Details and findings: `docs/hardware.md`.

## Layout

| Path | What |
|---|---|
| `tools/makaimura_rom.py` | the ROM set (files, CRC32, SHA-1, regions), the 6809's view of its ROM and banks |
| `tools/m6809/` | the 6809 frontend: `disassembler.py` (decode, flow, text), `analysis.py` (code discovery with bank tracking); later `lift.py`, `timing.py`, `emit.py` |
| `tools/disassemble.py` | the static analysis summary and the listing (`build/makaimurg.lst`) |
| `tools/symbols.py`, `symbols.txt` | the program's names: routines, tables, RAM |
| `tools/core/` | arcade-decomp (submodule): the decompiler, the Z80 frontend, the verification tools' patterns |
| `tools/reference/` | (planned) `gref`: the original program on MAME's 6809 and Z80 cores against the board model |
| `runtime/` | (planned) the board model in C: bus, banks, video, DMA, inputs, sound latch, Z80 + YM2203s |
| `ports/` | (planned) `headless/` for checks, `sdl2/` for the game in a window |
| `tests/` | (planned) frame-by-frame checks against the reference |
| `docs/hardware.md` | the board as MAME describes it, and what the program was found to do |

## Working

- Python 3.10+, run tools from the repository root: `python tools/disassemble.py --unresolved --listing`.
  Python that reads ROM-derived data runs with `-I` when it reads untrusted downloads.
- C: CMake 3.20+ and MSVC (Visual Studio 2022 Build Tools are installed) or gcc/clang. C11 for the runtime,
  C++20 only in the reference tools (MAME's cores).
- The ROM set goes in `rom/`: `makaimurg.zip` (the clone's 5 files) and `gng.zip` (the parent's: the sound
  program, graphics, PROMs). `python tools/makaimura_rom.py` lists what is there.
- Documentation, comments and commit messages are in English (as the sibling projects). The project owner
  talks in Spanish: answer them in Spanish.
- Comments say what the code does and why, and where a fact comes from (MAME's file, an address in the
  program, a check that proved it). Keep `docs/hardware.md` and `symbols.txt` up to date as things are found.
- Tick `TODO.md` as milestones are reached; add what is found to do. Commit in small, checked steps.

## Commits

Small, one subject each, message in the style of the sibling projects (a sentence saying what changed).
End with the attribution line the session gives. Never commit `rom/`, `build/`, `src/` or anything from the ROM.
