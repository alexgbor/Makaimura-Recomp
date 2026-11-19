/**
 *    Name:        sound.h
 *    Description: the board's sound side: the Z80 (the translated sound program), its RAM, the sound latch, its
 *                 240 Hz IRQ, its reset from the LS259's Q1, and the two YM2203s' writes
 *    Version:     0.1.0
 *    Created:     19-11-2026
 *
 *    license:BSD-3-Clause
 *    After MAME 0.289's capcom/gng.cpp (sound_map, the machine config) and its scheduler, as the reference's sound
 *    board (tools/reference/sound/gsound.cpp) has them and checks against MAME: time in Z80 cycles from power-on
 *    (2 a 6809 cycle); the Z80 runs lazily up to each 6809 write that changes its world (the latch, Q1) and to each
 *    frame's end; a latch read at time t sees the writes made at t or before; the IRQ every 12500 cycles from the
 *    reset's release (and none while held); in MAME mode the Z80 starts a cycle after its release (MAME's
 *    scheduler: docs/accuracy.md #7).
 */
#ifndef GNG_SOUND_H
#define GNG_SOUND_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct gng_sound
{
	uint8_t ram[0x800];          /* C000-C7FF */
	int in_reset;                /* the Z80 held (LS259 Q1 0), the YM2203s too */
	uint64_t ym_writes;          /* the YM2203 writes so far */
	uint64_t samples;            /* the chips' samples made so far (sample n from Z80 time 24 n) */
	/* the host's hooks (may be NULL): each YM2203 write, at Z80 time 'time' (chip 0 or 1, offset 0 the address
	   port, 1 the data port); each sample of the mix (MAME's routes, 125 kHz) */
	void (*on_ym_write)(uint64_t time, int chip, int offset, uint8_t value);
	void (*on_sample)(int16_t sample);
} gng_sound;

#define GNG_SOUND_RATE 125000u    /* the YM2203s' samples a second (1.5 MHz / 12) */

extern gng_sound gng_snd;

/* Power-on, with the sound program (the "audiocpu" region, 32 KB): the Z80 held in reset */
void gng_sound_power_on(const uint8_t *program, int mame_mode);

/* The 6809's writes, at Z80 time 'time' (twice the 6809's): the latch (3A00), the LS259's Q1 (3D01) */
void gng_sound_latch(uint64_t time, uint8_t value);
void gng_sound_q1(uint64_t time, int value);

/* The board's time moves on (a frame's end): the Z80 up to Z80 time 'time'; -1 if the sound program faulted */
int gng_sound_run_until(uint64_t time);

uint32_t gng_sound_ram_hash(void);
uint64_t gng_sound_time(void);

#ifdef __cplusplus
}
#endif

#endif
