# The readable C (milestone M6)

The translation (milestone M4, `tools/m6809/translate.py`) is the 6809 program an instruction at a time: the CPU's
registers in a struct, a region of code per C function, a trampoline that dispatches addresses. It is exact, and it
stays as the reference the readable C is compared with (`gng_headless`, `makaimura_translated`). The game
(`makaimura`) is the readable C. The readable C is the same program through the shared core's decompiler (`tools/core/decompiler`,
arcade-decomp): functions as the program has them, structured C with no goto, local variables instead of
registers, and no CPU left in it, checked frame by frame against the reference like the translation is.

Nothing of it is committed: like the translation, it is the game's code, written from the player's own ROM
(`src/`, git-ignored).

## Making it

```
python tools/m6809/decompile.py [--check] [--plain] [--out DIR]
cmake --build build/msvc --config Release --target gng_headless_c
python tools/check_translation.py --decompiled [--accuracy mame|board]
```

- `tools/m6809/function.py`: the program split into functions (the vectors, the calls' targets, the tables',
  symbols.txt's func and code lines) and each one lifted into the decompiler's blocks: calls and returns as C has
  them, the jumps through tables as switches, the window's code by its bank (every address in 4000-5FFF holds code in
  one bank only: no call needs to choose by the latch), PSHS/PULS as the stack's slots.
