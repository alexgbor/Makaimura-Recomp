/**
 *    Name:        state.c
 *    Description: save states (gng/state.h)
 *    Version:     0.1.0
 *    Created:     20-11-2026
 *
 *    license:BSD-3-Clause
 */
#include "gng/state.h"

#include "gng/board.h"
#include "gng/sound.h"
#include "gng/video.h"
#include "m6809/cpu.h"

#include <stddef.h>
#include <string.h>

/* the magic is the program's (m6809_state_magic): the translation's "MKRSTATE", the readable C's "MKRSTATC", whose
   states are taken where its main loop waits for work only (m6809/program.h); neither goes on from the other's */
#define MAGIC m6809_state_magic
#define VERSION 1u

/* the board's data: gng up to its ROMs and hooks (memories, latches, counters, inputs) */
#define BOARD_DATA offsetof(gng_board, roms)

typedef struct header
{
	char magic[8];
	uint32_t version;
	uint32_t accuracy;
	uint32_t rom_hash;          /* the program's ROMs (maincpu, audiocpu) */
	uint32_t pc;
	uint64_t frame;
	uint64_t sizes[4];          /* the parts': the 6809, the board, the video, the sound */
} header;

static uint32_t fnv(uint32_t hash, const uint8_t *bytes, size_t count)
{
	for (size_t i = 0; bytes && i < count; i++)
		hash = (hash ^ bytes[i]) * 16777619u;
	return hash;
}

static uint32_t rom_hash(void)
{
	uint32_t hash = fnv(2166136261u, gng.roms.maincpu, 0x18000);
	return fnv(hash, gng.roms.audiocpu, 0x8000);
}

static void sizes(uint64_t out[4])
{
	out[0] = sizeof(m6809_state);
	out[1] = BOARD_DATA;
	out[2] = gng_video_state_size();
	out[3] = gng_sound_state_size();
}

size_t gng_state_size(void)
{
	uint64_t s[4];
	sizes(s);
	return sizeof(header) + (size_t)(s[0] + s[1] + s[2] + s[3]);
}

void gng_state_save(uint8_t *out, uint16_t pc)
{
	header h;
	memset(&h, 0, sizeof(h));
	memcpy(h.magic, MAGIC, sizeof(h.magic));
	h.version = VERSION;
	h.accuracy = (uint32_t)gng.accuracy;
	h.rom_hash = rom_hash();
	h.pc = pc;
	m6809_state cpu_state;
	m6809_state_get(&cpu_state);
	h.frame = cpu_state.frame;
	sizes(h.sizes);
	memcpy(out, &h, sizeof(h));
	out += sizeof(h);
	memcpy(out, &cpu_state, sizeof(cpu_state));
	out += sizeof(cpu_state);
	memcpy(out, &gng, BOARD_DATA);
	out += BOARD_DATA;
	gng_video_state_save(out);
	out += gng_video_state_size();
	gng_sound_state_save(out);
}

const char *gng_state_check(const uint8_t *in, size_t size)
{
	header h;
	if (size < sizeof(h))
		return "not a save state";
	memcpy(&h, in, sizeof(h));
	if (memcmp(h.magic, MAGIC, sizeof(h.magic)))
	{
		if (!memcmp(h.magic, "MKRSTATE", sizeof(h.magic)))
			return "a state of the translated program";
		if (!memcmp(h.magic, "MKRSTATC", sizeof(h.magic)))
			return "a state of the readable C";
		return "not a save state";
	}
	uint64_t s[4];
	sizes(s);
	if (h.version != VERSION || memcmp(h.sizes, s, sizeof(s)) || size != gng_state_size())
		return "a state from another version";
	if (h.rom_hash != rom_hash())
		return "a state from another ROM set";
	if (h.accuracy != (uint32_t)gng.accuracy)
		return h.accuracy == GNG_ACCURACY_BOARD ? "a state from board mode" : "a state from MAME mode";
	return NULL;
}

uint64_t gng_state_frame(const uint8_t *in)
{
	header h;
	memcpy(&h, in, sizeof(h));
	return h.frame;
}

uint8_t *gng_state_ram(uint8_t *in)
{
	return in + sizeof(header) + sizeof(m6809_state) + offsetof(gng_board, ram);
}

void gng_state_load(const uint8_t *in)
{
	header h;
	memcpy(&h, in, sizeof(h));
	in += sizeof(h);
	m6809_state cpu_state;
	memcpy(&cpu_state, in, sizeof(cpu_state));
	in += sizeof(cpu_state);
	/* (the board's data only: its ROMs, its mode and its hooks stay the host's) */
	memcpy(&gng, in, BOARD_DATA);
	in += BOARD_DATA;
	gng_video_state_load(in);
	in += gng_video_state_size();
	gng_sound_state_load(in);
	m6809_state_set(&cpu_state);
	m6809_resume((uint16_t)h.pc);
}
