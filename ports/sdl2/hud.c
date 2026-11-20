/**
 *    Name:        hud.c
 *    Description: the side panels (hud.h)
 *    Version:     0.1.0 (draft)
 *    Created:     19-11-2026
 *
 *    license:BSD-3-Clause
 */
#include "hud.h"

#include "gng/board.h"
#include "gng/video.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

static const uint32_t white = 0xF0F0F0, yellow = 0xF8D040, cyan = 0x60D8F0, red = 0xF06070, grey = 0x8890A0,
	green = 0x60E070, section_back = 0x0C1020, frame_colour = 0x303A58, lit = 0xF8D040, unlit = 0x2C3448;

/* the panel's layout: sections, each a frame with its title, a GAP apart, as high together as the panel (the room
   their contents leave shared out between them, the contents centred in theirs); positions in pixels */
#define TITLE_ROOM 24       /* the title and the line under it */
#define PAD 8               /* the room left of the text and under a section's last line */
#define LINE 14             /* from a line of text to the next */
#define GAP 10

static void put_char(uint32_t *pixels, int x0, int y0, char c, uint32_t colour)
{
	if (c == ' ')
		return;
	for (int y = 0; y < 8; y++)
		for (int x = 0; x < 8; x++)
		{
			const int pen = gng_video_char_pen((unsigned char)toupper((unsigned char)c), x, y);
			if (pen == 3 || x0 + x < 0 || x0 + x >= HUD_W || y0 + y < 0 || y0 + y >= HUD_H)
				continue;
			const int level = pen == 0 ? 256 : pen == 1 ? 170 : 110;
			const uint32_t r = ((colour >> 16) & 0xFF) * level >> 8, g = ((colour >> 8) & 0xFF) * level >> 8,
				b = (colour & 0xFF) * level >> 8;
			pixels[(y0 + y) * HUD_W + x0 + x] = r << 16 | g << 8 | b;
		}
}

/* text from a pixel, at the section's left margin or against its right one */
static void text_at(uint32_t *pixels, int x, int y, const char *s, uint32_t colour)
{
	for (; *s && x < HUD_W; s++, x += 8)
		put_char(pixels, x, y, *s, colour);
}

static void text(uint32_t *pixels, int y, const char *s, uint32_t colour)
{
	text_at(pixels, PAD, y, s, colour);
}

static void right_text(uint32_t *pixels, int y, const char *s, uint32_t colour)
{
	text_at(pixels, HUD_W - PAD - 8 * (int)strlen(s), y, s, colour);
}

static void box(uint32_t *pixels, int x0, int y0, int w, int h, uint32_t colour)
{
	for (int y = y0; y < y0 + h; y++)
		for (int x = x0; x < x0 + w; x++)
			if (x >= 0 && x < HUD_W && y >= 0 && y < HUD_H)
				pixels[y * HUD_W + x] = colour;
}

/* a section's frame and title; its contents start TITLE_ROOM under y */
static void section(uint32_t *p, int y, int h, const char *title, uint32_t title_colour)
{
	box(p, 0, y, HUD_W, h, section_back);
	box(p, 0, y, HUD_W, 1, frame_colour);
	box(p, 0, y + h - 1, HUD_W, 1, frame_colour);
	box(p, 0, y, 1, h, frame_colour);
	box(p, HUD_W - 1, y, 1, h, frame_colour);
	text(p, y + 7, title, title_colour);
	box(p, 4, y + 19, HUD_W - 8, 1, frame_colour);
}

/* the sections' places: their heights grown to fill the panel, their tops, where their contents start (a section
   0 high is left out: the others share its room) */
typedef struct layout
{
	int top, height, content;
} layout;

