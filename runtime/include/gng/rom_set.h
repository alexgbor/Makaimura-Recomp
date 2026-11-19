/**
 *    Name:        rom_set.h
 *    Description: the player's ROM set (MAME's makaimurg and its parent gng) found and loaded into the regions the
 *                 board model takes (gng/board.h's gng_roms), with no other tool
 *    Version:     0.1.0
 *    Created:     19-11-2026
 *
 *    license:BSD-3-Clause
 *    After MAME's src/mame/capcom/gng.cpp (BSD-3-Clause, Pierpaolo Prazzoli): ROM_START( makaimurg ) and the files
 *    it takes from ROM_START( gng ), as tools/makaimura_rom.py has them (the same table: a file is known by its
 *    CRC-32 and size, whatever its name or the zip it is in). A place is a .zip, or a folder: then its
 *    makaimurg.zip and gng.zip, its makaimurg/ and gng/ folders and the files in it by their MAME names.
 */
#ifndef GNG_ROM_SET_H
#define GNG_ROM_SET_H

#include "gng/board.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define GNG_ROM_FILES 19

typedef struct gng_rom_set
{
	uint8_t maincpu[0x18000];
	uint8_t audiocpu[0x10000];
	uint8_t chars[0x4000];
	uint8_t tiles[0x18000];
	uint8_t sprites[0x20000];
	uint8_t proms[0x200];
	uint8_t found[GNG_ROM_FILES];    /* each file of the set found (in the table's order) */
} gng_rom_set;

/* Empty regions (as MAME fills them: the sprites' 0xFF, the rest 0), no file found */
void gng_rom_set_clear(gng_rom_set *set);

/* The set's files at 'place' (a .zip or a folder) that are still missing: how many it found */
int gng_rom_set_add(gng_rom_set *set, const char *place);

/* How many files are still missing; their names into 'names' (comma separated, if not NULL) */
int gng_rom_set_missing(const gng_rom_set *set, char *names, size_t size);

/* The regions as the board takes them */
gng_roms gng_rom_set_roms(const gng_rom_set *set);

#ifdef __cplusplus
}
#endif

#endif
