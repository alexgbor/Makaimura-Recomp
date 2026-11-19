# The board, and what the program does with it

From MAME's `src/mame/capcom/gng.cpp` (BSD-3-Clause, Pierpaolo Prazzoli; the DMA note from Jose Tejada's
jt_gng research) unless an entry says it was found in the program.

## Clocks and screen

| | |
|---|---|
| Crystal | 12 MHz |
| Main CPU | MC6809, fed 6 MHz (12/2), runs at 1.5 MHz (E = /4). IRQ at each VBLANK, held until acknowledged (`irq0_line_hold`) |
| Sound CPU | Z80 at 3 MHz (12/4), IRQ 4 times a frame (`set_periodic_int`, 240 Hz), held |
| Sound chips | 2× YM2203 at 1.5 MHz (12/8); each FM output at 0.40, SSG at 0.20, mono |
| Screen | `set_raw(6 MHz, 384, 0, 256, 262, 16, 240)`: 256×224 visible, 59.637 Hz |

## Main CPU map (`gng_main_map`)

| Address | |
|---|---|
| 0000-1DFF | work RAM |
| 1E00-1FFF | sprite RAM (`buffered_spriteram8`: a write to 3C00 copies it into the video's buffer) |
| 2000-27FF | text layer: 2000-23FF codes, 2400-27FF attributes (bits 7-6 code high, 5-4 flip YX, 3-0 colour) |
| 2800-2FFF | background: 2800-2BFF codes, 2C00-2FFF attributes (7-6 code high, 5-4 flip YX, 3 priority group, 2-0 colour) |
| 3000 | SYSTEM: start 1, start 2, -, -, -, service, coin 1, coin 2 (active low) |
| 3001, 3002 | P1, P2: right, left, down, up, button 1, button 2 (active low) |
| 3003, 3004 | DSW1, DSW2 |
| 3800-38FF | palette, the second byte of each colour (`write8_ext`) |
| 3900-39FF | palette, the first byte (`write8`); format RGBx 444 |
| 3A00 | sound latch |
| 3B08-3B09 | background scroll X (low, high) |
| 3B0A-3B0B | background scroll Y (low, high) |
| 3C00 | sprite DMA (was thought to be a watchdog; halts the CPU while it copies) |
| 3D00-3D07 | LS259: bit 0 flip screen (inverted), 1 sound CPU and YM reset (inverted), 2-3 coin counters |
| 3E00 | bank: 4 selects bank 4, any other value its low 2 bits |
| 4000-5FFF | banked ROM: banks 0-3 are mj05g's four 8 KB, bank 4 is mj04g's first 8 KB |
| 6000-7FFF | mj04g's second 8 KB (fixed) |
| 8000-FFFF | mj03g (fixed); vectors at FFF0 |

## Sound CPU map (`sound_map`)

| Address | |
|---|---|
| 0000-7FFF | gg2.bin |
| C000-C7FF | RAM |
| C800 | sound latch (read) |
| E000-E001 | YM2203 #1 (address, data) |
| E002-E003 | YM2203 #2 |

## Video (`screen_update`)

Drawn back to front: the background's back half (`TILEMAP_DRAW_LAYER1`), the sprites, the background's front
half (`LAYER0`), the text layer.

- Text: 32×32 tiles of 8×8, scanned by rows; 2 bpp (`charlayout`), colours 0x80-0xBF (16 palettes of 4); pen 3
  transparent.
- Background: 32×32 tiles of 16×16, scanned by **columns**; 3 bpp (`tilelayout`, planes in thirds of the
  region), colours 0x00-0x3F (8 palettes of 8). Split: group 0 is entirely behind the sprites (`transmask 0xFF,
  0x00`); group 1 is in front except pens 0 and 6 (`transmask 0x41, 0xBE`). Scroll X and Y, 16 bits each.
- Sprites: 128 of 4 bytes from the buffer (MAME draws `bytes() - 4` down to 0: the last sprite first, so the
  first is on top): code (+ attr bits 7-6 << 2... `(attr << 2) & 0x300`), attr (bit 0 X high, 2 flip X, 3 flip Y,
  5-4 colour), Y, X. 4 bpp, colours 0x40-0x7F (4 palettes of 16), pen 15 transparent.
- Flip screen from the LS259.
- MAME's power-on palette: every 4 colours black, 55, AA, FF grey (a TODO in MAME: the board's is random).
- Not emulated by MAME: palette writes during the active display cause bus conflicts (the "lightning" glitch
  when the VBLANK routine overruns into the next frame).

## ROM set (makaimurg)

Five files of its own (mj03g, mj04g, mj05g: the program; gng13.n4, gng16.l4: sprites), the rest from gng.
See `tools/makaimura_rom.py`.

## Found in the program

- **Reset (6000):** interrupts masked; sprite RAM filled with F8 (all sprites off screen); 100 frames waited
  with `SYNC` and a DMA each (`LDA $3C00`: a read starts it too? MAME maps 3C00 write-only: to check);
  flip off; scroll 0; DP = 0 (the direct page is the work RAM's first 256 bytes); both layers cleared; bank 3;
  `JMP $553A` with the return address in Y.
- **Bank 3, 553A:** stack at 2000; RAM test 0000-1FFF and 2000-27FF with the patterns 00, 11, ... FF (an
  error is shown with its address and stops); then the ROM check: bank 3's 560C-5659 (0x4E bytes) is copied
  to RAM at 1A00 and run there (`JMP $1A00`), since a routine in the banked window cannot switch banks under
  itself.
- **IRQ (65C4):** the sprite DMA first (`STB $3C00`), then three NOPs; the frame counter at <$20 (16 bits)
  incremented; one round-robin job (65FB: by <$21 & 3, its bank from the table at 660D: 3, 4, 4, 4; jumps
  through the table at 6611); the frame's routines (6755: flip/LS259 from <$D8 ^ <$DA, scroll from <$D4...;
  6772, 622C: DSW1 read into <$40...; 7788, 67B3, 6239, 6274); bank 3; the game state <$02's routine through
  the table at 65BC (4 states); the bank put back from <$D9; RTI.
- **The main loop (6134):** the frame counters <$D0-<$D3, the DSWs read (622C, 6239, 6274), the sound latch
  cleared, scroll 0, LS259 bit 1 (the sound CPU out of reset), IRQs on (`ANDCC #$EF`); then (617F) the task
  queue at <$12 (16-bit pointer into a ring of words at 0100-013F): a word with its high bit clear is a task
  number (A, doubled) run through the table at 6188 in bank 3 (`LDX A,X / JSR ,X`), and the word is set to
  FFFF afterwards (6160).
- **The objects (7E6D, from the IRQ's state routines):** bank 4; 9ACE walks 4 objects of 0x40 bytes from 0540
  (`JSR [A,U]` with U = 9C97: 4 states); 9F0C runs each object's handler by its type (`$5,X`) through bank 4's
  table at 4712 (19 types, one unused), and one of two routines by the frame's parity (4:470E).
- The RAM test's code in RAM (1A00-1A4D) is the only code the program runs outside its ROM (1200 frames of the
  attract mode traced).
