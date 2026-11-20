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
      switchable mode (`gng.accuracy`, `gref --accuracy`), board mode the final default; both stay playable,
      the player choosing one in the game (`[game] accuracy` and the menu, once board mode differs)
- [x] Board mode: the sprite DMA's halt of the 6809 (docs/accuracy.md #1, after jt_gng's jtgng_objdma.v): an
      access to 3C00 (read or write) halts it 194 cycles in the blank (else until the blank), 96 sprites drawn;
      gref and the translation the same in board mode (`tools/check_translation.py --accuracy board`), MAME mode
      unchanged (the translation against gref, gref against MAME); playable: `accuracy = board` in
      makaimura.ini and the menu
- [ ] Board mode #1, to settle: the DMA's latency to a cycle, the 6809's self-refresh during it, the IRQ
      flip-flop at the reset's SYNC loop (a recording of the board's boot and of a slowdown)
- [x] Input scripts that reach busy scenes (`tests/controls/make_stages.py`: stage2, stage4; the stage <$72 and the
      lives <$60 written: a test's cheat, "FRAME mADDR VALUE" in gref, gng_headless and makaimura --inputs): in
      board mode the game slows down where MAME mode does not (stage 2: 4 frames, stage 4: 3, in 12000)
- [x] Board mode: the palette's bus conflicts during the active display (the "lightning" glitch), after jt_gng's
      jtgng_colmix.v; the picture drawn as the beam goes (docs/accuracy.md #2, #2b); gref and the translation the
      same in board mode, MAME mode unchanged (against gref and against MAME)
- [x] Board mode #2 seen in busy scenes (`gng_headless --palette-conflicts`): the palette routine runs into the active
      display in stages 1, 2, 4, 5; in stage 4 the cave's background flashes yellow for 1-3 frames (board mode only),
      the "yellow flashes" of jt_gng's option
- [ ] Board mode #2, to compare: those flashes against the recordings MAME's header cites
- [x] Board mode: a read of 3C00 starts the DMA (jt_gng's decode has no R/W)

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
- [x] AA36 `JSR [B,U]`: U = AA41 (its 4 entries, AB83 among them: found as the stage scripts reached stage 4, the
      translation's safety net stopping at it); 6F76: the stage's table 6F90 (stage 6) added
- [x] The analysis' 4 jumps through registers (6F76, 9AF4, AA36 twice) have their tables in symbols.txt; every
      instruction the reference ran in every script is found statically (below)
- [~] Input scripts for the reference (`tests/controls/`): walk_right (a coin, start, walking, jumping, throwing),
      long_play (`make_long_play.py`: 40000 frames, four credits, a two-player game, pseudo-random moves, the
      service switch), stage1-6 and loop2_stage0, loop2_stage6 (`make_stages.py`: the stage, the loop <$7F, the
      weapon <$73 and the lives written: a test's cheat); every instruction they and the attract mode run is found
      statically (10830 addresses, `gref --executed`, `disassemble.py --executed`); to do: the ending (Astaroth
      beaten in the second loop), every weapon and enemy
- [x] The object dispatch at 9AF4: U from the table of tables at 9AFE by the object's type (0-5), JSR [A,U] by its
      state (0-3): its six tables in symbols.txt (long_play met the types walk_right had not)
- [x] The two-level table at 6F6B: by <$72, the stage (6F78: 7 tables' addresses, stages 0-5 6F8A, stage 6 6F90),
      then <$0B; each table runs to the next or to code
- [x] A safety net for the translation: an address the static analysis missed is reported (m6809_fault: the
      message box in the window, stderr in gng_headless), never run wrongly (it found AB83)
- [x] The RAM overlay: bank 3's 560C-5659 copied to 1A00 and run there (the boot's ROM check): symbols.txt's
      `overlay`, analysed and translated from the ROM bytes the boot copies, checked in RAM before it runs; its 31
      instructions run in every script
- [x] Every jump through a register resolved, checked against the reference's executed addresses (all scripts)
- [x] Coverage of the program ROM (`tools/coverage.py`: gref on the attract mode and every script): every address
      run is an instruction the analyses found (6809 and Z80); 71.3% of the 16830 instructions found are run by
      the scripts (the Z80's: 91.8%); the fixed ROM is 57% code run, 22% code never run, 19% data, banks 0-2 and 4
      data only (graphics' tables, the stages' maps), bank 3 code (the boot, the test mode, the ranking) and data
- [~] The code never run (114 routines, 6.9 KB): enemies the scripts never meet (object_handlers 6, 8 and 10:
      C39D, CE20, BD4F and what they call), the stage clears of stages 0-4 (they wait for the gate, <$90),
      the test mode's other screens; their translation is checked instruction by instruction against MAME's core
      (`compare_lift_with_mame.py`: the 3983 forms of every instruction found, 50 states each), not in context

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
- [x] The IRQ taken as a jump to its handler (M6809_CHECK leaves the region; RTI goes where the stack says): the
      program switches tasks in its IRQ (the round robin's 6619: STS, another stack), so a handler need not come
      back where it was taken; the nested handler the runtime had faulted ("nested too deep") in stage 2 in board
      mode, where gref (MAME's core) played on
- [x] Every stage, both loops, frame by frame against the reference, in both modes (stage1-6, loop2_stage0,
      loop2_stage6: bus, IRQs, pictures, sound the same as gref's)
- [x] The ending against the reference in both modes (`tests/controls/ending.txt`: loop2_stage6's, its stage
      cleared at frame 2100 as E799 clears one, <$08 = 2: 7D3F, 7E18 sets <$7F = 2, 71D7 plays it, 724F back to the
      first loop): bus, IRQs, sound, pictures the same as gref's ("CONGRATULATION", "RETURN TO STARTING POINT")
- [x] The features no script had reached (`tests/controls/make_features.py`): the test mode (service_mode), every
      weapon (weapons), a cocktail cabinet flipped with 7 lives, two players and a continue (cocktail), free play
      and the other DIPs (dips), each stage cleared in turn (clears); against the reference in both modes

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
- [x] The ROM set read from its zips or folders by the game itself (`runtime/src/rom_set.c`, `runtime/src/inflate.c`:
      zip and DEFLATE from their specifications, a file known by its CRC-32 and size): its regions the same as
      `tools/makaimura_rom.py`'s byte for byte (`gng_headless --rom PLACE --write-regions FILE`), from the two zips,
      a folder, stored, fixed-code, dynamic-code, Huffman-only and RLE zips, a merged set; a damaged zip refused
- [x] Settings (`ports/sdl2/settings.c`: makaimura.ini beside the program, written with what each does): the
      window, the picture, the sound, the ROM's place, every key and button
- [x] The picture as the arcade's 4:3 monitor shows it (or square pixels): fit, integer or stretch, sharp pixels
      (enlarged by whole numbers, then smoothed by the rest) or smooth; full screen (F11, Alt+Enter); vsync; the pace
      the board's 59.637 Hz by the clock, or a frame a refresh of the display (`sync = display`, 0.6% fast at 60
      Hz: not tried on a display yet); nothing drawn while the window is hidden or minimized (a hidden window's
      vsync is throttled by Windows: the board slowed with it). At 1:1 the window's picture is the board's, pixel
      for pixel (`makaimura --shot`, `gng_headless --ppm`)
- [x] The sound resampled with a windowed sinc (`ports/sdl2/resampler.c`: Kaiser, 128 taps, cut at 20 kHz):
      69.7 dB from scipy's resample_poly below 18 kHz on walk_right's sound, at 48 and 44.1 kHz
      (`tools/check_resampler.py`, `build/bin/resample_check`); the queue primed to its latency and kept there by
      nudges of the rate (at most 0.5%): on the device (WASAPI), 21-59 ms queued for a 50 ms latency over 2400
      frames, never empty
- [x] DIP switches as game options, by MAME's names (mame makaimurg -listxml): lives, difficulty, bonus life,
      coinage, coinage affects, demo sounds, flip screen, cabinet, service mode (makaimurg has no invulnerability
      DIP: DSW2's eighth switch is unused); lives = 7 checked on the picture
- [x] A menu in the window (`ports/sdl2/menu.c`; Esc, F1, the pad's right stick): the game's options, the
      accuracy mode (MAME 1:1 only until board mode differs), the picture's shape, scaling, pixels, full screen, the
      volume, reset, quit; written with the game's own font (its text layer's characters are ASCII), kept in
      makaimura.ini as it closes
- [x] The menu's accuracy choice: "MAME 1:1" or "board" (both playable; from the board's start, F3)
- [x] Recordings (`ports/sdl2/recorder.c`; F12, `makaimura --record FILE`): the board's frames and its sound into
      an uncompressed AVI (OpenDML: no 1 GB limit; the 4:3 in its vprp), in the board's time: its pictures the same
      as `gng_headless --ppm`'s at the same frames, its sound the same sample for sample as gng_headless --wav's
      through the resampler at 48 kHz (`resample_check`); `--record-inputs FILE` writes the controls as an input
      script (the DIPs too) that `--inputs` plays again: the same AVI byte for byte; `--accuracy` for the mode;
      `tools/side_by_side.py` puts two recordings side by side (ffmpeg)
- [x] The keys and buttons changed from the menu (CONTROLS: Enter, then the key, the pad's button or trigger; a key
      taken from whatever had it; Delete: its defaults; Esc: as it was), kept in makaimura.ini; checked with
      `makaimura --menu N --menu-keys LIST --shot N FILE` (the keys pressed on the menu, the settings saved)
- [x] The second player's own controls (3002), as the board's: the program reads them for a cocktail cabinet's
      second player (691C) and for either player's buttons at the continue (76D8); MAME's keys (R F D G, A, S) and
      the second gamepad; in the menu's CONTROLS page (which scrolls)
- [x] Side panels beside the picture on a wide window (`ports/sdl2/hud.c`): the player from the game's RAM (score,
      next life, lives, weapon, area, loop, rank), both players' controls lit, the board (mode, FPS, the frames lost
      to slowdown), the state slot, the window's keys, the menu's help; `panels_debug` adds the checks' sections
      (board mode's palette flashes)
- [x] Save states (`runtime/src/state.c`, `gng/state.h`): the whole board at a 6809 instruction boundary (the CPU,
      the memories and latches, board mode's raster, the Z80, the latch's history, the YM2203s), loaded from any
      VBLANK; F5 / F7, F6 the slot (8), SAVE STATE and LOAD STATE on the menu, files in `states/` beside the settings
      (one per accuracy mode: a state from the other mode, another ROM set or another build is refused);
      `tools/check_states.py`: from a state, every bus hash, IRQ, sound board hash, YM2203 write, sample and picture
      the same as the uninterrupted run's (both modes; walk_right, long_play, stage4, loop2_stage6; states at frames
      333, 1777, 3501); the window's `--save-state` / `--load-state` (and LOAD STATE from the menu at a VBLANK) give
      the same pictures
- [x] Practice (the menu's PRACTICE page): a game from the start of any area of the first loop (1-6); reached the
      first time from the power on, unpaced and silent, as tests/controls/make_stages.py does (a coin, start, the
      area written into <$72 right after it), saved where <$71 first says playing; kept in `states/` (by mode and
      DIP switches) and loaded from then on (the same game as reached: checked on the pictures)
- [x] Replays (`ports/sdl2/replays.c`, the menu's REPLAYS): a game at a time, from the coin that began it (the
      board's state at the instruction boundary after it; a start's in free play; a slot's or a practice's state if
      it was played from one: "+") to its game over (<$28 cleared), as that state and an input script from it in
      `replays/`; at the game over the menu asks KEEP REPLAY or LET IT GO (a reset, a state loaded or quitting
      before it lets it go); played from the menu in its mode, the player's game from its end (`# end FRAME`);
      Delete, then Enter, deletes one from the list; `keep_replays` in makaimura.ini; a game and its replay give the
      same picture (`makaimura --keep-replays`: kept without asking, with --inputs too)
- [x] High scores kept between sessions (`ports/sdl2/hiscore.c`, makaimura.hi): the ranking 1518-1571 and the top
      score <$D0 (symbols.txt) written back at the first VBLANK with the program's own in place (as MAME's hiscore
      plugin), kept a second after the program changes it (a name entered: checked with long_play's game over, its
      score raised), written back after a state is loaded; logged as RAM writes in a replay; not with --inputs
      (but with `--high-scores`); `keep_high_scores` in makaimura.ini
- [x] The ROM set's window (`ports/sdl2/rom_drop.c`, after DonPachi Recomp's): when the set is not found whole, drop
      makaimurg.zip and gng.zip onto it (together or one after the other: the files known by their CRC-32), what
      is missing said by zip; the dropped zips copied into the program's `rom` folder, a dropped folder named in
      makaimura.ini; checked hidden (`makaimura --drop LIST --drop-shot FILE`)
- [x] The player's package (`tools/make_release.ps1`, after DonPachi Recomp's): release\Makaimura-x.y.z-win64.zip
      with makaimura.exe (the C runtime built in), SDL2.dll, README.txt (`ports/sdl2/release`), CHANGES.txt and the
      licenses; an allow list, no ROM, settings or saves, no ROM file's start in the .exe; the version CMakeLists.txt's
      (the menu shows it), its section in CHANGELOG.md; the unpacked package checked in an empty folder (the ROM
      window, the game)
- [ ] A Linux build and package (the sources are portable C and SDL2; not built yet: no Linux toolchain here)
- [ ] CRT look (not for now)

## Later, maybe

- [ ] The other sets: makaimurc, makaimurb, makaimur, gng (World), gngt (US)...
