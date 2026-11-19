/**
 *    Name:        main.c
 *    Description: gng_headless: the translated program (build/generated/program) on the board model, with no
 *                 window: frame by frame, the same outputs as the reference's (tools/reference/gref.cpp), to be
 *                 compared with it and with MAME
 *    Version:     0.1.0
 *    Created:     19-11-2026
 *
 *    license:BSD-3-Clause
 *    gng_headless [options]
 *      --regions FILE    the ROM regions (python tools/makaimura_rom.py --extract FILE); default build/makaimurg.regions
 *      --frames N        how many frames (VBLANKs) to run (default 600)
 *      --accuracy MODE   mame or board (docs/accuracy.md)
 *      --bus-hashes FILE at each VBLANK the bus 0000-2FFF's FNV-1a: "frame hash pc" (pc 0000: the translation has
 *                        no PC in the middle of an instruction), as gref --bus-hashes writes it
 *      --irq-hashes FILE as the CPU takes each IRQ (its state pushed): "n hash pc cycle", as gref --irq-hashes
 *      --inputs FILE     an input script: "FRAME PORT VALUE" (system, p1, p2, dsw1, dsw2; hex, active low),
 *                        applied at that frame's VBLANK for the frames after
 *      --dsw1 HEX, --dsw2 HEX   the DIP switches from power-on
 *      --sound FILE      the sound board too (the translated sound program, runtime/src/sound.c), and at each
 *                        VBLANK "frame hash 0000 time" (the Z80's RAM, its time), as gref --sound
 *      --ym-writes FILE  with --sound: every YM2203 write, "time chip offset value" (Z80 cycles), as gref's
 *      --sound-marks FILE  with --sound: the Z80's RAM at each YM2203 write, "write hash 0000", as gref's (the PC
 *                        not kept: the translation has none inside an instruction)
 *      --wav FILE        with --sound: the mix (MAME's routes) at the chips' 125 kHz, as gref --wav writes it
 *      --pictures N FILE every Nth frame's picture, sampled as gref's (frame N's indexes in frame N+1's colours)
 *      --picture-hashes FILE   each frame's picture sampled as gref's, its FNV-1a: "frame hash"
 *      --ppm FRAME FILE  that frame's picture as a PPM image
 */
#include "gng/board.h"
#include "gng/video.h"
#include "m6809/cpu.h"
#include "gng/sound.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct region { char name[17]; uint32_t size; uint8_t *data; } region;
static region regions[16];
static int region_count;

static const uint8_t *find_region(const char *name)
{
	for (int i = 0; i < region_count; i++)
		if (!strcmp(regions[i].name, name))
			return regions[i].data;
	return NULL;
}

static int read_regions(const char *path)
{
	FILE *file = fopen(path, "rb");
	if (!file)
		return 0;
	char magic[4];
	if (fread(magic, 1, 4, file) != 4 || memcmp(magic, "MKRG", 4))
	{
		fclose(file);
		return 0;
	}
	while (region_count < 16)
	{
		region *r = &regions[region_count];
		memset(r->name, 0, sizeof(r->name));
		if (fread(r->name, 1, 16, file) != 16)
			break;
		uint8_t size[4];
		if (fread(size, 1, 4, file) != 4)
			break;
		r->size = size[0] | (size[1] << 8) | (size[2] << 16) | ((uint32_t)size[3] << 24);
		r->data = malloc(r->size);
		if (!r->data || fread(r->data, 1, r->size, file) != r->size)
			break;
		region_count++;
	}
	fclose(file);
	return region_count > 0;
}

typedef struct input_change { int frame; char port[16]; uint8_t value; } input_change;
static input_change *inputs;
static int input_count, next_input;

