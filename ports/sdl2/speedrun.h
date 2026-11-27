/**
 *    Name:        speedrun.h
 *    Description: the speedrun timer: a game's time from the START that began it to the end the player chose
 *                 (Astaroth beaten in the first loop or the second, or none: to the game over), counted in the
 *                 board's frames, shown in the left side panel
 *    Version:     0.1.0 (draft)
 *    Created:     27-11-2026
 *
 *    license:BSD-3-Clause
 *    The time is the board's, not the computer's: VBLANKs, each 384 x 262 pixels at 6 MHz (1 / 59.637 s, as
 *    MAME's gng.cpp sets the screen). The slowdown is in it as the board has it (a lost frame is a VBLANK all the
 *    same); a pause, the menu, a hitch of the computer are not. A replay gives its game's time again, to the frame.
 *    The game's work RAM, read only (symbols.txt): <$28 a game on (game_on), <$7F the loop, <$A5 Astaroth's
 *    record (+0x10, while <$A4 is set), its type at +0x15 (0D), its state at +3 (2: dying, from the hit that kills
 *    him: astaroth_states' table at DA29).
 */
#ifndef MAKAIMURA_SPEEDRUN_H
#define MAKAIMURA_SPEEDRUN_H

#include <stdint.h>

/* where a run ends (makaimura.ini's speedrun_end) */
enum { SPEEDRUN_END_LOOP1, SPEEDRUN_END_LOOP2, SPEEDRUN_END_NEVER, SPEEDRUN_ENDS };

/* what the timer shows */
enum {
	SPEEDRUN_IDLE,         /* no run since the board's start (or a state was loaded) */
	SPEEDRUN_RUNNING,
	SPEEDRUN_FINISHED,     /* the end chosen reached: the time kept */
	SPEEDRUN_OVER          /* the game over (back to the attract mode) before it: the time kept */
};

typedef struct speedrun_view
{
	int status;
	uint64_t frames;       /* the run's time, in the board's frames */
} speedrun_view;

/* At each VBLANK, once the controls the board reads from it are set: 'pressed' the system port's bits newly
   pressed (active high: 0x01 start 1, 0x02 start 2), 'end' SPEEDRUN_END_* */
void speedrun_vblank(const uint8_t *ram, uint64_t frame, uint8_t pressed, int end);

/* The board reset or a state loaded: no run (the next START that begins a game starts one) */
void speedrun_clear(void);

speedrun_view speedrun_get(void);

/* The time as "M:SS.CC" or "H:MM:SS.CC" */
void speedrun_format(char *out, unsigned long size, uint64_t frames);

#endif
