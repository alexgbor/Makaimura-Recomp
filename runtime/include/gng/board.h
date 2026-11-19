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
	gng_inputs inputs;
	gng_roms roms;
	/* the host's hooks (may be NULL): a latch write, with the board's time in 6809 cycles */
	void (*on_sound_latch)(struct gng_board *board, uint8_t value);
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

/* A 32-bit FNV-1a of the board's memories and latches: the state the frame checks compare */
uint32_t gng_state_hash(void);

#ifdef __cplusplus
}
#endif

#endif
