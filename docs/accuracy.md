# Accuracy: MAME first, then the board

**The final goal is the real board, 1:1.** The way there is in two steps, and both must stay available:

1. **MAME mode** — the board exactly as MAME 0.289's `capcom/gng.cpp` models it. This is what can be
   checked today, frame by frame and cycle by cycle, against MAME itself (`tools/mame_trace.py`) and against
   the reference (`gref`). Everything is built and verified in this mode first.
2. **Board mode** — MAME mode plus the board's own behaviour where MAME simplifies it, as Jose Tejada
   (jotego) found it on the real PCB for his FPGA core [jt_gng](https://github.com/jotego/jt_gng). This is the
   mode the game will use by default once each difference is implemented and checked.

The switch is a setting of the board model, not a different build: the same runtime, the same translated
program, a flag (`gng.accuracy`: `gng_accuracy_mode` in `runtime/include/gng/board.h`; `gref --accuracy
mame|board`) that the reference, the headless checks and the game all honour. Every check names the mode it ran
in. The player chooses too: the game keeps both modes playable, MAME's "1:1 with MAME" and the board's, as a
setting of the window port (once board mode differs from MAME's).

## Rules

- A difference between the modes lives in the board model (`runtime/`) behind the flag, never in the translated
  program: the program is the arcade's, whatever the board does around it.
- MAME mode stays exact against MAME forever: `tools/mame_trace.py` must keep passing in MAME mode.
- Each board-mode difference is listed below with its source (jotego's notes, a PCB recording, a schematic), how
  it is checked, and its status.
- jt_gng is GPL-3: its Verilog is read for **facts** about the board (timings, what triggers what); none of its
  code is copied here. This project's implementation is its own, credited to the research.

## Where MAME and the board differ

| # | What | MAME (gng.cpp) | The board | Status |
|---|---|---|---|---|
| 1 | **Sprite DMA** (an access to 3C00) | `buffered_spriteram8`: the 512 bytes copied at once, the 6809 not delayed, 128 sprites drawn | a DMA copies the object RAM to the video's buffer and **halts the 6809** while it runs (jotego; noted in MAME's own header) — this changes the cycle count of every frame, and so where the slowdown happens | **in board mode** (below): 194 cycles a DMA, 96 sprites |
| 2 | **Palette writes during the active display** | applied at once, no glitch | bus conflicts: wrong colours on screen, the "lightning" effect when the VBLANK routine overruns into the next frame (MAME's header cites recordings) | to research |
| 3 | **Read of 3C00** (`LDA $3C00` at 6017, the reset's wait) | 3C00 is mapped for writes only: a read does nothing | jt_gng: any access to 3C00-3CFF, read or write, starts the DMA (jtgng_main.v's decode has no R/W); the program reads it at reset in a loop built like the IRQ's start (an access, three NOPs) | **in board mode**: a read starts the DMA too |
| 4 | **Power-on palette** | every 4 colours 00/55/AA/FF (a MAME TODO: "the POST has random/filled data") | random | cosmetic (the boot clears it); MAME's kept in both modes unless a recording says otherwise |
| 4b | **Power-on LS259** (3D00-3D07) | every output 0: the screen flipped and the sound CPU held in reset until the boot writes 3D00/3D01 (MAME's first frame is drawn flipped) | the LS259's state before the first write depends on whether its /CLR is tied to the reset (schematics) | cosmetic (the first frame, during the boot); MAME's kept unless the schematics say otherwise |
| 5 | **Sound timing** (latch, Z80 interrupts, YM2203) | MAME's scheduler, 4 IRQs a frame by a timer (240 Hz, not the video's 59.64 Hz) | to compare with recordings | MAME's in the reference, checked (below) |
| 6 | **The time origin** | MAME's time 0 is a VBLANK's start: its 6809 meets each VBLANK 2110 cycles later than at the board's power-on with the beam at line 0 (the reference's 23040 + 25152 n) | the video counter's state at power-on: unknown | a constant: only the boot's first frame sees it (the 6809 waits for the VBLANK with SYNC); the checks compare MAME's times less 2110 (`tools/mame_trace.py`'s MAME_ORIGIN) |
| 7 | **The Z80's start after its reset** | one Z80 cycle after the 6809 releases the line (its scheduler's rounding while the Z80 is suspended: measured, every Z80 write one cycle late against 2 x the release) | at the release (the board's clocks are one crystal) | MAME's in the reference (`kReleaseDelay` in tools/reference/sound/gsound.cpp); none in board mode |

## #1, the sprite DMA, in board mode

What is known, and from where:

- **MAME's header** (gng.cpp, after jotego): a DMA circuit copies the object data from the CPU's RAM to a buffer
  and halts the CPU meanwhile; it is triggered when a memory location is addressed (3C00, once taken for a
  watchdog).
- **jt_gng** (jotego's FPGA core, `cores/gng/hdl/jtgng_objdma.v`, `jtgng_main.v`; GPL-3, read for facts only): any
  access to 3C00-3CFF strobes the DMA; it asks for the bus with the 6809's DMA/BREQ (not HALT, not clock
  gating) at once, waits for the vertical blank if it is not in it, then copies **0x180 bytes** (96 sprites,
  1E00-1F7F), a byte every 2 pixel clocks (6 MHz): 768 pixel clocks, **192 6809 cycles**; then gives the bus
  back. Its header says the PCB's timing is followed (the real board had no dual-port RAM).
- **The program agrees with 96 sprites**: the reset fills exactly 0x180 bytes of the sprite RAM with F8
  (6002-6012: `LDY #$0060`, four bytes a pass). Its IRQ handler starts with the access and three NOPs (65C4),
  as does its reset loop (6016-601C), as if made to let the DMA take the bus.
- **The MC6809's DMA/BREQ** (Motorola's data sheet): the CPU stops at the end of the current cycle and gives the
  bus after a dead cycle; it takes it back with another dead cycle.

