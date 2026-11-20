/**
 *    Name:        hud.h
 *    Description: the side panels: beside the picture on a wide window, what the game and the port are doing
 *                 (left: the player, from the game's memory; right: the board, the pace, the recording; the menu's
 *                 help while it is open)
 *    Version:     0.1.0 (draft)
 *    Created:     19-11-2026
 *
 *    license:BSD-3-Clause
 *    The game's work RAM, read only (symbols.txt): <$68-$6B the score (BCD), <$64-$67 the next extra life's score,
 *    <$60 the lives, <$73 the weapon (0 torch, 1 lance, 2 axe, 3 shield, 4 dagger), <$72 the stage (0-6), <$7F the
 *    loop (0, 1; 2 the ending), <$61 the rank (0-3F). Drawn in the game's font, at half the picture's pixel size.
 */
#ifndef MAKAIMURA_HUD_H
#define MAKAIMURA_HUD_H

#include <stdbool.h>
#include <stdint.h>

/* a panel's pixels: 14 characters by 63 rows, 2:9 as the room a 4:3 picture leaves on each side of a 16:9 screen */
#define HUD_W 112
#define HUD_H 504
#define HUD_HOTKEYS 5

typedef struct hud_info
{
	double fps;               /* the frames shown a second, measured */
	bool board_mode;
	uint32_t lost_frames;     /* VBLANKs whose IRQ the program did not take in time: its slowdown */
	uint32_t recent_lost;     /* of the last second's */
	uint8_t lost_last[60];    /* the last second's frames, the oldest first: 1 lost */
	uint32_t flash_frames;    /* board mode: frames with palette writes in the active display */
	bool recording;
	bool paused;
	bool menu;                /* the menu is open: the right panel shows its help */
	const char *help_title, *help_text;
	uint8_t p1, p2, system;   /* the controls as the board reads them (active low) */
	char hot_keys[HUD_HOTKEYS][7];    /* the window's keys' names: menu, pause, reset, record, full screen */
} hud_info;

/* The panels into two HUD_W x HUD_H buffers (0x00RRGGBB) */
void hud_draw(uint32_t *left, uint32_t *right, const hud_info *info);

#endif
