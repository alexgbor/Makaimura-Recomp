/**
 *    Name:        gsound.h
 *    Description: the reference sound board: MAME 0.289's Z80 and ymfm's two YM2203s (as MAME 0.289 has them),
 *                 on the board's sound side, the time kept as MAME's scheduler keeps it
 *    Version:     0.1.0
 *    Created:     19-11-2026
 *
 *    license:BSD-3-Clause
 *    A development tool, never part of the game: the sound the game's own sound board has to make.
 *    After MAME's capcom/gng.cpp (sound_map, the machine config): the Z80 at 3 MHz, ROM 0000-7FFF, RAM C000-C7FF,
 *    the latch read at C800, the YM2203s written at E000-E001 and E002-E003 (never read), the IRQ held 240 times
 *    a second (irq0_line_hold, set_periodic_int: every 12500 Z80 cycles, the timer restarted as the Z80's reset
 *    line is released, none while it is held), the LS259's Q1 inverted on the Z80's RESET line, and both YM2203s
 *    reset while Q1 is 0.
 *
 *    Time is in Z80 cycles from power-on: 2 a 6809 cycle (both from the board's 12 MHz). The Z80 runs lazily,
 *    up to the time of whatever changes its world: the 6809's latch writes and LS259 writes (MAME synchronizes
 *    both at the 6809's time: the Z80 runs up to it, then the change is made), its own IRQ timer, the end of a
 *    frame. Each YM2203 makes a sample every 12 of its clocks (1.5 MHz: ymfm's medium fidelity, MAME's for the
 *    YM2203, 125 kHz), sample n from Z80 time 24 n: the samples that start before a write are made without it.
 */
#ifndef GSOUND_H
#define GSOUND_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Power-on with the sound program (the "audiocpu" region, 32 KB at least): the Z80 held in reset (LS259 Q1 0);
   in MAME mode the Z80 starts a cycle after its release (docs/accuracy.md #7) */
void gs_power_on(const uint8_t *program, int mame_mode);

/* The board's time moves on: the Z80 and the chips up to Z80 time 'time' */
void gs_run_until(uint64_t time);

/* The 6809 writes the sound latch (3A00) at Z80 time 'time' */
void gs_latch(uint64_t time, uint8_t value);

/* The LS259's Q1 at Z80 time 'time' (1: the Z80 runs, 0: the Z80 and the YM2203s held in reset) */
void gs_q1(uint64_t time, int value);

/* The Z80's RAM (C000-C7FF), its FNV-1a, and its PC */
const uint8_t *gs_ram(void);
uint32_t gs_ram_hash(void);
uint16_t gs_pc(void);
uint64_t gs_time(void);

/* Every YM2203 write from now on to 'out': "time chip offset value" (time in Z80 cycles, chip 0 or 1, offset 0
   the address port, 1 the data port) */
void gs_log_writes(FILE *out);

/* The Z80's RAM as it makes each YM2203 write (a moment MAME's run has exactly): "write hash pc", 'write' the
   write's number from 0 */
void gs_ram_at_writes(FILE *out);

/* The mix, as MAME routes it (gng.cpp: each chip's SSG A, B, C at 0.40, its FM at 0.20, one speaker), at the
   chips' rate (125 kHz), from power-on: written to 'out' as 16-bit mono (gs_close_wav ends the file) */
void gs_wav(FILE *out);
void gs_close_wav(void);
uint32_t gs_sample_rate(void);

/* The addresses the Z80 executed (8 KB: address >> 3, bit address & 7) */
const uint8_t *gs_executed(void);

#ifdef __cplusplus
}
#endif

#endif
