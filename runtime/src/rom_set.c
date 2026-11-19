/**
 *    Name:        rom_set.c
 *    Description: the player's ROM set from its zips or folders (gng/rom_set.h)
 *    Version:     0.1.0
 *    Created:     19-11-2026
 *
 *    license:BSD-3-Clause
 *    The zip format (PKWARE's APPNOTE): the end of the central directory found from the file's end, each entry of
 *    the central directory (its method, CRC-32, sizes, the local header's offset), then the local header and the
 *    data, stored (0) or deflated (8). A file is taken only if what it gives has the CRC-32 it is known by.
 */
#include "gng/rom_set.h"

#include "gng/inflate.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { MAINCPU, AUDIOCPU, CHARS, TILES, SPRITES, PROMS };

typedef struct rom_file
{
	const char *name;
	uint32_t size;
	uint32_t crc;
	int region;
	uint32_t offset;
} rom_file;

/* ROM_START( makaimurg ), MAME's order (tools/makaimura_rom.py's FILES) */
static const rom_file files[GNG_ROM_FILES] = {
	{ "mj04g.bin", 0x4000, 0x757C94D3, MAINCPU, 0x04000 },
	{ "mj03g.bin", 0x8000, 0x61B043BB, MAINCPU, 0x08000 },
	{ "mj05g.bin", 0x8000, 0xF2FDCCF5, MAINCPU, 0x10000 },
	{ "gg2.bin", 0x8000, 0x615F5B6F, AUDIOCPU, 0x00000 },
	{ "gg1.bin", 0x4000, 0xECFCCF07, CHARS, 0x00000 },
	{ "gg11.bin", 0x4000, 0xDDD56FA9, TILES, 0x00000 },
	{ "gg10.bin", 0x4000, 0x7302529D, TILES, 0x04000 },
	{ "gg9.bin", 0x4000, 0x20035BDA, TILES, 0x08000 },
	{ "gg8.bin", 0x4000, 0xF12BA271, TILES, 0x0C000 },
	{ "gg7.bin", 0x4000, 0xE525207D, TILES, 0x10000 },
	{ "gg6.bin", 0x4000, 0x2D77E9B2, TILES, 0x14000 },
	{ "gng13.n4", 0x4000, 0x4613AFDC, SPRITES, 0x00000 },
	{ "gg16.bin", 0x4000, 0x06D7E5CA, SPRITES, 0x04000 },
	{ "gg15.bin", 0x4000, 0xBC1FE02D, SPRITES, 0x08000 },
	{ "gng16.l4", 0x4000, 0x608D68D5, SPRITES, 0x10000 },
	{ "gg13.bin", 0x4000, 0xE80C3FCA, SPRITES, 0x14000 },
	{ "gg12.bin", 0x4000, 0x7780A925, SPRITES, 0x18000 },
	{ "tbp24s10.14k", 0x0100, 0x0EAF5158, PROMS, 0x0000 },
	{ "63s141.2e", 0x0100, 0x4A1285A4, PROMS, 0x0100 },
};

#define LARGEST_FILE 0x8000u
#define LARGEST_ZIP (16u << 20)

static uint8_t *region_of(gng_rom_set *set, int region)
{
	switch (region)
	{
	case MAINCPU: return set->maincpu;
	case AUDIOCPU: return set->audiocpu;
	case CHARS: return set->chars;
	case TILES: return set->tiles;
	case SPRITES: return set->sprites;
	default: return set->proms;
	}
}

void gng_rom_set_clear(gng_rom_set *set)
{
	memset(set, 0, sizeof(*set));
	memset(set->sprites, 0xFF, sizeof(set->sprites));    /* (ROMREGION_ERASEFF) */
}

/* A missing file with this CRC-32 and size, or -1 */
static int wanted(const gng_rom_set *set, uint32_t crc, uint32_t size)
{
	for (int i = 0; i < GNG_ROM_FILES; i++)
		if (!set->found[i] && files[i].crc == crc && files[i].size == size)
			return i;
	return -1;
}

/* A file's data, if it is the set's file 'index': into its region */
static int take(gng_rom_set *set, int index, const uint8_t *data, size_t size)
{
	if (size != files[index].size || gng_crc32(data, size) != files[index].crc)
		return 0;
	memcpy(region_of(set, files[index].region) + files[index].offset, data, size);
	set->found[index] = 1;
	return 1;
}

static uint8_t *read_file(const char *path, size_t limit, size_t *size)
{
	FILE *file = fopen(path, "rb");
	if (!file)
		return NULL;
	uint8_t *data = NULL;
	if (fseek(file, 0, SEEK_END) == 0)
	{
		const long length = ftell(file);
		if (length > 0 && (size_t)length <= limit && fseek(file, 0, SEEK_SET) == 0)
		{
			data = malloc((size_t)length);
			if (data && fread(data, 1, (size_t)length, file) != (size_t)length)
			{
				free(data);
				data = NULL;
			}
			*size = (size_t)length;
		}
	}
	fclose(file);
	return data;
}