static void read_inputs(const char *path)
{
	FILE *file = fopen(path, "r");
	if (!file)
	{
		fprintf(stderr, "gng_headless: no input script %s\n", path);
		exit(2);
	}
	char line[256];
	int capacity = 0;
	while (fgets(line, sizeof(line), file))
	{
		if (line[0] == '#')
			continue;
		input_change change;
		unsigned value;
		if (sscanf(line, "%d %15s %x", &change.frame, change.port, &value) == 3)
		{
			change.value = (uint8_t)value;
			if (input_count == capacity)
			{
				capacity = capacity ? capacity * 2 : 64;
				inputs = realloc(inputs, (size_t)capacity * sizeof(*inputs));
			}
			inputs[input_count++] = change;
		}
	}
	fclose(file);
}

static void apply_input(const input_change *change)
{
	if (!strcmp(change->port, "system")) gng.inputs.system = change->value;
	else if (!strcmp(change->port, "p1")) gng.inputs.p1 = change->value;
	else if (!strcmp(change->port, "p2")) gng.inputs.p2 = change->value;
	else if (!strcmp(change->port, "dsw1")) gng.inputs.dsw1 = change->value;
	else if (!strcmp(change->port, "dsw2")) gng.inputs.dsw2 = change->value;
	else fprintf(stderr, "gng_headless: unknown port %s\n", change->port);
}

static uint32_t bus_hash(void)
{
	uint32_t hash = 2166136261u;
	for (unsigned address = 0; address < 0x3000; address++)
		hash = (hash ^ gng_read((uint16_t)address)) * 16777619u;
	return hash;
}

static FILE *bus_hashes, *irq_hashes, *pictures, *picture_hashes;
static FILE *sound_out, *ym_out, *marks_out, *wav_out;
static uint32_t wav_samples;

static void on_sample(int16_t sample)
{
	const uint8_t bytes[2] = { (uint8_t)sample, (uint8_t)((uint16_t)sample >> 8) };
	fwrite(bytes, 1, 2, wav_out);
	wav_samples++;
}

static void put32(uint8_t *p, uint32_t v)
{
	p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}

/* gsound's WAV: 16-bit mono at 125 kHz */
static void close_wav(void)
{
	uint8_t header[44];
	memcpy(header, "RIFF", 4);
	put32(header + 4, 36 + wav_samples * 2);
	memcpy(header + 8, "WAVEfmt ", 8);
	put32(header + 16, 16);
	header[20] = 1; header[21] = 0;
	header[22] = 1; header[23] = 0;
	put32(header + 24, GNG_SOUND_RATE);
	put32(header + 28, GNG_SOUND_RATE * 2);
	header[32] = 2; header[33] = 0;
	header[34] = 16; header[35] = 0;
	memcpy(header + 36, "data", 4);
	put32(header + 40, wav_samples * 2);
	fseek(wav_out, 0, SEEK_SET);
	fwrite(header, 1, sizeof(header), wav_out);
	fclose(wav_out);
}
static int sound;

static void on_latch(gng_board *board, uint8_t value)
{
	(void)board;
	if (sound)
		gng_sound_latch(2 * cpu.cycles, value);    /* (Z80 cycles: two a 6809 cycle) */
}

static void on_ls259(gng_board *board, unsigned output, unsigned value)
{
	(void)board;
	if (sound && output == 1)
		gng_sound_q1(2 * cpu.cycles, (int)value);
}

static void on_ym(uint64_t time, int chip, int offset, uint8_t value)
{
	if (marks_out)
		fprintf(marks_out, "%llu %08x 0000\n", (unsigned long long)(gng_snd.ym_writes - 1), gng_sound_ram_hash());
	if (ym_out)
		fprintf(ym_out, "%llu %d %d %02x\n", (unsigned long long)time, chip, offset, value);
}
static int pictures_every, ppm_frame = -1;
static const char *ppm_path;
static uint8_t check_indexes[GNG_SCREEN_WIDTH * GNG_SCREEN_HEIGHT];
static uint64_t irqs;

static uint64_t picture_frame;

