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

#include "gng/ym2203.h"
#include "z80/cpu.h"

#include <math.h>
#include <stddef.h>

#include <string.h>

#define IRQ_PERIOD 12500u
#define SAMPLE_PERIOD 24u     /* Z80 cycles a YM2203 sample takes: 12 of its 1.5 MHz clocks */
#define LATCH_HISTORY 16u

gng_sound gng_snd;

static const uint8_t *program;
static int mame;
/* the latch's last writes, the newest last: a read at time t takes the newest made at t or before */
static uint64_t latch_time[LATCH_HISTORY];
static uint8_t latch_value[LATCH_HISTORY];
static unsigned latch_count;
static ym2203 chips[2];

/* the chips' samples that start before 'time', mixed as MAME routes them (gng.cpp: each chip's SSG A, B, C at
   0.40, its FM at 0.20, one speaker: tools/reference/sound/gsound.cpp's mix) */
static void make_samples(uint64_t time)
{
	while (gng_snd.samples * SAMPLE_PERIOD + gng_snd.sample_phase < time)
	{
		int32_t a[4], b[4];
		ym2203_generate(&chips[0], a);
		ym2203_generate(&chips[1], b);
		gng_snd.samples++;
		if (gng_snd.on_sample)
		{
			/* (the same sums in the same order as gsound's, and lround: the same bits) */
			double mix = 0;
			mix += 0.20 * a[0] + 0.40 * (a[1] + a[2] + a[3]);
			mix += 0.20 * b[0] + 0.40 * (b[1] + b[2] + b[3]);
			const long rounded = lround(mix);
			const int value = rounded < -32768 ? -32768 : rounded > 32767 ? 32767 : (int)rounded;
			gng_snd.on_sample((int16_t)value);
		}
	}
}

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
		make_samples(z80.cycles);
		ym2203_write(&chips[(address >> 1) & 1], address & 1, value);
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
	void (*sample_hook)(int16_t) = gng_snd.on_sample;
	memset(&gng_snd, 0, sizeof(gng_snd));
	gng_snd.on_ym_write = hook;
	gng_snd.on_sample = sample_hook;
	for (int i = 0; i < 2; i++)
	{
		ym2203_init(&chips[i]);
		ym2203_reset(&chips[i]);
	}
	gng_snd.in_reset = 1;
	program = rom;
	mame = mame_mode;
	/* MAME's chips' sample grid (docs/accuracy.md #6b, tools/reference/sound/gsound.cpp's g_sample_phase) */
	gng_snd.sample_phase = mame_mode ? 3u : 0u;
	latch_count = 0;
	z80_init(0);
	z80_set_timer(UINT64_MAX, irq_timer);
}

int gng_sound_run_until(uint64_t time)
{
	int result = 0;
	if (gng_snd.in_reset)
	{
		if (z80.cycles < time)
			z80.cycles = time;
	}
	else if (z80.cycles < time)
		result = z80_run(time);
	make_samples(time);
	return result;
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
		/* the YM2203s reset (ym_reset_w), the Z80 held (a held IRQ stays held) */
		for (int i = 0; i < 2; i++)
			ym2203_reset(&chips[i]);
		gng_snd.in_reset = 1;
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

/* the state: gng_snd up to its hooks, the latch's history, the chips, the Z80 */
#define SOUND_DATA offsetof(gng_sound, on_ym_write)

size_t gng_sound_state_size(void)
{
	return SOUND_DATA + sizeof(latch_time) + sizeof(latch_value) + sizeof(uint32_t) + sizeof(chips) +
		sizeof(z80_state);
}

void gng_sound_state_save(uint8_t *out)
{
	const uint32_t count = latch_count;
	z80_state z;
	z80_state_get(&z);
	memcpy(out, &gng_snd, SOUND_DATA); out += SOUND_DATA;
	memcpy(out, latch_time, sizeof(latch_time)); out += sizeof(latch_time);
	memcpy(out, latch_value, sizeof(latch_value)); out += sizeof(latch_value);
	memcpy(out, &count, sizeof(count)); out += sizeof(count);
	memcpy(out, chips, sizeof(chips)); out += sizeof(chips);
	memcpy(out, &z, sizeof(z));
}

void gng_sound_state_load(const uint8_t *in)
{
	uint32_t count;
	z80_state z;
	memcpy(&gng_snd, in, SOUND_DATA); in += SOUND_DATA;
	memcpy(latch_time, in, sizeof(latch_time)); in += sizeof(latch_time);
	memcpy(latch_value, in, sizeof(latch_value)); in += sizeof(latch_value);
	memcpy(&count, in, sizeof(count)); in += sizeof(count);
	memcpy(chips, in, sizeof(chips)); in += sizeof(chips);
	memcpy(&z, in, sizeof(z));
	latch_count = count;
	z80_state_set(&z);
	z80_set_timer(z.timer_at, irq_timer);
}

uint32_t gng_sound_ram_hash(void)
{
	uint32_t hash = 2166136261u;
	for (unsigned i = 0; i < sizeof(gng_snd.ram); i++)
		hash = (hash ^ gng_snd.ram[i]) * 16777619u;
	return hash;
}

uint64_t gng_sound_time(void) { return z80.cycles; }
