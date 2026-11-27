/**
 *    Name:        speedrun.c
 *    Description: the speedrun timer (speedrun.h)
 *    Version:     0.1.0 (draft)
 *    Created:     27-11-2026
 *
 *    license:BSD-3-Clause
 */
#include "speedrun.h"

#include <stdio.h>

/* a frame: 384 x 262 pixel clocks at 6 MHz (MAME's gng.cpp: set_raw(6MHz, 384, 128, 0, 262, 22, 246)) */
#define FRAME_CLOCKS (384u * 262u)
#define PIXEL_CLOCK 6000000u

/* how long a START may come before the game it began is on (<$28 set): the attract mode sees it at its next
   step, a few frames later */
#define START_WINDOW 30

static speedrun_view view;
static uint64_t started;           /* the run's first frame: the VBLANK its START was read from */
static int64_t start_pressed = -1; /* a START with no game on: the frame the board first read it */
static int game_was_on;

void speedrun_clear(void)
{
	view.status = SPEEDRUN_IDLE;
	view.frames = 0;
	start_pressed = -1;
	game_was_on = 1;    /* (a game already on, a state's: not begun here) */
}

/* the hit that kills Astaroth: his record (<$A5, its +0x10, while <$A4 says he is on) in state 2, dying */
static int astaroth_beaten(const uint8_t *ram)
{
	if (!ram[0xA4])
		return 0;
	const unsigned record = (unsigned)(ram[0xA5] << 8 | ram[0xA6]) - 0x10;
	if (record < 0x0500 || record + 0x30 > 0x1E00)
		return 0;
	/* (+3 the state, +0x15 the type: 0D Astaroth's, object_astaroth; seen with astaroth6.txt) */
	return ram[record + 0x15] == 0x0D && ram[record + 3] == 2;
}

void speedrun_vblank(const uint8_t *ram, uint64_t frame, uint8_t pressed, int end)
{
	const int game_on = ram[0x28] != 0;
	if (!game_on && (pressed & 0x03))
	{
		/* (the first of the STARTs that may begin the game: a second one before it is on does not move it) */
		if (start_pressed < 0 || frame - (uint64_t)start_pressed > START_WINDOW)
			start_pressed = (int64_t)frame;
	}
	if (game_on && !game_was_on && start_pressed >= 0 && frame - (uint64_t)start_pressed <= START_WINDOW)
	{
		view.status = SPEEDRUN_RUNNING;
		started = (uint64_t)start_pressed;
	}
	if (game_on)
		start_pressed = -1;    /* (a START in the game, player 2's or another, changes nothing) */
	game_was_on = game_on;
	if (view.status != SPEEDRUN_RUNNING)
		return;
	view.frames = frame - started;
	if (!game_on)
		view.status = SPEEDRUN_OVER;
	else if (end != SPEEDRUN_END_NEVER && astaroth_beaten(ram) && ram[0x7F] == (end == SPEEDRUN_END_LOOP1 ? 0 : 1))
		view.status = SPEEDRUN_FINISHED;
}

speedrun_view speedrun_get(void)
{
	return view;
}

void speedrun_format(char *out, unsigned long size, uint64_t frames)
{
	/* centiseconds, rounded down: frames x 384 x 262 / 6 MHz */
	const uint64_t centis = frames * FRAME_CLOCKS * 100u / PIXEL_CLOCK;
	const unsigned cs = (unsigned)(centis % 100), s = (unsigned)(centis / 100 % 60),
		m = (unsigned)(centis / 6000 % 60), h = (unsigned)(centis / 360000);
	if (h)
		snprintf(out, size, "%u:%02u:%02u.%02u", h, m, s, cs);
	else
		snprintf(out, size, "%u:%02u.%02u", m, s, cs);
}