static void spread(layout *out, const int *heights, int count)
{
	int total = 0, shown = 0;
	for (int i = 0; i < count; i++)
		if (heights[i])
			total += heights[i], shown++;
	total += GAP * (shown - 1);
	const int room = HUD_H - total > 0 ? HUD_H - total : 0;
	int y = 0;
	for (int i = 0, k = 0; i < count; i++)
	{
		out[i] = (layout){ 0 };
		if (!heights[i])
			continue;
		const int extra = room * (k + 1) / shown - room * k / shown;
		k++;
		out[i].top = y;
		out[i].height = heights[i] + extra;
		out[i].content = y + TITLE_ROOM + extra / 2;
		y += out[i].height + GAP;
	}
}

/* BCD bytes as a number, its leading zeros dropped ("-" if they are not BCD: the work RAM before the boot clears it) */
static void bcd(char *out, size_t size, const uint8_t *bytes, int count)
{
	char digits[16];
	int n = 0;
	for (int i = 0; i < count; i++)
	{
		if ((bytes[i] >> 4) > 9 || (bytes[i] & 15) > 9)
		{
			snprintf(out, size, "-");
			return;
		}
		digits[n++] = (char)('0' + (bytes[i] >> 4));
		digits[n++] = (char)('0' + (bytes[i] & 15));
	}
	digits[n] = 0;
	const char *start = digits;
	while (start[0] == '0' && start[1])
		start++;
	snprintf(out, size, "%s", start);
}

/* a player's controls under their label: the 4-way joystick's cross and the two buttons, lit while pressed */
#define CONTROLS_H 58
static void controls(uint32_t *pixels, int y, uint8_t port, const char *label)
{
	const int pressed = (uint8_t)~port;
	text(pixels, y, label, grey);
	const int cx = 26, cy = y + 30, s = 9;
	box(pixels, cx - s / 2, cy - s / 2, s, s, unlit);
	box(pixels, cx - s / 2, cy - s / 2 - s - 1, s, s, pressed & 0x08 ? lit : unlit);
	box(pixels, cx - s / 2, cy + s / 2 + 1, s, s, pressed & 0x04 ? lit : unlit);
	box(pixels, cx - s / 2 - s - 1, cy - s / 2, s, s, pressed & 0x02 ? lit : unlit);
	box(pixels, cx + s / 2 + 1, cy - s / 2, s, s, pressed & 0x01 ? lit : unlit);
	/* the buttons, their names under them (3 letters each) */
	const char *const names[2] = { "ATK", "JMP" };
	const uint32_t colours[2] = { red, cyan };
	for (int b = 0; b < 2; b++)
	{
		const int bx = 54 + 28 * b;
		box(pixels, bx, cy - 13, 18, 18, pressed & (0x10 << b) ? colours[b] : unlit);
		for (int i = 0; i < 3; i++)
			put_char(pixels, bx - 3 + 8 * i, cy + 9, names[b][i], grey);
	}
}

