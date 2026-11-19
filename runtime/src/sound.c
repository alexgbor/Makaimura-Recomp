/**
 *    Name:        sound.c
 *    Description: the board's sound side (gng/sound.h): the Z80's bus, the latch's writes by time, the IRQ timer, the
 *                 reset line, the YM2203 writes
 *    Version:     0.1.0
 *    Created:     19-11-2026
 *
 *    license:BSD-3-Clause
 *    After MAME 0.289's capcom/gng.cpp (BSD-3-Clause, Pierpaolo Prazzoli): ROM 0000-7FFF, RAM C000-C7FF, the latch
 *    read at C800, the YM2203s at E000-E001 and E002-E003 (written only), an unmapped read 0; the IRQ held 240
 *    times a second (12500 Z80 cycles), restarted as the reset line is released (diexec.cpp's
 *    interface_post_reset), none while it is held (trigger_periodic_interrupt); the YM2203s reset while Q1 is 0.
 */
#include "gng/sound.h"

#include "z80/cpu.h"

#include <string.h>

#define IRQ_PERIOD 12500u
#define LATCH_HISTORY 16u

gng_sound gng_snd;

static const uint8_t *program;
static int mame;
/* the latch's last writes, the newest last: a read at time t takes the newest made at t or before */
static uint64_t latch_time[LATCH_HISTORY];
static uint8_t latch_value[LATCH_HISTORY];
static unsigned latch_count;

static uint8_t latch_at(uint64_t time)
{
	for (unsigned i = latch_count; i > 0; i--)
	{
		const unsigned index = (i - 1) % LATCH_HISTORY;
		if (latch_time[index] <= time)
			return latch_value[index];
	}
	return 0;
}

uint32_t z80_read8(uint32_t address)
{
	address &= 0xFFFFu;
	if (address < 0x8000u)
		return program[address];
	if (address >= 0xC000u && address < 0xC800u)
		return gng_snd.ram[address - 0xC000u];
	if (address == 0xC800u)
		return latch_at(z80.cycles);
	return 0;
}

void z80_write8(uint32_t address, uint8_t value)
{
	address &= 0xFFFFu;
	if (address >= 0xC000u && address < 0xC800u)
		gng_snd.ram[address - 0xC000u] = value;
	else if (address >= 0xE000u && address <= 0xE003u)
	{
		gng_snd.ym_writes++;
		if (gng_snd.on_ym_write)
			gng_snd.on_ym_write(z80.cycles, (int)((address >> 1) & 1), (int)(address & 1), value);
	}
}

static uint64_t irq_timer(uint64_t time)
{
	if (!gng_snd.in_reset)
		z80_set_irq(1);
	return time + IRQ_PERIOD;
}

void gng_sound_power_on(const uint8_t *rom, int mame_mode)
{
	void (*hook)(uint64_t, int, int, uint8_t) = gng_snd.on_ym_write;
	memset(&gng_snd, 0, sizeof(gng_snd));
	gng_snd.on_ym_write = hook;
	gng_snd.in_reset = 1;
	program = rom;
	mame = mame_mode;
	latch_count = 0;
	z80_init(0);
	z80_set_timer(UINT64_MAX, irq_timer);
}

int gng_sound_run_until(uint64_t time)
{
	if (gng_snd.in_reset)
	{
		if (z80.cycles < time)
			z80.cycles = time;
		return 0;
	}
	return z80.cycles < time ? z80_run(time) : 0;
}

void gng_sound_latch(uint64_t time, uint8_t value)
{
	/* the write is known from its time on before the Z80 runs up to it: a read the Z80 makes at that time or
	   later, in the instruction it stops in, sees it (MAME stops its core at the first machine cycle at or after) */
	const unsigned index = latch_count % LATCH_HISTORY;
	latch_time[index] = time;
	latch_value[index] = value;
	latch_count++;
	gng_sound_run_until(time);
}

void gng_sound_q1(uint64_t time, int value)
{
	gng_sound_run_until(time);
	if (!value)
	{
		gng_snd.in_reset = 1;    /* (a held IRQ stays held) */
		return;
	}
	if (gng_snd.in_reset)
	{
		/* released: the Z80 reset, on from a cycle later in MAME mode; its IRQ timer restarted */
		const uint64_t start = time + (mame ? 1u : 0u);
		gng_snd.in_reset = 0;
		z80_reset(start);
		z80_set_timer(start + IRQ_PERIOD, irq_timer);
	}
}

uint32_t gng_sound_ram_hash(void)
{
	uint32_t hash = 2166136261u;
	for (unsigned i = 0; i < sizeof(gng_snd.ram); i++)
		hash = (hash ^ gng_snd.ram[i]) * 16777619u;
	return hash;
}

uint64_t gng_sound_time(void) { return z80.cycles; }