- `tools/m6809/decompile.py`: the 6809's profile for the decompiler (A, B, D, X, Y, U; S the stack; CC's flags but
  I), and the core's passes in the order the sound program's driver runs them (`tools/core/decompile_sound.py`):
  the stack, the data flow and the functions' signatures, the dead code, the expressions, the types of pointers,
  the structure, the writer. D (A:B) is a register of its own, its bytes A and B variables of their own only where a
  function writes them apart (the Z80's pairs: `tools/core/z80/pairs.py`); a subtraction's or an addition's flags
  are the core's carry_sub, overflow_sub... of its operands and the branches its conditions, so that a branch on
  them reads as the comparison (`if (d >= 0x8Fu)`: `function.DecompileLifter`, the same values as the lift's).
  `--plain` writes A and B apart and the lift's flags as they are.
- `tools/m6809/emit.py`: the 6809's board for the writer: the work RAM by address (`RAM8`), the program's data
  through the board (`ROM8`: a pointer may lead into the fixed ROM, the window in its bank, or the work RAM), the
  devices through the bus by name, and the timing's calls.
- `runtime/src/m6809_program.c`, `runtime/include/m6809/program.h`: the board the C runs on.

## What the program does that C does not, and how the C does it

- **The board's time, to the cycle.** The checks compare the IRQ's cycle, and the picture and the sound depend on
  when the program writes the video, the palette and the sound latch. So the C keeps the translation's time:
  `m6809_block(n)` after each instruction, where the runtime lets the board's time pass and takes the IRQ (a call
  of its handler, `program_irq`); `m6809_at(c)` before an access that may reach 1E00-3FFF (the sprite RAM, the
  video RAMs, the palette, the devices), the end of its bus cycle; `m6809_cycles(n)` on a long branch's longer
  way. The work RAM needs no time: nothing but the 6809 reads it. Where the IRQ cannot come in (the functions that
  only run with it masked: the IRQ's handler and all it calls, most of the game; `function.masked_functions`) a
  block's instructions are one `m6809_block` at its end, or before a call (`function.merge_time`).
- **The IRQ.** It always returns where it was taken (checked over every script in both modes: the "nesting" the
  translation once saw was the next IRQ due at the RTI, in slowdown), so it is a call. The interrupt mask is the
  runtime's (`m6809_mask`, `m6809_masked`).
- **The stack.** Return addresses and saved registers are the C's own variables, and their bytes are in the work
  RAM too, where the 6809 has them: each byte the code puts on the stack (a call's return address, PSHS, a store
  through S) is `m6809_stack_byte(offset, value)` at S + offset, each instruction that moves S
  `m6809_stack_moved(step)` (LDS `m6809_stack_set`), S the runtime's `cpu.s`; a call's return address is one line,
  `m6809_push_return(back)` before the call, and an RTS's `m6809_pop_return()`. The program reads them as data once:
  in stage 3 from its midway point (tests/controls/midway3.txt) 633F draws a block of the map whose base is 0000,
  its tiles from 02F8-02FF (return addresses and pushed registers). What the C still cannot write is the state the
  IRQ's entry pushes (12 bytes below S: the registers are the C's variables) and the stack pointer the round robin's
  copies keep at <$E2-<$E3; the checks leave 02C0-02FF (S starts at 0300 and goes no lower than 02CF in any script)
  and <$E2-<$E3 out (`gng_stack_byte`), though above the IRQ's frame the stack is the same as gref's. The boot's RAM test runs on a
  stack at 2000 (the sprite RAM's last bytes): the boot clears them before the first DMA.
- **S as a pointer.** The round robin's copies (6619-6754) keep S at <$E2, point it at the data and pull it with
  PULS: that S is a variable of its own (`source`), the STS and the LDS left out.
- **Returning through Y.** The boot's RAM and ROM test (3:553A) is entered by a jump with its return address in Y,
  which it pushes and returns through (symbols.txt: `return=Y`): a call.
- **A jump through a table by an accumulator** (ASLB / LDU #table / JMP [B,U]) is a switch on the offset, its value
  zero-extended (`function.Builder.lift`): the core's writer prints a switch's value right in its low bits only, and
  where the function takes the whole of D (satan's BDE2: its callee reads A) the byte was printed as `d * 2u`, no
  case matched with A set and Satan's state 1 ran nothing (found by tests/controls/satan4.txt). To tell upstream
  (arcade-decomp's emit.py: the jump_target of a Switch from `expr`, not `exact`).
- **DP** is 0 all along (the two TFR B,DP of the boot set it to 0): the direct page is the work RAM's first 256
  bytes, read as variables.
- **A function that loops back to its start** (the main loop; a routine that counts down from its entry) gets an
  empty block before its first: what the passes put at a function's start (the bytes of D it takes) runs once.
- **Code that overlaps itself** (task 17 of the main loop's table, never queued in any script, starts in the middle
  of an instruction): its blocks start where its instructions do.
- **Scratch below S.** A few routines write below S and read it back (3:5A2C, 8B26, 63CC): on the board an IRQ in
  between would overwrite it with the state it pushes, which the C cannot reproduce. No IRQ lands in those stretches
  in any script (gref's IRQ PCs).

## Save states

The C has no stack a state could hold: its calls are the host's. So a state is taken where the program's own is
nothing but the main loop waiting for work: the top of its poll (617F, symbols.txt's `idle`), which it passes every
few dozen cycles while it waits, the C there `m6809_idle(u)` with what the main loop keeps across its tasks (U). A
state asked for is taken the next time it passes there; it goes on by calling the main loop there
(`program_resume(u)`, src/vectors.c). `tools/check_states.py --program build/bin/gng_headless_c.exe`: the run from
a state the same as the uninterrupted one.

## How it is checked

`tools/check_translation.py --decompiled`: every script of `tests/controls/` and the attract mode, gref against
`gng_headless_c`: at each IRQ the bus 0000-2FFF but the stack's bytes (`--irq-state`) and the cycle it is taken at;
each frame's picture; every YM2203 write and the sound, byte for byte. `tools/m6809/compare_dumps.py` compares
the bus at each IRQ byte by byte (`--irq-dumps`) to find where a difference starts.

That sees only what the scripts play. **Each function alone** (`tools/m6809/check_functions.py`, on
`build/bin/fcheck`, `tools/reference/fcheck.cpp`): the original program runs on MAME's core over a script, and the
state at each function's entry is kept (the registers, the board's memories and latches; up to `--samples` a
function, spread over the run). Then each function of the C is run from those states, and from changed ones, on
MAME's core and in the C (`tools/reference/fcheck_runtime.c`: the board's time from 0, no IRQ), until it returns,
and compared: how it ends, its cycles, the registers it hands back (`src/functions.txt`, the signatures
`decompile.py` writes), the board's memories and latches but the stack below S and <$E2-<$E3, the sound latch's
writes and their times. The states, by kind:

- **real**: as the game called it. These must all be the same.
- **plausible**: registers from the function's other calls, and bytes of the work RAM it read changed to values that
  byte held somewhere in the run (after the boot's RAM test).
- **wild**: registers and bytes made up, but what every caller agrees a register holds as the function starts
  (`functions.txt`: `x=global:15A2`, an address the C reads what it points at as variables; `x=object:10`, a
  pointer into an object's record), which the C takes as given.
- **unowned**: the same, for a function the script never calls, from the other functions' states.

A run where the original goes where the C cannot follow is counted apart, not compared: to an instruction the static
analysis never found (`build/fcheck/code.txt`), or through a table past its end (the analysis's tables and
symbols.txt's `jump=`: an index the game never gives, which the original reads code as an address for and the C's
switch has no case for); and where the C reaches past the work RAM (a pointer no state of the game holds:
`M6809_RAM_GUARD`, `m6809/program.h`). `--function NAME --trace` (on fcheck) prints the original's instructions in
the first run that differs.

## To do

See TODO.md, M6: the names (symbols.txt: the routines, the RAM's variables and the objects' structures).

## Reading it

- The work RAM's 16-bit values (high byte first, the 6809's order) are `read16(&x)` and `write16(&x, v)` where the
  C reads or writes both bytes of one (`decompile.join_words`: the same bytes in the same order).
- A structure symbols.txt describes (`struct`, `field` lines, the core's format) is read through its macro,
  `OBJECT(p)->state`, where a `local` or `param` line says a register points at one (`object+10`: the objects'
  handlers keep X 0x10 bytes into each record, so their C reads `OBJECT(object - 0x10u)->type`); the pointer
  analysis carries it on to the functions the pointer is handed to. Its fields are the work RAM's (no bus, no
  time), as the objects are.
- A dispatch through a table whose address the code loads right before it (`LDX #table; ... JMP [B,X]`) is a
  switch on the offset into the table (`case 0x2u:` the second entry); the others switch on the address read.
- `m6809_block(n)`, `m6809_at(c)`: the board's time (above); `mem_read8/mem_write8`: the board's bus, where the
  address is worked out or a device's; `ROM8(a)`: the program's data through the board; `RAM8`, the variables of
  `ram.h`: the work RAM.
