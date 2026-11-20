/**
 *    Name:        hiscore.c
 *    Description: the high scores kept between sessions (hiscore.h)
 *    Version:     0.1.0
 *    Created:     20-11-2026
 *
 *    license:BSD-3-Clause
 */
#include "hiscore.h"

#include <stdio.h>
#include <string.h>

#define ENTRIES 10u
#define ENTRY_BYTES 7u
#define FIRST_ENTRY 0x152Cu
#define MAGIC "MKRHI1\n"

static char file_path[1300];
static uint8_t kept[HISCORE_BYTES];      /* the ranking as the file has it (or the program, once it changed it) */
static int have_kept;                    /* the file had one */
static int in_place;                     /* written back (or the program's own taken): the RAM's is followed */
static int dirty;
static uint64_t changed_at;

static int bcd(uint8_t byte)
{
	return (byte >> 4) < 10 && (byte & 15) < 10;
}

/* a ranking as the program keeps it: the ten addresses each an entry's, once each; the scores BCD */
static int valid(const uint8_t *table)
{
	unsigned seen = 0;
	for (unsigned i = 0; i < ENTRIES; i++)
	{
		const unsigned address = (unsigned)table[2 * i] << 8 | table[2 * i + 1];
		if (address < FIRST_ENTRY || (address - FIRST_ENTRY) % ENTRY_BYTES || (address - FIRST_ENTRY) / ENTRY_BYTES >= ENTRIES)
			return 0;
		seen |= 1u << ((address - FIRST_ENTRY) / ENTRY_BYTES);
		const uint8_t *entry = table + (address - HISCORE_FIRST);
		for (unsigned b = 0; b < 4; b++)
			if (!bcd(entry[b]))
				return 0;
	}
	return seen == (1u << ENTRIES) - 1;
}

/* the program's own ranking just made (60FD-6132, 6134): the addresses in order, 10000 everywhere */
static int fresh(const uint8_t *ram)
{
	static const uint8_t ten_thousand[4] = { 0x00, 0x01, 0x00, 0x00 };
	if (memcmp(ram + HISCORE_TOP, ten_thousand, 4))
		return 0;
	for (unsigned i = 0; i < ENTRIES; i++)
	{
		const unsigned address = FIRST_ENTRY + i * ENTRY_BYTES;
		const uint8_t *pointer = ram + HISCORE_FIRST + 2 * i;
		if (pointer[0] != address >> 8 || pointer[1] != (address & 0xFF) || memcmp(ram + address, ten_thousand, 4))
			return 0;
	}
	return 1;
}

static void save(void)
{
	FILE *file = fopen(file_path, "wb");
	if (!file)
		return;
	fwrite(MAGIC, 1, sizeof(MAGIC) - 1, file);
	fwrite(kept, 1, sizeof(kept), file);
	fclose(file);
	dirty = 0;
}

void hiscore_open(const char *path)
{
	snprintf(file_path, sizeof(file_path), "%s", path);
	have_kept = 0;
	FILE *file = fopen(path, "rb");
	if (!file)
		return;
	uint8_t data[sizeof(MAGIC) - 1 + HISCORE_BYTES + 1];
	const size_t got = fread(data, 1, sizeof(data), file);
	fclose(file);
	if (got == sizeof(data) - 1 && !memcmp(data, MAGIC, sizeof(MAGIC) - 1) && valid(data + sizeof(MAGIC) - 1))
	{
		memcpy(kept, data + sizeof(MAGIC) - 1, HISCORE_BYTES);
		have_kept = 1;
	}
}

void hiscore_power_on(void)
{
	in_place = 0;
}

/* the top score <$D0-$D3: the ranking's first, if higher */
static void top_score(uint8_t *ram, uint64_t frame, void (*wrote)(uint64_t, uint16_t, uint8_t))
{
	const unsigned first = (unsigned)kept[0] << 8 | kept[1];
	const uint8_t *best = kept + (first - HISCORE_FIRST);
	if (memcmp(best, ram + HISCORE_TOP, 4) <= 0)
		return;
	for (unsigned b = 0; b < 4; b++)
	{
		ram[HISCORE_TOP + b] = best[b];
		if (wrote)
			wrote(frame, (uint16_t)(HISCORE_TOP + b), best[b]);
	}
}

void hiscore_vblank(uint8_t *ram, uint64_t frame, void (*wrote)(uint64_t, uint16_t, uint8_t))
{
	uint8_t *table = ram + HISCORE_FIRST;
	if (!in_place)
	{
		if (!fresh(ram))
			return;
		in_place = 1;
		if (!have_kept)
		{
			memcpy(kept, table, HISCORE_BYTES);
			return;
		}
		for (unsigned i = 0; i < HISCORE_BYTES; i++)
			if (table[i] != kept[i])
			{
				table[i] = kept[i];
				if (wrote)
					wrote(frame, (uint16_t)(HISCORE_FIRST + i), kept[i]);
			}
		top_score(ram, frame, wrote);
		return;
	}
	/* the program changed it (a name entered): kept once it has been still for a second */
	if (memcmp(table, kept, HISCORE_BYTES))
	{
		if (valid(table))
		{
			memcpy(kept, table, HISCORE_BYTES);
			have_kept = 1;
			dirty = 1;
			changed_at = frame;
		}
	}
	else if (dirty && frame >= changed_at + 60)
		save();
}

void hiscore_state_loaded(uint8_t *ram)
{
	/* (a state loaded before the board's start made its ranking: followed from its own if it has one) */
	if (!in_place)
	{
		if (!valid(ram + HISCORE_FIRST))
			return;
		in_place = 1;
		if (!have_kept)
		{
			memcpy(kept, ram + HISCORE_FIRST, HISCORE_BYTES);
			return;
		}
	}
	if (!have_kept)
		return;
	memcpy(ram + HISCORE_FIRST, kept, HISCORE_BYTES);
	top_score(ram, 0, NULL);
}

void hiscore_close(void)
{
	if (dirty)
		save();
}