static uint32_t u16(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8; }
static uint32_t u32(const uint8_t *p) { return u16(p) | u16(p + 2) << 16; }

static int add_zip(gng_rom_set *set, const uint8_t *zip, size_t size)
{
	/* the end of the central directory: 22 bytes, then a comment of up to 64 KB */
	if (size < 22)
		return 0;
	size_t end = size - 22;
	const size_t lowest = size > 22 + 0xFFFFu ? size - 22 - 0xFFFFu : 0;
	while (u32(zip + end) != 0x06054B50u)
	{
		if (end == lowest)
			return 0;
		end--;
	}
	const uint32_t entries = u16(zip + end + 10);
	size_t at = u32(zip + end + 16);
	int count = 0;
	uint8_t data[LARGEST_FILE];
	for (uint32_t i = 0; i < entries; i++)
	{
		if (at + 46 > size || u32(zip + at) != 0x02014B50u)
			break;
		const uint32_t method = u16(zip + at + 10);
		const uint32_t crc = u32(zip + at + 16);
		const uint32_t packed = u32(zip + at + 20);
		const uint32_t unpacked = u32(zip + at + 24);
		const size_t local = u32(zip + at + 42);
		at += 46 + u16(zip + at + 28) + u16(zip + at + 30) + u16(zip + at + 32);
		const int index = wanted(set, crc, unpacked);
		if (index < 0 || local + 30 > size || u32(zip + local) != 0x04034B50u)
			continue;
		const size_t start = local + 30 + u16(zip + local + 26) + u16(zip + local + 28);
		if (start + packed > size)
			continue;
		if (method == 0 && packed == unpacked)
			count += take(set, index, zip + start, unpacked);
		else if (method == 8 && gng_inflate(zip + start, packed, data, sizeof(data)) == (long)unpacked)
			count += take(set, index, data, unpacked);
	}
	return count;
}

static int add_zip_file(gng_rom_set *set, const char *path)
{
	size_t size = 0;
	uint8_t *zip = read_file(path, LARGEST_ZIP, &size);
	if (!zip)
		return 0;
	const int count = add_zip(set, zip, size);
	free(zip);
	return count;
}

/* The set's missing files in 'folder', by their MAME names */
static int add_folder_files(gng_rom_set *set, const char *folder)
{
	int count = 0;
	for (int i = 0; i < GNG_ROM_FILES; i++)
	{
		if (set->found[i])
			continue;
		char path[1024];
		size_t size = 0;
		snprintf(path, sizeof(path), "%s/%s", folder, files[i].name);
		uint8_t *data = read_file(path, LARGEST_FILE, &size);
		if (data)
		{
			count += take(set, i, data, size);
			free(data);
		}
	}
	return count;
}

static int ends_with_zip(const char *path)
{
	const size_t length = strlen(path);
	if (length < 4)
		return 0;
	const char *e = path + length - 4;
	return e[0] == '.' && (e[1] | 0x20) == 'z' && (e[2] | 0x20) == 'i' && (e[3] | 0x20) == 'p';
}

int gng_rom_set_add(gng_rom_set *set, const char *place)
{
	if (ends_with_zip(place))
		return add_zip_file(set, place);
	static const char *const sets[] = { "makaimurg", "gng" };
	int count = 0;
	char path[1024];
	for (int i = 0; i < 2; i++)
	{
		snprintf(path, sizeof(path), "%s/%s.zip", place, sets[i]);
		count += add_zip_file(set, path);
		snprintf(path, sizeof(path), "%s/%s", place, sets[i]);
		count += add_folder_files(set, path);
	}
	return count + add_folder_files(set, place);
}

int gng_rom_set_missing(const gng_rom_set *set, char *names, size_t size)
{
	int missing = 0;
	size_t used = 0;
	if (names && size)
		names[0] = 0;
	for (int i = 0; i < GNG_ROM_FILES; i++)
	{
		if (set->found[i])
			continue;
		if (names && used < size)
			used += (size_t)snprintf(names + used, size - used, "%s%s", missing ? ", " : "", files[i].name);
		missing++;
	}
	return missing;
}

gng_roms gng_rom_set_roms(const gng_rom_set *set)
{
	gng_roms roms;
	roms.maincpu = set->maincpu;
	roms.audiocpu = set->audiocpu;
	roms.chars = set->chars;
	roms.tiles = set->tiles;
	roms.sprites = set->sprites;
	return roms;
}