static void write_check_picture(void)
{
	static uint8_t rgb[GNG_SCREEN_WIDTH * GNG_SCREEN_HEIGHT * 3];
	for (size_t i = 0; i < sizeof(check_indexes); i++)
	{
		const uint8_t index = check_indexes[i];
		rgb[3 * i] = (uint8_t)((gng.palette_ext[index] >> 4) * 17);
		rgb[3 * i + 1] = (uint8_t)((gng.palette_ext[index] & 15) * 17);
		rgb[3 * i + 2] = (uint8_t)((gng.palette_base[index] >> 4) * 17);
	}
	if (pictures)
		fwrite(rgb, 1, sizeof(rgb), pictures);
	if (picture_hashes)
	{
		uint32_t hash = 2166136261u;
		for (size_t i = 0; i < sizeof(rgb); i++)
			hash = (hash ^ rgb[i]) * 16777619u;
		fprintf(picture_hashes, "%llu %08x\n", (unsigned long long)picture_frame, hash);
	}
}

static void on_vblank(uint64_t frame)
{
	if (sound)
	{
		/* the sound board up to the VBLANK */
		if (gng_sound_run_until(2 * (GNG_VBLANK_CYCLE + frame * GNG_CPU_CYCLES_PER_FRAME)) < 0)
			fprintf(stderr, "gng_headless: the sound program faulted\n");
		if (sound_out)
			fprintf(sound_out, "%llu %08x 0000 %llu\n", (unsigned long long)frame, gng_sound_ram_hash(),
				(unsigned long long)gng_sound_time());
	}
	if (bus_hashes)
		fprintf(bus_hashes, "%llu %08x %04x\n", (unsigned long long)frame, bus_hash(), 0);
	if ((pictures || picture_hashes) && frame >= 1 && (frame - 1) % pictures_every == 0)
	{
		picture_frame = frame - 1;
		write_check_picture();
	}
	if ((pictures || picture_hashes) && frame % pictures_every == 0)
		gng_video_render_indexed(check_indexes);
	if ((int64_t)frame == ppm_frame)
	{
		static uint32_t picture[GNG_SCREEN_WIDTH * GNG_SCREEN_HEIGHT];
		gng_video_render(picture);
		FILE *ppm = fopen(ppm_path, "wb");
		fprintf(ppm, "P6\n%d %d\n255\n", GNG_SCREEN_WIDTH, GNG_SCREEN_HEIGHT);
		for (size_t i = 0; i < sizeof(picture) / 4; i++)
		{
			const uint8_t rgb[3] = { (uint8_t)(picture[i] >> 16), (uint8_t)(picture[i] >> 8), (uint8_t)picture[i] };
			fwrite(rgb, 1, 3, ppm);
		}
		fclose(ppm);
	}
	while (next_input < input_count && inputs[next_input].frame <= (int64_t)frame)
		apply_input(&inputs[next_input++]);
}

static void on_irq(uint16_t pc, uint64_t cycles)
{
	if (irq_hashes)
		fprintf(irq_hashes, "%llu %08x %04x %llu\n", (unsigned long long)irqs, bus_hash(), pc, (unsigned long long)cycles);
	irqs++;
}

static void on_fault(const char *what, uint16_t pc)
{
	fprintf(stderr, "gng_headless: %s at %04X (bank %u, cycle %llu)\n", what, pc, gng.bank, (unsigned long long)cpu.cycles);
}

