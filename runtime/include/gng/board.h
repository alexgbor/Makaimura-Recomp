/**
 *    Name:        board.h
 *    Description: the Ghosts'n Goblins / Makaimura board (Capcom, 1985) as the 6809 program sees it: its
 *                 memories, its bus, the bank latch, the sprite DMA, the LS259 latch, the inputs
 *    Version:     0.1.0
 *    Created:     19-11-2026
 *
 *    license:BSD-3-Clause
 *    After MAME's src/mame/capcom/gng.cpp (BSD-3-Clause, Pierpaolo Prazzoli): gng_main_map, bankswitch_w,
 *    machine_start, machine_reset, the LS259's outputs and the buffered sprite RAM. docs/hardware.md has the map.
 *
 *    The bus is gng_read and gng_write: the reference (MAME's 6809 core) and the translated program both call
 *    them, a byte at a time, in the order the 6809 makes its accesses.
 */
#ifndef GNG_BOARD_H
#define GNG_BOARD_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* How faithful the board model is (docs/accuracy.md): MAME 0.289's gng.cpp exactly, or the real board (MAME's
   plus what MAME leaves out: the sprite DMA's halt of the 6809, the palette's bus conflicts...). Every check
   names the mode it ran in; the program itself is the same in both. Board mode adds nothing yet: each
   difference is added, checked and listed in docs/accuracy.md */
typedef enum gng_accuracy_mode {
	GNG_ACCURACY_MAME = 0,
	GNG_ACCURACY_BOARD = 1,
} gng_accuracy_mode;

#define GNG_CPU_CYCLES_PER_FRAME 25152u    /* 384 x 262 pixel clocks at 6 MHz, the 6809 at a quarter of it */
#define GNG_VBLANK_CYCLE         23040u    /* line 240's start (MAME's vblank begin), in 6809 cycles from line 0 */

/* The ROM regions as MAME loads them (tools/makaimura_rom.py) */
typedef struct gng_roms {
	const uint8_t *maincpu;     /* 0x18000: 4000-7FFF mj04g, 8000-FFFF mj03g, 10000-17FFF mj05g (banks 0-3) */
	const uint8_t *audiocpu;    /* 0x10000 (0x8000 used) */
	const uint8_t *chars;       /* 0x4000 */
	const uint8_t *tiles;       /* 0x18000 */
	const uint8_t *sprites;     /* 0x20000 */
} gng_roms;

/* The inputs, as the board reads them: active low */
typedef struct gng_inputs {
	uint8_t system;    /* 3000: bit 0 start 1, 1 start 2, 5 service, 6 coin 1, 7 coin 2 */
	uint8_t p1, p2;    /* 3001, 3002: bit 0 right, 1 left, 2 down, 3 up, 4 button 1, 5 button 2 */
	uint8_t dsw1;      /* 3003 */
	uint8_t dsw2;      /* 3004 */
} gng_inputs;

typedef struct gng_board {
	uint8_t ram[0x1E00];          /* 0000-1DFF */
	uint8_t sprite_ram[0x200];    /* 1E00-1FFF */
	uint8_t sprite_buffer[0x200]; /* what the video draws: sprite_ram as the last DMA (3C00) copied it */
	uint8_t fg_ram[0x800];        /* 2000-27FF: the text layer, codes then attributes */
	uint8_t bg_ram[0x800];        /* 2800-2FFF: the background, codes then attributes */
	uint8_t palette_ext[0x100];   /* 3800-38FF: each colour's RRRRGGGG */
	uint8_t palette_base[0x100];  /* 3900-39FF: each colour's BBBBxxxx */
	uint8_t scroll_x[2];          /* 3B08-3B09 */
	uint8_t scroll_y[2];          /* 3B0A-3B0B */
	uint8_t ls259;                /* 3D00-3D07, bit n = output Qn: 0 flip (inverted), 1 sound reset (inverted),
	                                 2-3 coin counters */
	uint8_t bank;                 /* the 4000-5FFF window's bank, 0-4 */
	uint8_t sound_latch;          /* 3A00 */
	uint32_t sound_latch_writes;  /* how many times the latch was written (the sound side follows them) */
	uint32_t dma_count;           /* sprite DMAs since the reset */
	uint32_t palette_conflicts;   /* board mode: palette writes in the active display (gng/video.h) */
	gng_inputs inputs;
	gng_roms roms;
	gng_accuracy_mode accuracy;   /* (kept over gng_power_on: set it before or after) */
	/* a check's faulty board (never the game's): 0, or 1 + the address of a RAM cell in 1E00-2FFF whose bit 0 is
	   stored inverted, as a bad chip would, for the boot's RAM test's failures (bank 3's 558C, 55A9: the sprite
	   RAM is in its first loop, 0000-1FFF; the text layer's in its second). Kept over gng_power_on. */
	uint32_t faulty_cell;
	/* the host's hooks (may be NULL), called as the 6809 writes: the sound latch; an LS259 output that changes
	   (Q1 is the sound board's reset) */
	void (*on_sound_latch)(struct gng_board *board, uint8_t value);
	void (*on_ls259)(struct gng_board *board, unsigned output, unsigned value);
	/* board mode: an access to 3C00 started the sprite DMA, which halts the 6809: the host stops its CPU for
	   gng_dma_halt(the access's time) cycles */
	void (*on_dma)(struct gng_board *board);
	/* board mode: the 6809's time now (cycles from power-on), at the end of the bus cycle being made: the raster
	   (gng/video.h) draws up to it before a write that changes the picture */
	uint64_t (*clock)(void);
} gng_board;

