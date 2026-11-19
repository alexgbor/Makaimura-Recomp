# Makaimura Recomp: the plan

Roughly in order; each milestone is checked against the reference before it is ticked. `[x]` done, `[~]` started.

## M0. The repository

- [x] Repository, `AGENTS.md`, this plan, `docs/hardware.md`, `.gitignore` (nothing from the ROM)
- [x] arcade-decomp as `tools/core` (CapsLock4284/arcade-decomp, `timing-model`, as DonPachi Recomp's)
- [x] The ROM set: `tools/makaimura_rom.py` (MAME's makaimurg and its parent gng: CRC32, SHA-1, regions, banks)
- [x] The parent set `gng.zip` (sound program, graphics, PROMs): all 19 files with MAME's SHA-1; MAME 0.289 says
      "romset makaimurg [gng] is good"
- [x] Licence: BSD-3-Clause (`LICENSE`), as the sibling projects

## Accuracy: MAME mode, then board mode (`docs/accuracy.md`)

- [x] Decided: match MAME exactly first; then the real board's behaviour 1:1 (jotego's jt_gng research), as a
      switchable mode (`gng.accuracy`, `gref --accuracy`), board mode the final default
- [ ] Board mode: the sprite DMA's halt of the 6809 (its length, when the CPU stops)
- [ ] Board mode: the palette's bus conflicts during the active display (the "lightning" glitch)
- [ ] Board mode: does a read of 3C00 start the DMA (the reset's `LDA $3C00`)?

## M1. The 6809 program, read

- [x] Disassembler (`tools/m6809/disassembler.py`): the three pages, every addressing mode, control flow
- [x] Disassembler checked against MAME's (`build/bin/oracle_m6809`, `tools/m6809/compare_with_mame.py`): the same
      at every address of 6000-FFFF and of each bank, and for every opcode with every postbyte
- [~] Static analysis (`tools/m6809/analysis.py`): from the vectors, bank tracking through the 3E00 latch, the
      jump tables found from `LDr #table` before `JMP/JSR [acc,r]` and `LDr acc,r / JSR ,r` (76% of the fixed
      ROM so far); every instruction the reference ran in 1200 frames of the attract mode is found
- [~] The dispatch tables in `symbols.txt` (IRQ states, round robin jobs, the main loop's tasks, the objects')
- [ ] Still unresolved: 6F76 `JMP [B,X]`, AA36 `JSR [B,U]`; 6 illegal opcodes reached (tables read too far:
      B0B9/B131, BA80, 40F3) and a 0000 entry: give those tables their sizes in `symbols.txt`
- [~] Input scripts for the reference (`tests/controls/`): walk_right (a coin, start, walking, jumping, throwing);
      every instruction they run is found statically (16636 instructions, 78.7% of the fixed ROM); to do: the
      whole game, both loops, every weapon and enemy
- [ ] Data-driven dispatches (U from an object's record at 9AF4: 9C97, 9ECC; the two-level table at 6F6B): find
      where those values come from, so that the tables are complete without traces
- [ ] A safety net for the translation: an address the static analysis missed is reported (crash log), never
      run wrongly
- [ ] The RAM overlay: bank 3's 560C-5659 copied to 1A00 and run there (the boot's ROM check)
- [ ] Every jump through a register resolved, checked against the reference's executed addresses
- [ ] Coverage of the program ROM: code, data, fill (all banks)

## M2. The reference (`tools/reference`, "gref")

- [x] MAME's MC6809 core built standalone (`tools/reference/stub/emu.h`; m6809make.py at build time): `gref`
      boots the program to its title (1200 frames in 0.7 s)
- [x] The sound board (`tools/reference/sound/gsound.cpp`, `gref --sound`): MAME 0.289's Z80 and ymfm's YM2203
      (tools/reference/mame: MAME 0.289's own copies; arcade-decomp's are older) on the sound map of gng.cpp, the
      Z80 run lazily up to each event as MAME's scheduler does (the latch's and the LS259's writes at the 6809's
      time, its 240 Hz IRQ restarted as its reset is released, the frame's end), each YM2203 at ymfm's medium
      fidelity (MAME's: 125 kHz), MAME's routes (SSG 0.40, FM 0.20); a WAV of the mix
- [x] Checked against MAME (`tools/mame_trace.py --sound`): walk_right's 214 latch writes, 1,201,454 YM2203 writes
      (Z80 cycle, chip, port, value) and the Z80's RAM at 6673 of them, every one the same. Two MAME effects
      found and documented (docs/accuracy.md #6, #7): its time origin (2110 6809 cycles) and its Z80's start a
      cycle after its reset is released
- [ ] The board model (runtime/) driven by them: frame timing (384x262 at 6 MHz), VBLANK IRQ, sprite DMA
      (and its halt of the CPU), the sound latch, the Z80's 4 IRQs a frame
- [~] Per frame: state hash (`--hashes`), executed addresses with their bank (`--executed`), every transfer
      of control (`--jumps`), registers at an address (`--registers-at`), input scripts (`--inputs`); to do:
      picture hash, sound hash
- [x] Ground truth from MAME itself (`tools/mame_trace.py`, `tools/mame/trace.lua`, MAME 0.289): the bus
      0000-2FFF and the PC at every VBLANK identical to gref's, 1200 frames of the attract mode and 7000 frames
      with a game played (`tests/controls/walk_right.txt`)
- [x] The picture (`--pictures N`): every frame of walk_right (7000) the same as MAME's screen, pixel for
      pixel, upright and flipped (`--dsw1 5F`). MAME's `screen:pixels()` at a frame notifier is the previous
      VBLANK's indexes in this VBLANK's colours: the check samples gref the same way (`--pictures`); `--ppm` is
      the board's own picture
- [x] Frame 0 (the first VBLANK after power-on) differed from MAME's: the screen is flipped from power-on (the
      LS259 starts at 0 and Q0 drives the flip through an inverter) until the boot writes 3D00
- [x] DIP switches from power-on in both runs (`--dsw1`, `--dsw2`; trace.lua's MK_DSW1/2). MAME keeps a game's
      DIP switches in its cfg directory across runs, `-noreadconfig` or not: each MAME run starts from an empty one
- [ ] The reference's mix against MAME's WAV (its mixer, filters and resampler to 48 kHz): by ear and by
      spectrum, not sample for sample

## M3. The board model (`runtime/`)

- [ ] Bus: RAM, sprite RAM, the two layers' RAM, inputs and DIP switches, palette, latch, scroll, DMA, LS259, bank
- [x] Video (`runtime/src/video.c`), checked against MAME's screen: text layer (2 bpp, pen 3 transparent), background (3 bpp, 16x16, column-major, scroll, split
      priority: group 0 all behind, group 1 pens 0 and 6 behind), sprites (4 bpp, buffered, pen 15 transparent,
      drawn from the last to the first), flip screen; 256x224 out of the 256x256 drawn
- [x] Palette RGBx 444 (3800 RG, 3900 B), its power-on contents as MAME's machine_reset
- [x] Flip screen (the LS259's Q0 inverted; DSW1 bit 7, and the second player's turn in a cocktail cabinet):
      the picture turned 180 degrees in its 256x256, as MAME 0.289's tilemaps and sprites flip
- [ ] Sound: the Z80's bus, the latch, two YM2203 (ymfm), the mix (FM 0.40, SSG 0.20 each), YM reset via LS259 bit 1
- [ ] Timing: cycle-exact 6809 (the CPU's cycles as MAME's core counts them), the DMA's stolen cycles, the
      palette's bus conflicts (MAME does not emulate them: note, check)

## M4. The translation (6809 to C)

- [x] `tools/m6809/lift.py`: every instruction into the core's IR (v60/ir.py) as MAME's microcode does it: its bus
      cycles in order, each at its cycle (the opcode space's: operands, dummy reads, VMA reads of FFFF), every
      flag as set_flags sets it; the IRQ's entry (IrqEntry). Its cycles are the instruction's time
- [x] Checked against MAME's core (`build/bin/oracle_m6809_step`, `tools/m6809/compare_lift_with_mame.py`, the IR
      run by `tools/m6809/interpreter.py`): registers, flags, cycles and every bus cycle (kind, address, value,
      cycle) the same, the program's 3983 instructions from 50 states each, and every opcode of the three pages
      with every indexed postbyte (200632 cases)
- [x] The IRQ's timing (MAME's core): an instruction always runs to its end, and interrupts are looked at in MAIN
      only, before the next one, so the IRQ held from the VBLANK is taken at the first instruction boundary at or
      after it (the arcade's slowdown: the frame's work interrupted where its cycles say)
- [x] The translation scaffold (`tools/m6809/translate.py` -> build/generated/program, never committed): every
      instruction the C of its IR, the board's time set before each data access to the end of its bus cycle;
      regions entered at any instruction, a trampoline (`runtime/src/m6809.c`) that dispatches the address a
      region leaves for (the bank's in the window), so the 6809's stack is the only one; the IRQ taken at the
      first instruction boundary at or after the VBLANK, its handler run nested until its RTI comes back;
      SYNC and CWAI in the runtime; an address the analysis did not find is a fault (logged), never run
- [x] Banked code keyed by (bank, address), dispatched with the latch's bank; the RAM overlay at 1A00 translated
      from the ROM bytes the boot copies (symbols.txt's `overlay`), checked in RAM before it runs
- [x] `build/bin/gng_headless` (`ports/headless/main.c`): the translated program on the board model, gref's
      outputs; `tools/check_translation.py`: against gref, the bus at every IRQ taken (with its PC and cycle), at
      every VBLANK, and every picture: the attract mode (1200 frames, 890 IRQs) and walk_right (7596 frames,
      7286 IRQs) the same; `tools/mame_trace.py --against gng_headless`: against MAME itself, walk_right's 7001
      frames and 7000 pictures the same. 7001 frames run in 3.9 s with a picture each
- [ ] The whole game, both loops, frame by frame against the reference (input scripts that reach every stage)
- [ ] The 3 illegal opcodes the analysis reaches (B131, BA80, 4:40F3: tables read too far) are faults if run

## M5. The sound program (Z80)

- [ ] Translated with the core's Z80 frontend (`tools/core/z80`: lift, timing, C), its own timing model (the
      instruction's cycles, the IRQ between the same two instructions as MAME's core), on the game's sound board
- [ ] The game's own YM2203 (FM and SSG) in C, sample for sample against ymfm's (ymfm stays in the tools: BSD-3,
      but the game's chips are its own, as DonPachi's and TMNT's are)
- [ ] The game's sound against the reference's: the YM2203 writes to the cycle, the samples; the mix out

## M6. Readable C

- [ ] The core's decompiler on the 6809's IR (structured C, no CPU left), checked frame by frame again
- [ ] Names: routines and RAM in `symbols.txt` as the game is understood (player, enemies, stages, score...)

## M7. The game in a window (`ports/sdl2`)

- [ ] Window, scaling, input (keyboard, gamepads), sound out, settings, the ROM set dropped on the window
- [ ] DIP switches as game options (lives, difficulty, bonus, the invulnerability DIP of the Japanese sets)
- [ ] From DonPachi Recomp's port, as wanted: save states, replays, training, CRT look, high scores kept

## Later, maybe

- [ ] The other sets: makaimurc, makaimurb, makaimur, gng (World), gngt (US)...
