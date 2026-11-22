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
5. **Two accuracy modes, always switchable** (`docs/accuracy.md`): *MAME mode* is MAME 0.289's board exactly and
   must keep matching MAME (`tools/mame_trace.py`); *board mode* adds what the real PCB does and MAME does not
   (the sprite DMA halting the CPU, the palette's bus conflicts..., after jotego's jt_gng research: facts only,
   jt_gng is GPL-3). Board mode is the final goal and the game's default once done, but both stay playable: the
   player chooses "1:1 with MAME" or "the board" in the game. Differences live in the board model behind
   `gng.accuracy`, never in the translated program; list each one in `docs/accuracy.md`.
6. **`tools/core` is a submodule** (arcade-decomp, CapsLock4284's `timing-model` branch). Do not edit it in
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
| `tools/makaimura_rom.py` | the ROM set (files, CRC32, SHA-1, regions), the 6809's view of its ROM and banks; `--extract` writes `build/makaimurg.regions` for the tools |
| `tools/m6809/` | the 6809 frontend: `disassembler.py`, `analysis.py` (code discovery with bank tracking), `lift.py` (each instruction into the core's IR, cycle by cycle as MAME's microcode), `interpreter.py` and `compare_lift_with_mame.py` (the lift against MAME's core), `translate.py` (the program as C: `build/generated/program`); the readable C (M6, `docs/decompilation.md`): `function.py` (the program's functions in the decompiler's blocks, the board's time kept), `decompile.py` (the core's passes: `src/`), `emit.py` (the 6809's board for the core's writer), `compare_dumps.py` |
| `tools/z80cpu/` | the same for the sound program (Z80): `analysis.py`, `lift.py`, `interpreter.py`, `compare_lift_with_mame.py`, `translate.py` (`build/generated/sound`) |
| `tools/disassemble.py` | the static analysis summary and the listing (`build/makaimurg.lst`) |
| `tools/symbols.py`, `symbols.txt` | the program's names: routines, tables, overlays, RAM |
| `tools/core/` | arcade-decomp (submodule): the IR, its C printer, the decompiler |
| `tools/reference/` | `gref.cpp`: the original program on MAME 0.289's 6809 core against the board model; `sound/`: the sound board on MAME's Z80 and ymfm (`gsound`), `ymcheck`, `oracle_z80_step`; `oracle_m6809*`; `mame/`: MAME's and ymfm's vendored sources (never in the game) |
| `tools/mame/trace.lua`, `tools/mame_trace.py` | MAME itself traced and compared with gref or gng_headless (bus, IRQs, pictures, sound) |
| `tools/check_translation.py` | the translated game (gng_headless) against gref: bus, IRQs, pictures, YM2203 writes, the sound's WAV; `--decompiled`: the readable C (gng_headless_c), the bus at the IRQs but the stack |
| `tools/check_resampler.py` | the window's resampler against scipy's |
| `tools/coverage.py` | what the checks run: gref's executed addresses on every script against the static analyses, the code never run by routine, the ROM's map |
| `tools/check_states.py` | save states: a run from a state against the uninterrupted run (gng_headless `--save-state`, `--load-state`) |
| `runtime/` | the board model in C: `board.c` (bus, banks, DMA, LS259, inputs), `video.c`, `m6809.c` and `z80.c` (the CPUs the translated programs run on), `m6809_program.c` (the 6809 the readable C runs on: `m6809/program.h`), `sound.c`, `ym2203.c` (after ymfm), `state.c` (save states), `rom_set.c` and `inflate.c` (the set from its zips) |
| `ports/headless/` | `gng_headless`: the translated game with no window, gref's outputs, for the checks; `gng_headless_c`: the same on the readable C |
| `tools/game_profile.py` | what the core's modules ask of the game as they are imported (the 6809's frontend sets the rest itself) |
| `ports/sdl2/` | `makaimura`: the game in a window (on the readable C; `makaimura_translated` on the translation); `settings.c` (makaimura.ini), `audio_out.c`, `resampler.c`, `menu.c`, `recorder.c` (AVI recordings), `hud.c` (the side panels), `hiscore.c` (makaimura.hi), `replays.c`, `rom_drop.c` (the ROM set's window), `updater.c` (updates from GitHub's releases), `crt.c` (the CRT look), `release/` (the package's README.txt, NOTICES.txt). Check options: `--hidden`, `--inputs`, `--frames`, `--shot`, `--menu`, `--menu-keys`, `--settings`, `--accuracy`, `--record`, `--record-inputs`, `--save-state`, `--load-state`, `--high-scores`, `--keep-replays`, `--drop`, `--drop-shot`, `--update-test` |
| `tools/make_release.ps1`, `CHANGELOG.md` | the player's package (`release/`, never committed): the version is CMakeLists.txt's `project(makaimura VERSION)`, its section in CHANGELOG.md |
| `tools/side_by_side.py` | two recordings side by side (ffmpeg), for the modes' comparison |
| `tests/` | `controls/*.txt` input scripts (walk_right, long_play, stage1-6, loop2_stage0/6, ending, service_mode, weapons, cocktail, dips, clears: `make_*.py` write them), `resample_check.c` |
| `docs/hardware.md`, `docs/accuracy.md`, `docs/decompilation.md` | the board as MAME describes it and what the program was found to do; the two accuracy modes and where MAME and the board differ; the readable C (M6): how it is made, what it does differently, how it is checked |

## Working

- Build and check: `python tools/m6809/translate.py`, `python tools/z80cpu/translate.py`, `python
  tools/m6809/decompile.py` (the readable C, `src/`: the game's 6809 program), `cmake -S . -B build/msvc && cmake
  --build build/msvc --config Release`, then `python tools/check_translation.py --decompiled` and `python
  tools/check_translation.py` (the attract mode and every script, in one mode: `--accuracy mame` or `board`; about
  15-20 minutes each) after any change to the runtime, the translators or the decompiler's frontend; `python
  tools/check_states.py --program build/bin/gng_headless_c.exe` for the states. `makaimura` is the game on the
  readable C; `makaimura_translated` the same window on the translation, to compare.
- Check the window without disturbing whoever uses the computer: `makaimura --hidden` or `--shot N FILE`, sound
  through `SDL_AUDIODRIVER=disk` (or `volume = 0` in a `--settings` file); never show windows or capture the
  desktop.
- Python 3.10+, run tools from the repository root: `python tools/disassemble.py --unresolved --listing`.
  Python that reads ROM-derived data runs with `-I` when it reads untrusted downloads.
- C: CMake 3.20+ and MSVC (Visual Studio 2022 Build Tools are installed) or gcc/clang. C11 for the runtime,
  C++20 only in the reference tools (MAME's cores).
- MAME 0.289 (the official Windows build, outside the repository) is at `D:/repositories/mame0289/mame.exe`
  (or the `MAME` environment variable). `python tools/mame_trace.py --frames N [--inputs FILE] [--dumps]` runs
  MAME's own makaimurg with `tools/mame/trace.lua` and compares it with `gref` frame by frame.
- Input scripts are `tests/controls/*.txt`: "FRAME PORT VALUE" (the byte the board reads, active low, from the
  VBLANK after FRAME on); the same file drives `gref --inputs` and MAME. "FRAME mADDR VALUE" writes the work RAM
  (a test's cheat: the stage, the lives, to reach later scenes); gref, gng_headless and makaimura take it, MAME's
  trace does not (`make_stages.py`'s scripts are for gref against the translation).
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
