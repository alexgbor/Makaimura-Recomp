/**
 *    Name:        rom_drop.h
 *    Description: the window that asks for the ROM set when the game does not find it whole: MAME's makaimurg.zip
 *                 and its parent gng.zip dropped onto it (together or one after the other)
 *    Version:     0.1.0
 *    Created:     20-11-2026
 *
 *    license:BSD-3-Clause
 *    Its letters are the window's own: the game's font is in the ROMs it waits for.
 */
#ifndef MAKAIMURA_ROM_DROP_H
#define MAKAIMURA_ROM_DROP_H

#include "gng/rom_set.h"

#include <stdbool.h>
#include <stddef.h>

/* The window, 'set' as found so far (its files are known by their CRC-32, whatever zip or name). Each dropped
   .zip or folder adds its files; once the set is whole the dropped .zips are copied into 'keep_folder' (the
   program's rom folder: found there from then on), a dropped folder's path into 'folder' (for makaimura.ini's
   [files] rom; "" if none). true: the set is whole; false: the window was closed */
bool rom_drop_screen(gng_rom_set *set, const char *keep_folder, char *folder, size_t folder_size);

/* For checks (makaimura --drop, --drop-shot): the window hidden, these places dropped one after the other
   (comma separated), its picture after the last into 'shot' (BMP), and then closed if the set is not whole */
void rom_drop_check(const char *drops, const char *shot);

#endif