extern gng_board gng;

/* The board at power-on: memories cleared, MAME's machine_reset (scroll 0, the palette's 00/55/AA/FF greys),
   bank 0 (MAME's membank starts at entry 0), the LS259 all low */
void gng_power_on(const gng_roms *roms);

/* The bus as the 6809 sees it */
uint8_t gng_read(uint16_t address);
void gng_write(uint16_t address, uint8_t value);

/* A byte of the program's ROM as the 6809 would read it now (4000-FFFF); 0 elsewhere */
uint8_t gng_rom_byte(uint16_t address);

/* The DIP switches' factory settings (MAME's defaults for gng: 1 coin 1 credit, demo sounds on, upright,
   3 lives, 20K 70K every 70K, normal difficulty) */
#define GNG_DSW1_DEFAULT 0xDFu
#define GNG_DSW2_DEFAULT 0xFBu

/* The sprite DMA in board mode (docs/accuracy.md #1, after jotego's jt_gng: jtgng_objdma.v, jtgng_main.v).
   The vertical blank is lines 240-261 and 0-15 (38 lines of 96 6809 cycles), the DMA copies 0x180 bytes (96
   sprites) a byte every 2 pixel clocks (half a 6809 cycle) during it, the 6809 off the bus meanwhile (its
   DMA/BREQ: a dead cycle as it lets the bus go and one as it takes it back) */
#define GNG_BLANK_FIRST_CYCLE  23040u    /* line 240 */
#define GNG_BLANK_LAST_CYCLE   1536u     /* line 16: the blank ends there in the next frame */
#define GNG_DMA_SPRITE_BYTES   0x180u
#define GNG_DMA_COPY_CYCLES    192u      /* 0x180 bytes x 2 pixel clocks / 4 */
#define GNG_DMA_DEAD_CYCLES    2u

/* The 6809 cycles a DMA started by an access at board time 'cycle' (6809 cycles from power-on, the beam at line
   0) halts the CPU: the copy, after the wait for the blank if the access is outside it */
uint32_t gng_dma_halt(uint64_t cycle);

/* A 32-bit FNV-1a of the board's memories and latches: the state the frame checks compare */
uint32_t gng_state_hash(void);

/* The bytes of the bus the decompiled program (src/, milestone M6) cannot have as the board does, having no stack:
   the 6809's stack (02C0-02FF: S starts at 0300 and goes no lower than 02CF in any script, a few bytes below S
   written by the code too) and the stack pointer the round robin's copies keep at <$E2-<$E3 */
static inline int gng_stack_byte(uint16_t address)
{
	return (address >= 0x02C0u && address < 0x0300u) || address == 0x00E2u || address == 0x00E3u;
}

/* An FNV-1a of the bus 0000-2FFF but those bytes: what the decompiled program is checked by against the reference */
uint32_t gng_bus_hash_without_stack(void);

#ifdef __cplusplus
}
#endif

#endif
