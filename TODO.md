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
- [x] No illegal opcode reached, no flow out of the ROM: the tables the analysis read past their end have their
      sizes in symbols.txt (7EFF, 9AA8, B103, B196: data after them that looked like addresses), and a table in the
      window is read in its own bank (4:55E4 at DD71, 4:4942 at A0BE: read with no bank they gave FFFF); a size in
      symbols.txt wins over the analysis's guess
- [ ] Still unresolved: 6F76 `JMP [B,X]`, AA36 `JSR [B,U]` (and 9AF4, 9F2D's described in symbols.txt)
- [~] Input scripts for the reference (`tests/controls/`): walk_right (a coin, start, walking, jumping, throwing),
      long_play (`make_long_play.py`: 40000 frames, four credits, a two-player game, pseudo-random moves, the
      service switch); every instruction they run is found statically (6945 addresses); to do: the whole game,
      both loops, every weapon and enemy (scripts that get further: the stages need a player who survives)
- [x] The object dispatch at 9AF4: U from the table of tables at 9AFE by the object's type (0-5), JSR [A,U] by its
      state (0-3): its six tables in symbols.txt (long_play met the types walk_right had not)
- [ ] The two-level table at 6F6B (by <$72 and <$0B): its counts are what the traces saw; find them in the code
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
      every VBLANK, and every picture (`--picture-hashes`): the attract mode (1200 frames, 890 IRQs), walk_right
      (7596 frames, 7286 IRQs) and long_play (39700 frames, 39390 IRQs) the same; `tools/mame_trace.py --against gng_headless`: against MAME itself, walk_right's 7001
      frames and 7000 pictures the same. 7001 frames run in 3.9 s with a picture each
- [ ] The whole game, both loops, frame by frame against the reference (input scripts that reach every stage)

## M5. The sound program (Z80)

- [x] The sound program's lift (`tools/z80cpu/lift.py`) after MAME 0.289's z80.lst: every access at its machine
      cycle's start, the flags the program can see (S Z H P/V N C; it never stores F, has no DAA, no LD A,R: the
      translator refuses them); checked against MAME's core (`build/bin/oracle_z80_step`,
      `tools/z80cpu/compare_lift_with_mame.py`): every instruction of the program and the IRQ's entry, 47040 cases
- [x] Its static analysis (`tools/z80cpu/analysis.py`): RST $30 is a jump through the words after it, a return
      address pushed by hand is code; every address the reference's Z80 ran is found (1177 instructions)
- [x] Translated (`tools/z80cpu/translate.py` -> build/generated/sound) on the runtime's Z80 (`runtime/src/z80.c`:
      a trampoline run up to a time, the IRQ a jump to 0038, EI's delay, HALT) and the board's sound side
      (`runtime/src/sound.c`: the latch by time, the 240 Hz IRQ, Q1's reset, MAME's start a cycle late):
      walk_right's 1,201,454 YM2203 writes the same as the reference's (so as MAME's) to the cycle, and the Z80's
      RAM at each of them (`tools/check_translation.py`)
- [x] The game's own YM2203 (`runtime/src/ym2203.c`, after ymfm) sample for sample the same as ymfm's
      (`build/bin/ymcheck`, the reference's write logs: walk_right's 14.7 million samples a chip, long_play's 83.2
      million, every output); the program selects prescale 3 (addresses 2D then 2E): the FM every 3 samples, the
      SSG resampled 2:3; on the game's sound board (`runtime/src/sound.c`): its mix the same as the reference's
      WAV byte for byte (`tools/check_translation.py`). The plan it followed:
      - `runtime/src/ym2203.c` after ymfm (credited): what ym2203 uses of it, at MAME's settings: medium fidelity,
        prescale 6 (the FM clocked every 6 output samples, its output through the DAC's 10.3 float round trip),
        the SSG resampled 4:3 (ssg_resampler); the FM engine for OPN (opn_registers: 3 channels, no LFO), its
        operators (phase, envelope with its rates and the SSG-EG, the attenuation tables), the timers (A for CSM)
      - `tools/reference/sound/ymcheck.cpp`: a write log (gref --ym-writes) fed to ymfm's ym2203 and to the C one at
        the same samples (sample n from Z80 time 24 n, a write before the samples that start after it), every
        output (FM, SSG A, B, C) compared; then the game's mix (MAME's routes) and its samples by frame
      - the headless runner and the check: the samples' hashes against gref's (`--wav` already writes the mix)
- [x] The game's sound against the reference's: the YM2203 writes to the cycle, the samples, the mix
- [ ] The mix out to the host: resampled from 125 kHz to the device's rate (the port, M7)

## M6. Readable C

- [ ] The core's decompiler on the 6809's IR (structured C, no CPU left), checked frame by frame again
- [ ] Names: routines and RAM in `symbols.txt` as the game is understood (player, enemies, stages, score...)

## M7. The game in a window (`ports/sdl2`)

- [x] A first window (`ports/sdl2/main.c`, `build/bin/makaimura`): the picture scaled, keyboard (MAME's keys, and Z/X)
      and a gamepad, the 4-way joystick, the sound (the board's 125 kHz mix averaged down to the device's rate),
      the board's own pace (59.637 Hz), F3 reset, a message where the program faults
- [ ] Settings, the ROM set read from its zips (no regions file), a 4:3 picture, a better resampler, vsync
- [ ] DIP switches as game options (lives, difficulty, bonus, the invulnerability DIP of the Japanese sets)
- [ ] From DonPachi Recomp's port, as wanted: save states, replays, training, CRT look, high scores kept

## Later, maybe

- [ ] The other sets: makaimurc, makaimurb, makaimur, gng (World), gngt (US)...
