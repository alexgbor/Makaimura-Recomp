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
- [ ] MAME's Z80 core (tools/core/system32/reference/mame/z80) and ymfm's YM2203 for the sound board
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
- [ ] The sound (MAME's WAV)

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

- [ ] `tools/m6809/lift.py`: every instruction into the core's IR, flags as MAME's core sets them (H for DAA)
- [ ] `tools/m6809/timing.py`: each instruction's cycles as MAME's core (indexed modes' extra cycles), the IRQ
      taken between the same two instructions as on the board (the arcade's slowdown)
- [ ] A first translation (instruction by instruction, every flag) that boots and runs the attract mode,
      identical to the reference frame by frame
- [ ] Banked code: one C function per (bank, routine); the RAM overlay
- [ ] The whole game, both loops, frame by frame against the reference

## M5. The sound program (Z80)

- [ ] Translated with the core's Z80 frontend (`decompile_sound.py`, `sound_symbols.txt`)
- [ ] Sound identical to the reference's (sample hashes), and to MAME's WAV output

## M6. Readable C

- [ ] The core's decompiler on the 6809's IR (structured C, no CPU left), checked frame by frame again
- [ ] Names: routines and RAM in `symbols.txt` as the game is understood (player, enemies, stages, score...)

## M7. The game in a window (`ports/sdl2`)

- [ ] Window, scaling, input (keyboard, gamepads), sound out, settings, the ROM set dropped on the window
- [ ] DIP switches as game options (lives, difficulty, bonus, the invulnerability DIP of the Japanese sets)
- [ ] From DonPachi Recomp's port, as wanted: save states, replays, training, CRT look, high scores kept

## Later, maybe

- [ ] The other sets: makaimurc, makaimurb, makaimur, gng (World), gngt (US)...
