/**
 *    Name:        menu.c
 *    Description: the window's menu (menu.h)
 *    Version:     0.1.0
 *    Created:     19-11-2026
 *
 *    license:BSD-3-Clause
 *    The letters are the game's own: its text layer's characters (gng/video.h's gng_video_char_pen), whose codes
 *    are ASCII's (upper and lower case, digits, punctuation); pen 0 the letter, 1 and 2 its shading, 3 clear.
 *    32 columns of 8 pixels, 28 rows.
 */
#include "menu.h"

#include "gng/video.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

enum { HEADER, BLANK, DIP, ACCURACY, ASPECT, SCALING, SMOOTH, FULLSCREEN, VOLUME, RESUME, RESET, QUIT };

typedef struct item { int kind; int dip; const char *label; } item;

static item items[40];
static int item_count;
static int selected;

#define COLUMNS 32
#define VALUE_COLUMN 14

static const uint32_t white = 0xF0F0F0, yellow = 0xF8D040, cyan = 0x60D8F0, red = 0xF06070, grey = 0x9098A0,
	bar = 0x203070;

static void add(int kind, int dip, const char *label)
{
	items[item_count].kind = kind;
	items[item_count].dip = dip;
	items[item_count].label = label;
	item_count++;
}

static int selectable(int i)
{
	return items[i].kind != HEADER && items[i].kind != BLANK;
}

static void build(void)
{
	static char dip_labels[16][24];
	item_count = 0;
	add(HEADER, 0, "GAME");
	for (int d = 0; d < settings_dip_count() && d < 16; d++)
	{
		/* "bonus_life" as "BONUS LIFE" */
		const char *name = settings_dip_name(d);
		size_t i = 0;
		for (; name[i] && i < sizeof(dip_labels[d]) - 1; i++)
			dip_labels[d][i] = name[i] == '_' ? ' ' : (char)toupper((unsigned char)name[i]);
		dip_labels[d][i] = 0;
		/* (the labels end before the values' column) */
		add(DIP, d, !strcmp(name, "coinage_affects") ? "COINAGE FOR" : dip_labels[d]);
	}
	add(ACCURACY, 0, "ACCURACY");
	add(BLANK, 0, "");
	add(HEADER, 0, "PICTURE AND SOUND");
	add(ASPECT, 0, "SHAPE");
	add(SCALING, 0, "SCALING");
	add(SMOOTH, 0, "PIXELS");
	add(FULLSCREEN, 0, "FULL SCREEN");
	add(VOLUME, 0, "VOLUME");
	add(BLANK, 0, "");
	add(RESUME, 0, "RESUME");
	add(RESET, 0, "RESET THE BOARD");
	add(QUIT, 0, "QUIT");
}

void menu_open(void)
{
	if (!item_count)
		build();
	selected = 0;
	while (!selectable(selected))
		selected++;
}

static void step(settings *s, int direction, int *changed)
{
	const item *it = &items[selected];
	switch (it->kind)
	{
	case DIP:
		settings_dip_step(s, it->dip, direction);
		break;
	case ASPECT:
		s->aspect = (s->aspect + direction + ASPECTS) % ASPECTS;
		break;
	case SCALING:
		s->scaling = (s->scaling + direction + SCALINGS) % SCALINGS;
		break;
	case SMOOTH:
		s->smooth = !s->smooth;
		break;
	case FULLSCREEN:
		*changed |= MENU_CHANGED_FULLSCREEN;
		break;
	case VOLUME:
		s->volume += direction * 10;
		s->volume = s->volume < 0 ? 0 : s->volume > 100 ? 100 : s->volume;
		*changed |= MENU_CHANGED_VOLUME;
		break;
	default:
		break;
	}
}

static void move(int direction)
{
	do
		selected = (selected + direction + item_count) % item_count;
	while (!selectable(selected));
}

int menu_event(const SDL_Event *event, settings *s, int *changed)
{
	*changed = MENU_CHANGED_NONE;
	int up = 0, down = 0, left = 0, right = 0, accept = 0, back = 0;
	if (event->type == SDL_KEYDOWN)
	{
		switch (event->key.keysym.scancode)
		{
		case SDL_SCANCODE_UP: up = 1; break;
		case SDL_SCANCODE_DOWN: down = 1; break;
		case SDL_SCANCODE_LEFT: left = 1; break;
		case SDL_SCANCODE_RIGHT: right = 1; break;
		case SDL_SCANCODE_RETURN: case SDL_SCANCODE_KP_ENTER: case SDL_SCANCODE_SPACE: accept = 1; break;
		case SDL_SCANCODE_BACKSPACE: back = 1; break;
		default:
			for (int i = 0; i < BINDINGS_MAX; i++)
				if (s->hot_keys[HOTKEY_MENU][i] == event->key.keysym.scancode && event->key.keysym.scancode)
					back = 1;
			break;
		}
	}
	else if (event->type == SDL_CONTROLLERBUTTONDOWN)
	{
		switch (event->cbutton.button)
		{
		case SDL_CONTROLLER_BUTTON_DPAD_UP: up = 1; break;
		case SDL_CONTROLLER_BUTTON_DPAD_DOWN: down = 1; break;
		case SDL_CONTROLLER_BUTTON_DPAD_LEFT: left = 1; break;
		case SDL_CONTROLLER_BUTTON_DPAD_RIGHT: right = 1; break;
		case SDL_CONTROLLER_BUTTON_A: accept = 1; break;
		case SDL_CONTROLLER_BUTTON_B: case SDL_CONTROLLER_BUTTON_START: back = 1; break;
		default:
			for (int i = 0; i < BINDINGS_MAX; i++)
				if (s->menu_buttons[i].kind == PAD_BUTTON && s->menu_buttons[i].index == event->cbutton.button)
					back = 1;
			break;
		}
	}
	if (back)
		return MENU_RESUME;
	if (up)
		move(-1);
	if (down)
		move(1);
	if (left)
		step(s, -1, changed);
	if (right)
		step(s, 1, changed);
	if (accept)
	{
		switch (items[selected].kind)
		{
		case RESUME: return MENU_RESUME;
		case RESET: return MENU_RESET;
		case QUIT: return MENU_QUIT;
		default: step(s, 1, changed); break;
		}
	}
	return MENU_STAY;
}

