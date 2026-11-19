# Accuracy: MAME first, then the board

**The final goal is the real board, 1:1.** The way there is in two steps, and both must stay available:

1. **MAME mode** — the board exactly as MAME 0.289's `capcom/gng.cpp` models it. This is what can be
   checked today, frame by frame and cycle by cycle, against MAME itself (`tools/mame_trace.py`) and against
   the reference (`gref`). Everything is built and verified in this mode first.
2. **Board mode** — MAME mode plus the board's own behaviour where MAME simplifies it, as Jose Tejada
   (jotego) found it on the real PCB for his FPGA core [jt_gng](https://github.com/jotego/jt_gng). This is the
   mode the game will use by default once each difference is implemented and checked.

The switch is a setting of the board model, not a different build: the same runtime, the same translated
program, a flag (`gng_accuracy`, planned in `runtime/include/gng/board.h`) that the reference, the headless
checks and the game all honour. Every check names the mode it ran in.

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
| 1 | **Sprite DMA** (write to 3C00) | `buffered_spriteram8`: the 512 bytes copied at once, the 6809 not delayed | a DMA copies the object RAM to the video's buffer and **halts the 6809** while it runs (jotego; noted in MAME's own header) — this changes the cycle count of every frame, and so where the slowdown happens | to research (the DMA's length and when the CPU stops) |
| 2 | **Palette writes during the active display** | applied at once, no glitch | bus conflicts: wrong colours on screen, the "lightning" effect when the VBLANK routine overruns into the next frame (MAME's header cites recordings) | to research |
| 3 | **Read of 3C00** (`LDA $3C00` at 6017, the reset's wait) | 3C00 is mapped for writes only: a read does nothing | jotego: the DMA "is triggered when a certain memory location is addressed" — perhaps by a read too | to check |
| 4 | **Power-on palette** | every 4 colours 00/55/AA/FF (a MAME TODO: "the POST has random/filled data") | random | cosmetic (the boot clears it); MAME's kept in both modes unless a recording says otherwise |
| 5 | **Sound timing** (latch, Z80 interrupts, YM2203) | MAME's scheduler, 4 IRQs a frame by a timer | to compare when the sound board is done | later |

## How the board mode will be checked

There is no MAME to compare with there. In order of preference: recordings of the real board (video at the
same inputs: the slowdown's frames, the lightning glitch), jt_gng's simulation run on the same inputs (its
frame timing), and the PCB's schematics. Each check is written down in this file as it is done.