static void left_panel(uint32_t *p, const hud_info *info)
{
	static const char *const weapons[] = { "TORCH", "LANCE", "AXE", "SHIELD", "DAGGER" };
	const uint8_t *ram = gng.ram;
	char line[32];
	enum { PLAYER, STAGE, CONTROLS, COUNT };
	/* (the contents' heights: as they are drawn below) */
	const int heights[COUNT] = {
		TITLE_ROOM + 30 + 30 + 20 + 18 + PAD,
		TITLE_ROOM + 3 * LINE + 5 + PAD,
		TITLE_ROOM + 2 * CONTROLS_H + 4 + 8 + PAD,
	};
	layout at[COUNT];
	spread(at, heights, COUNT);

	/* <$28: a game is played (0: the attract mode's demo, 691C) */
	/* (and the RAM holding a game's values: not yet while the boot checks the board) */
	const int playing = ram[0x28] != 0 && ram[0x27] < 2 && ram[0x72] < 7;
	/* the player whose turn it is (<$27: 0 player 1; their score, lives... swapped into <$60-$8F) */
	section(p, at[PLAYER].top, at[PLAYER].height, !playing ? "DEMO" : ram[0x27] ? "PLAYER 2" : "PLAYER 1", yellow);
	int y = at[PLAYER].content;
	if (!playing)
	{
		text(p, y + 20, "INSERT COIN", grey);
		text(p, y + 50, "THEN PRESS", grey);
		text(p, y + 50 + LINE, "START", grey);
	}
	else
	{
		/* label, then the value under it, against the right */
		text(p, y, "SCORE", grey);
		bcd(line, sizeof(line), ram + 0x68, 4);
		right_text(p, y + 10, line, white);
		y += 30;
		text(p, y, "NEXT LIFE", grey);
		bcd(line, sizeof(line), ram + 0x64, 4);
		right_text(p, y + 10, line, white);
		y += 30;
		text(p, y, "LIVES", grey);
		snprintf(line, sizeof(line), ram[0x60] < 100 ? "%u" : "-", ram[0x60]);
		right_text(p, y, line, white);
		y += 20;
		text(p, y, "WEAPON", grey);
		right_text(p, y + 10, ram[0x73] < 5 ? weapons[ram[0x73]] : "-", cyan);
	}

	section(p, at[STAGE].top, at[STAGE].height, "STAGE", yellow);
	y = at[STAGE].content;
	text(p, y, "AREA", grey);
	snprintf(line, sizeof(line), ram[0x72] < 7 ? "%u/7" : "-", ram[0x72] + 1u);
	right_text(p, y, line, white);
	text(p, y + LINE, "LOOP", grey);
	right_text(p, y + LINE, ram[0x7F] == 0 ? "1ST" : ram[0x7F] == 1 ? "2ND" : ram[0x7F] == 2 ? "END" : "-", white);
	text(p, y + 2 * LINE, "RANK", grey);
	snprintf(line, sizeof(line), "%u/63", ram[0x61] & 0x3F);
	right_text(p, y + 2 * LINE, line, white);
	/* the rank as a bar */
	box(p, PAD, y + 3 * LINE, HUD_W - 2 * PAD, 5, unlit);
	box(p, PAD, y + 3 * LINE, (HUD_W - 2 * PAD) * (ram[0x61] & 0x3F) / 63, 5, red);

	section(p, at[CONTROLS].top, at[CONTROLS].height, "CONTROLS", yellow);
	y = at[CONTROLS].content;
	controls(p, y, info->p1, "PLAYER 1");
	controls(p, y + CONTROLS_H, info->p2, "PLAYER 2");
	const uint8_t pressed = (uint8_t)~info->system;
	y += 2 * CONTROLS_H + 4;
	text(p, y, "COIN", pressed & 0xC0 ? yellow : unlit);
	right_text(p, y, "START", pressed & 0x03 ? yellow : unlit);
}

/* the menu's help in a section as high as the panel, its lines broken at spaces to the section's width */
static void help(uint32_t *p, const hud_info *info)
{
	section(p, 0, HUD_H, info->help_title ? info->help_title : "MENU", yellow);
	const char *s = info->help_text ? info->help_text : "";
	int y = TITLE_ROOM;
	const int width = (HUD_W - 2 * PAD) / 8;
	while (*s && y < HUD_H - PAD - 8)
	{
		int n = (int)strlen(s), cut = n;
		if (n > width)
		{
			cut = width;
			while (cut > 0 && s[cut] != ' ')
				cut--;
			if (cut == 0)
				cut = width;
		}
		char line[32];
		snprintf(line, sizeof(line), "%.*s", cut, s);
		text(p, y, line, white);
		y += LINE - 2;
		s += cut;
		while (*s == ' ')
			s++;
	}
}