The model (`runtime/src/board.c`'s `gng_dma_halt`, the hosts' `on_dma`): an access to 3C00 (read or write) halts
the 6809 for 2 + 192 = 194 cycles from the end of its bus cycle, or, outside the blank (lines 240-261 and 0-15),
until the blank's start and then 194. The copy is made at once as the access is made (the 6809, the only writer
of the sprite RAM, is off the bus until it ends, and the video reads the buffer only during the active display:
the same picture). The video draws 96 sprites. The reference (`gref --accuracy board`: MAME's core, its icount
eaten) and the translation (`cpu.cycles`) follow the same model, and are the same in board mode
(`tools/check_translation.py --accuracy board`: attract, walk_right, long_play; bus, IRQs, pictures, the sound).

What it does to the game, measured (board mode against MAME mode):

- In play, the IRQ's access always falls in the blank: of long_play's 39390 IRQs, all but the boot's first take
  it within the blank, so the DMA never waits for one there; every frame loses 194 cycles (0.8% of 25152), and
  the IRQ is taken a cycle later in its frame.
- At the boot, the reset's loop (SYNC, LDA $3C00, 100 times, the IRQ masked) is slower: the IRQ line stays
  asserted (masked, it is never acknowledged), so SYNC no longer waits, and each read halts the CPU, those
  outside the blank until the next one: about 17 a frame, the boot 5 frames longer (the first IRQ taken at frame
  314, not 309).
- The scripts have no busy scene, so no slowdown frame in either mode yet: a script with one is needed to see
  the DMA move the slowdown.

Not known, and so not modelled: the DMA's exact start and end latency (a pixel clock or two of resynchronising,
the 6809's dead cycles: within a cycle or two); the 6809's self-refresh (its data sheet: it takes the bus back
for a cycle after 15 cycles of DMA/BREQ; whether the board's DMA holds the request through that, and whether
jt_gng's 6809 models it, is not known: if it does, the halt is about 13 x 3 cycles longer); whether the real
board's IRQ flip-flop is cleared other than by the acknowledge (then the reset loop's SYNC would wait a frame
each time). To settle with a recording of the board's boot (its length to the first picture) or of a slowdown.

## How MAME's own picture is sampled

MAME's screen bitmap holds palette indexes; `screen:pixels()` turns them into colours when it is called. At a
frame notifier the bitmap is the previous VBLANK's, and the colours are this VBLANK's. `gref --pictures` takes
its pictures the same way for the check against MAME. What MAME displays, and what `gref --ppm` and the game
draw, is a VBLANK's indexes in that VBLANK's colours. On the real board the picture is made line by line
during the active display, with the palette and the layers as they are on each line. That is a board-mode
difference to model: every write during the active display then shows from its line on (#2 above is one case).

## The sound board against MAME

`tools/mame_trace.py --sound` runs MAME with taps on the latch (3A00), the YM2203s (the Z80's E000-E003) and the
Z80's RAM, and the reference with its sound board (`gref --sound`: MAME 0.289's Z80 and ymfm, as MAME has them),
and compares the latch's writes (6809 time and value), every YM2203 write (Z80 time, chip, port, value) and the
Z80's RAM and PC at the first YM2203 write after each frame. Over walk_right (7001 frames): 214 latch writes,
1,201,454 YM2203 writes and 6673 RAM states, every one the same, the times on MAME's origin (#6 above). The
samples are then ymfm's from the same writes at the same cycles; MAME's own WAV goes through its mixer and
resampler (to 48 kHz), which the reference does not copy.

## How the board mode will be checked

There is no MAME to compare with there. In order of preference: recordings of the real board (video at the
same inputs: the slowdown's frames, the lightning glitch), jt_gng's simulation run on the same inputs (its
frame timing), and the PCB's schematics. Each check is written down in this file as it is done.