/* ---- drawing ---- */

static void put_char(uint32_t *pixels, int column, int row, char c, uint32_t colour)
{
	for (int y = 0; y < 8; y++)
		for (int x = 0; x < 8; x++)
		{
			const int pen = gng_video_char_pen((unsigned char)c, x, y);
			if (pen == 3)
				continue;
			/* pen 0 the letter, 1 and 2 its shading */
			const int level = pen == 0 ? 256 : pen == 1 ? 170 : 110;
			const uint32_t r = ((colour >> 16) & 0xFF) * level >> 8, g = ((colour >> 8) & 0xFF) * level >> 8,
				b = (colour & 0xFF) * level >> 8;
			pixels[(row * 8 + y) * GNG_SCREEN_WIDTH + column * 8 + x] = r << 16 | g << 8 | b;
		}
}

static void put_text(uint32_t *pixels, int column, int row, const char *text, uint32_t colour)
{
	for (; *text && column < COLUMNS; text++, column++)
		if (*text != ' ')
			put_char(pixels, column, row, *text, colour);
}

static void fill_row(uint32_t *pixels, int row, uint32_t colour)
{
	for (int y = row * 8; y < row * 8 + 8; y++)
		for (int x = 0; x < GNG_SCREEN_WIDTH; x++)
			pixels[y * GNG_SCREEN_WIDTH + x] = colour;
}

static void upper(char *out, size_t size, const char *in)
{
	size_t i = 0;
	for (; in[i] && i < size - 1; i++)
		out[i] = (char)toupper((unsigned char)in[i]);
	out[i] = 0;
}

static const char *value_of(const item *it, const settings *s, char *buffer, size_t size)
{
	switch (it->kind)
	{
	case DIP: upper(buffer, size, settings_dip_value(s, it->dip)); return buffer;
	case ACCURACY: return "MAME 1:1";
	case ASPECT: return s->aspect == ASPECT_MONITOR ? "4:3 MONITOR" : "SQUARE PIXELS";
	case SCALING: upper(buffer, size, settings_scaling_names[s->scaling]); return buffer;
	case SMOOTH: return s->smooth ? "SMOOTH" : "SHARP";
	case FULLSCREEN: return s->fullscreen ? "ON" : "OFF";
	case VOLUME: snprintf(buffer, size, "%d", s->volume); return buffer;
	default: return "";
	}
}

static const char *hint_of(const item *it)
{
	switch (it->kind)
	{
	case DIP: return "READ AS THE BOARD STARTS: F3";
	case ACCURACY: return "BOARD MODE: NOT READY YET";
	case ASPECT: return "AS THE ARCADE'S MONITOR, OR 1:1";
	case RESET: return "THE BOARD'S POWER CYCLED";
	default: return "";
	}
}

void menu_draw(uint32_t *pixels, const settings *s)
{
	/* the game behind, dark */
	for (int i = 0; i < GNG_SCREEN_WIDTH * GNG_SCREEN_HEIGHT; i++)
		pixels[i] = (pixels[i] >> 4) & 0x0F0F0F;
	put_text(pixels, 8, 0, "MAKAIMURA RECOMP", yellow);
	int row = 2;
	char buffer[40];
	for (int i = 0; i < item_count && row < 27; i++, row++)
	{
		const item *it = &items[i];
		if (it->kind == BLANK)
			continue;
		if (it->kind == HEADER)
		{
			put_text(pixels, 0, row, it->label, red);
			continue;
		}
		if (i == selected)
			fill_row(pixels, row, bar);
		put_text(pixels, 1, row, it->label, i == selected ? yellow : white);
		const char *value = value_of(it, s, buffer, sizeof(buffer));
		/* a value too long for its column starts earlier, over the label's spaces */
		const int length = (int)strlen(value);
		const int column = length > COLUMNS - VALUE_COLUMN ? COLUMNS - length : VALUE_COLUMN;
		put_text(pixels, column, row, value, i == selected ? yellow : cyan);
	}
	const char *hint = hint_of(&items[selected]);
	put_text(pixels, (COLUMNS - (int)strlen(hint)) / 2, 27, hint, grey);
}