int main(int argc, char **argv)
{
	const char *regions_path = "build/makaimurg.regions";
	long frames = 600;
	int dsw1 = GNG_DSW1_DEFAULT, dsw2 = GNG_DSW2_DEFAULT;
	gng_accuracy_mode accuracy = GNG_ACCURACY_MAME;
	for (int i = 1; i < argc; i++)
	{
		const char *a = argv[i];
#define NEXT() (i + 1 < argc ? argv[++i] : (fprintf(stderr, "gng_headless: %s needs a value\n", a), exit(2), ""))
		if (!strcmp(a, "--regions")) regions_path = NEXT();
		else if (!strcmp(a, "--frames")) frames = atol(NEXT());
		else if (!strcmp(a, "--accuracy")) accuracy = !strcmp(NEXT(), "board") ? GNG_ACCURACY_BOARD : GNG_ACCURACY_MAME;
		else if (!strcmp(a, "--bus-hashes")) bus_hashes = fopen(NEXT(), "w");
		else if (!strcmp(a, "--irq-hashes")) irq_hashes = fopen(NEXT(), "w");
		else if (!strcmp(a, "--sound")) { sound = 1; sound_out = fopen(NEXT(), "w"); }
		else if (!strcmp(a, "--ym-writes")) ym_out = fopen(NEXT(), "w");
		else if (!strcmp(a, "--sound-marks")) marks_out = fopen(NEXT(), "w");
		else if (!strcmp(a, "--wav")) wav_out = fopen(NEXT(), "wb");
		else if (!strcmp(a, "--inputs")) read_inputs(NEXT());
		else if (!strcmp(a, "--dsw1")) dsw1 = (int)strtoul(NEXT(), NULL, 16) & 0xFF;
		else if (!strcmp(a, "--dsw2")) dsw2 = (int)strtoul(NEXT(), NULL, 16) & 0xFF;
		else if (!strcmp(a, "--pictures")) { pictures_every = atoi(NEXT()); pictures = fopen(NEXT(), "wb"); }
		else if (!strcmp(a, "--picture-hashes")) { picture_hashes = fopen(NEXT(), "w"); pictures_every = 1; }
		else if (!strcmp(a, "--ppm")) { ppm_frame = atoi(NEXT()); ppm_path = NEXT(); }
		else { fprintf(stderr, "gng_headless: unknown option %s\n", a); return 2; }
#undef NEXT
	}
	if (!read_regions(regions_path) || !find_region("maincpu"))
	{
		fprintf(stderr, "gng_headless: no ROM regions in %s (python tools/makaimura_rom.py --extract %s)\n",
			regions_path, regions_path);
		return 2;
	}
	gng_roms roms = { 0 };
	roms.maincpu = find_region("maincpu");
	roms.audiocpu = find_region("audiocpu");
	roms.chars = find_region("chars");
	roms.tiles = find_region("tiles");
	roms.sprites = find_region("sprites");
	gng_power_on(&roms);
	gng.accuracy = accuracy;
	gng.inputs.dsw1 = (uint8_t)dsw1;
	gng.inputs.dsw2 = (uint8_t)dsw2;
	gng_video_decode();
	if (sound)
	{
		if (!roms.audiocpu)
		{
			fprintf(stderr, "gng_headless: no audiocpu region\n");
			return 2;
		}
		gng_snd.on_ym_write = on_ym;
		if (wav_out)
		{
			static const uint8_t blank[44] = { 0 };
			fwrite(blank, 1, sizeof(blank), wav_out);
			gng_snd.on_sample = on_sample;
		}
		gng_sound_power_on(roms.audiocpu, accuracy == GNG_ACCURACY_MAME);
		gng.on_sound_latch = on_latch;
		gng.on_ls259 = on_ls259;
	}

	m6809_host host = { on_vblank, on_irq, on_fault };
	m6809_power_on(&host);
	const int result = m6809_run((uint64_t)frames);
	if (bus_hashes) fclose(bus_hashes);
	if (irq_hashes) fclose(irq_hashes);
	if (pictures) fclose(pictures);
	if (picture_hashes) fclose(picture_hashes);
	if (sound_out) fclose(sound_out);
	if (ym_out) fclose(ym_out);
	if (marks_out) fclose(marks_out);
	if (wav_out) close_wav();
	return result ? 1 : 0;
}
