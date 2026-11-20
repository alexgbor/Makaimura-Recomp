/**
 *    Name:        hiscore.h
 *    Description: the high scores kept between sessions: the program's ranking written back into its work RAM as
 *                 the board starts, and kept in a file (makaimura.hi beside the settings) whenever it changes
 *    Version:     0.1.0
 *    Created:     20-11-2026
 *
 *    license:BSD-3-Clause
 *    What the program does (symbols.txt): at the power on 60FD-6132 fill the ranking, ten entries of 7 bytes at
 *    152C-1571 (the score, 4 BCD bytes, then 3 letters: 10000 and the names of a table at 61AA), then the ten
 *    entries' addresses at 1518-152B in the ranking's order (the name entry moves these); main_loop_start (6134)
 *    sets the top score <$D0-$D3 to 10000, which 545B raises as a score passes it. Nothing writes them again but the
 *    name entry, so, as MAME's hiscore plugin does, the kept ranking is written back at the first VBLANK where the
 *    program's own is in place (the addresses in order, every score and the top score 10000), the top score the
 *    ranking's first. The file holds 1518-1571 as the program keeps them: the user's own, never in the repository.
 */
#ifndef MAKAIMURA_HISCORE_H
#define MAKAIMURA_HISCORE_H

#include <stdint.h>

#define HISCORE_FIRST 0x1518u
#define HISCORE_BYTES 0x5Au         /* 1518-1571 */
#define HISCORE_TOP   0x00D0u       /* <$D0-$D3 */

/* The kept ranking from 'path' (none if it is not one: then the program's own is kept from the first change) */
void hiscore_open(const char *path);

/* The board powered on: the ranking written back once the program has its own in place */
void hiscore_power_on(void);

/* At each VBLANK, with the work RAM: written back when it is time ('wrote' told each byte, for a recording of
   the controls: as "mADDR" changes), the file written a second after the ranking changed */
void hiscore_vblank(uint8_t *ram, uint64_t frame, void (*wrote)(uint64_t frame, uint16_t address, uint8_t value));

/* A state was loaded: the kept ranking into its RAM (the scores made since the state was saved stay) */
void hiscore_state_loaded(uint8_t *ram);

/* The window closes: the file written if the ranking changed since */
void hiscore_close(void);

#endif