static void right_panel(uint32_t *p, const hud_info *info)
{
	char line[32];
	if (info->menu)
	{
		help(p, info);
		return;
	}
	enum { BOARD, SLOWDOWN, PALETTE, STATUS, KEYS, COUNT };
	const int heights[COUNT] = {
		TITLE_ROOM + 30 + 8 + PAD,
		TITLE_ROOM + 2 * LINE + 15 + PAD,
		info->debug ? TITLE_ROOM + LINE + 8 + PAD : 0,
		TITLE_ROOM + 2 * LINE + 8 + PAD,
		TITLE_ROOM + (HUD_HOTKEYS - 1) * 12 + 8 + PAD,
	};
	layout at[COUNT];
	spread(at, heights, COUNT);

	section(p, at[BOARD].top, at[BOARD].height, "THE BOARD", yellow);
	int y = at[BOARD].content;
	text(p, y, "MODE", grey);
	right_text(p, y + 10, info->board_mode ? "BOARD" : "MAME 1:1", info->board_mode ? green : cyan);
	y += 30;
	text(p, y, "FPS", grey);
	snprintf(line, sizeof(line), "%.2f", info->fps);
	right_text(p, y, line, white);

	section(p, at[SLOWDOWN].top, at[SLOWDOWN].height, "SLOWDOWN", yellow);
	if (info->recent_lost)
		right_text(p, at[SLOWDOWN].top + 7, "NOW", red);
	y = at[SLOWDOWN].content;
	text(p, y, "LOST", grey);
	snprintf(line, sizeof(line), "%u", info->lost_frames);
	right_text(p, y, line, white);
	text(p, y + LINE, "LAST SEC", grey);
	snprintf(line, sizeof(line), "%u", info->recent_lost);
	right_text(p, y + LINE, line, info->recent_lost ? red : white);
	/* the last second, a column a frame: the lost ones red */
	const int graph_y = y + 2 * LINE + 1, graph_w = HUD_W - 2 * PAD;
	box(p, PAD, graph_y + 13, graph_w, 1, unlit);
	for (int i = 0; i < 60; i++)
	{
		const int x0 = PAD + graph_w * i / 60, x1 = PAD + graph_w * (i + 1) / 60;
		if (info->lost_last[i])
			box(p, x0, graph_y, x1 - x0, 13, red);
	}

	/* (the palette's flashes: a check of board mode's, shown with panels_debug only) */
	if (info->debug)
	{
		section(p, at[PALETTE].top, at[PALETTE].height, "PALETTE", yellow);
		y = at[PALETTE].content;
		text(p, y, "FLASHES", grey);
		if (info->board_mode)
		{
			snprintf(line, sizeof(line), "%u", info->flash_frames);
			right_text(p, y, line, white);
		}
		else
		{
			right_text(p, y, "-", grey);
			text(p, y + LINE, "BOARD ONLY", unlit);
		}
	}

	section(p, at[STATUS].top, at[STATUS].height, "STATUS", yellow);
	y = at[STATUS].content;
	text(p, y, info->paused ? "PAUSED" : info->replay ? "REPLAY" : "RUNNING",
		info->paused ? yellow : info->replay ? cyan : green);
	text(p, y + LINE, "STATE SLOT", grey);
	snprintf(line, sizeof(line), "%d", info->state_slot);
	right_text(p, y + LINE, line, white);
	if (info->recording)
		text(p, y + 2 * LINE, "RECORDING", red);

	/* the window's keys, as makaimura.ini has them */
	static const char *const names[HUD_HOTKEYS] = { "MENU", "PAUSE", "SAVE", "LOAD", "SLOT", "RESET", "REC" };
	section(p, at[KEYS].top, at[KEYS].height, "KEYS", yellow);
	y = at[KEYS].content;
	for (int i = 0; i < HUD_HOTKEYS; i++, y += 12)
	{
		text(p, y, names[i], grey);
		right_text(p, y, info->hot_keys[i][0] ? info->hot_keys[i] : "-", white);
	}
}

void hud_draw(uint32_t *left, uint32_t *right, const hud_info *info)
{
	for (int i = 0; i < HUD_W * HUD_H; i++)
		left[i] = right[i] = 0;
	left_panel(left, info);
	right_panel(right, info);
}
