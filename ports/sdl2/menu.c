/**
 *    Name:        menu.c
 *    Description: the window's menu (menu.h)
 *    Version:     0.1.0
 *    Created:     19-11-2026
 *
 *    license:BSD-3-Clause
 *    The letters are the game's own: its text layer's characters (gng/video.h's gng_video_char_pen), whose codes
 *    are ASCII's (upper and lower case, digits, punctuation); pen 0 the letter, 1 and 2 its shading, 3 clear.
 *    32 columns of 8 pixels, 28 rows. Two pages: the main one, and the controls (each action and the window's own
 *    keys: Enter, then the key or the pad's button that takes it; Delete gives it its defaults back).
 */
#include "menu.h"

#include "gng/video.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

enum { HEADER, BLANK, DIP, ACCURACY, ASPECT, SCALING, SMOOTH, FULLSCREEN, VOLUME, CONTROLS, RESUME, RESET, QUIT,
	ACTION, HOTKEY, DEFAULTS, BACK };

typedef struct item { int kind; int dip; const char *label; } item;

static item main_items[40], control_items[40];
static int main_count, control_count;
static item *items = main_items;
static int item_count;
static int selected, main_selected;
static int capturing;                 /* the selected control waits for its key or button */

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

static const char *const action_labels[ACTIONS] = {
	"UP", "DOWN", "LEFT", "RIGHT", "ATTACK", "JUMP", "START 1P", "START 2P", "COIN 1", "COIN 2", "SERVICE",
	"UP", "DOWN", "LEFT", "RIGHT", "ATTACK", "JUMP" };
static const char *const hotkey_labels[HOTKEYS] = { "RESET", "PAUSE", "FULL SCREEN", "QUIT", "MENU", "RECORD" };

static void build_controls(void)
{
	items = control_items;
	item_count = 0;
	add(HEADER, 0, "CONTROLS");
	for (int a = 0; a < ACTION_P2_FIRST; a++)
		add(ACTION, a, action_labels[a]);
	add(HEADER, 0, "PLAYER 2: 2ND PAD");
	for (int a = ACTION_P2_FIRST; a < ACTIONS; a++)
		add(ACTION, a, action_labels[a]);
	add(HEADER, 0, "THE WINDOW'S KEYS");
	for (int h = 0; h < HOTKEYS; h++)
		add(HOTKEY, h, hotkey_labels[h]);
	add(DEFAULTS, 0, "ALL TO DEFAULTS");
	add(BACK, 0, "BACK");
	control_count = item_count;
}

static int selectable(int i)
{
	return items[i].kind != HEADER && items[i].kind != BLANK;
}

static void build(void)
{
	static char dip_labels[16][24];
	items = main_items;
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
	add(CONTROLS, 0, "CONTROLS");
	add(BLANK, 0, "");
	add(RESUME, 0, "RESUME");
	add(RESET, 0, "RESET THE BOARD");
	add(QUIT, 0, "QUIT");
	main_count = item_count;
}

static void show_page(item *page, int count, int first)
{
	items = page;
	item_count = count;
	selected = first;
	while (!selectable(selected))
		selected++;
	capturing = 0;
}

void menu_open(void)
{
	if (!main_count)
	{
		build_controls();
		build();
	}
	show_page(main_items, main_count, 0);
}

/* ---- the controls ---- */

static void clear_key(SDL_Scancode *slot, SDL_Scancode code)
{
	for (int i = 0; i < BINDINGS_MAX; i++)
		if (slot[i] == code)
		{
			for (int j = i; j < BINDINGS_MAX - 1; j++)
				slot[j] = slot[j + 1];
			slot[BINDINGS_MAX - 1] = SDL_SCANCODE_UNKNOWN;
			i--;
		}
}

static void clear_pad(pad_input *slot, pad_input input)
{
	for (int i = 0; i < BINDINGS_MAX; i++)
		if (slot[i].kind == input.kind && slot[i].index == input.index)
		{
			for (int j = i; j < BINDINGS_MAX - 1; j++)
				slot[j] = slot[j + 1];
			slot[BINDINGS_MAX - 1].kind = PAD_NONE;
			slot[BINDINGS_MAX - 1].index = 0;
			i--;
		}
}

/* The key now the selected control's only key, taken from every other control (one key, one thing) */
static void bind_key(settings *s, SDL_Scancode code)
{
	for (int a = 0; a < ACTIONS; a++)
		clear_key(s->keys[a], code);
	for (int h = 0; h < HOTKEYS; h++)
		clear_key(s->hot_keys[h], code);
	SDL_Scancode *slot = items[selected].kind == ACTION ? s->keys[items[selected].dip] :
		s->hot_keys[items[selected].dip];
	for (int i = 0; i < BINDINGS_MAX; i++)
		slot[i] = SDL_SCANCODE_UNKNOWN;
	slot[0] = code;
}

/* The pad's button or trigger now the selected action's only one (the menu's own: its button), taken from the
   others of the same gamepad (player 2's actions are the second one's) */
static void bind_pad(settings *s, pad_input input)
{
	const int second = items[selected].kind == ACTION && items[selected].dip >= ACTION_P2_FIRST;
	for (int a = 0; a < ACTIONS; a++)
		if ((a >= ACTION_P2_FIRST) == second)
			clear_pad(s->buttons[a], input);
	if (!second)
		clear_pad(s->menu_buttons, input);
	pad_input *slot = NULL;
	if (items[selected].kind == ACTION)
		slot = s->buttons[items[selected].dip];
	else if (items[selected].dip == HOTKEY_MENU)
		slot = s->menu_buttons;
	if (!slot)
		return;    /* (the other window keys have no buttons) */
	for (int i = 0; i < BINDINGS_MAX; i++)
		slot[i].kind = PAD_NONE, slot[i].index = 0;
	slot[0] = input;
}

/* The selected control's defaults back (settings.c's) */
static void control_defaults(settings *s, const item *it)
{
	settings defaults;
	settings_defaults(&defaults);
	if (it->kind == ACTION)
	{
		memcpy(s->keys[it->dip], defaults.keys[it->dip], sizeof(s->keys[0]));
		memcpy(s->buttons[it->dip], defaults.buttons[it->dip], sizeof(s->buttons[0]));
	}
	else if (it->kind == HOTKEY)
	{
		memcpy(s->hot_keys[it->dip], defaults.hot_keys[it->dip], sizeof(s->hot_keys[0]));
		if (it->dip == HOTKEY_MENU)
			memcpy(s->menu_buttons, defaults.menu_buttons, sizeof(s->menu_buttons));
	}
	else if (it->kind == DEFAULTS)
	{
		memcpy(s->keys, defaults.keys, sizeof(s->keys));
		memcpy(s->buttons, defaults.buttons, sizeof(s->buttons));
		memcpy(s->hot_keys, defaults.hot_keys, sizeof(s->hot_keys));
		memcpy(s->menu_buttons, defaults.menu_buttons, sizeof(s->menu_buttons));
	}
}

/* An event while a control waits: its key, button or trigger (Escape: left as it was) */
static void capture(const SDL_Event *event, settings *s)
{
	if (event->type == SDL_KEYDOWN)
	{
		if (event->key.repeat)
			return;
		if (event->key.keysym.scancode != SDL_SCANCODE_ESCAPE)
			bind_key(s, event->key.keysym.scancode);
		capturing = 0;
	}
	else if (event->type == SDL_CONTROLLERBUTTONDOWN)
	{
		bind_pad(s, (pad_input){ PAD_BUTTON, event->cbutton.button });
		capturing = 0;
	}
	else if (event->type == SDL_CONTROLLERAXISMOTION && event->caxis.value > 16384 &&
		(event->caxis.axis == SDL_CONTROLLER_AXIS_TRIGGERLEFT || event->caxis.axis == SDL_CONTROLLER_AXIS_TRIGGERRIGHT))
	{
		bind_pad(s, (pad_input){ PAD_TRIGGER, event->caxis.axis });
		capturing = 0;
	}
}

static void step(settings *s, int direction, int *changed)
{
	const item *it = &items[selected];
	switch (it->kind)
	{
	case DIP:
		settings_dip_step(s, it->dip, direction);
		break;
	case ACCURACY:
		s->accuracy = !s->accuracy;
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
	if (capturing)
	{
		capture(event, s);
		return MENU_STAY;
	}
	int up = 0, down = 0, left = 0, right = 0, accept = 0, back = 0, erase = 0;
	if (event->type == SDL_KEYDOWN)
	{
		switch (event->key.keysym.scancode)
		{
		case SDL_SCANCODE_DELETE: erase = 1; break;
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
	if (back && items == control_items)
	{
		show_page(main_items, main_count, main_selected);
		return MENU_STAY;
	}
	if (back)
		return MENU_RESUME;
	if (erase)
		control_defaults(s, &items[selected]);
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
		case CONTROLS:
			main_selected = selected;
			show_page(control_items, control_count, 0);
			break;
		case ACTION: case HOTKEY: capturing = 1; break;
		case DEFAULTS: control_defaults(s, &items[selected]); break;
		case BACK: show_page(main_items, main_count, main_selected); break;
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

/* a pad's input, short: "A", "LB", "LT", "D UP" */
static const char *pad_name(pad_input input, char *buffer, size_t size)
{
	static const struct { const char *sdl, *shown; } names[] = {
		{ "leftshoulder", "LB" }, { "rightshoulder", "RB" }, { "lefttrigger", "LT" }, { "righttrigger", "RT" },
		{ "leftstick", "LS" }, { "rightstick", "RS" }, { "dpup", "D UP" }, { "dpdown", "D DOWN" },
		{ "dpleft", "D LEFT" }, { "dpright", "D RIGHT" } };
	const char *name = input.kind == PAD_BUTTON ?
		SDL_GameControllerGetStringForButton((SDL_GameControllerButton)input.index) :
		SDL_GameControllerGetStringForAxis((SDL_GameControllerAxis)input.index);
	if (!name)
		return "?";
	for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); i++)
		if (!strcmp(name, names[i].sdl))
			return names[i].shown;
	upper(buffer, size, name);
	return buffer;
}

/* "Z, LEFT CTRL / B, X": the keys, then the pad's */
static const char *bindings(const SDL_Scancode *keys, const pad_input *pad, char *buffer, size_t size)
{
	char name[32];
	size_t used = 0;
	buffer[0] = 0;
	for (int i = 0; i < BINDINGS_MAX && keys[i] != SDL_SCANCODE_UNKNOWN; i++)
	{
		upper(name, sizeof(name), SDL_GetScancodeName(keys[i]));
		used += (size_t)snprintf(buffer + used, size - used, "%s%s", i ? ", " : "", name);
		if (used >= size) return buffer;
	}
	for (int i = 0; pad && i < BINDINGS_MAX && pad[i].kind != PAD_NONE; i++)
	{
		used += (size_t)snprintf(buffer + used, size - used, "%s%s", i ? ", " : used ? " / " : "",
			pad_name(pad[i], name, sizeof(name)));
		if (used >= size) return buffer;
	}
	if (!buffer[0])
		snprintf(buffer, size, "NONE");
	return buffer;
}

static const char *value_of(const item *it, const settings *s, char *buffer, size_t size)
{
	switch (it->kind)
	{
	case ACTION:
		return bindings(s->keys[it->dip], s->buttons[it->dip], buffer, size);
	case HOTKEY:
		return bindings(s->hot_keys[it->dip], it->dip == HOTKEY_MENU ? s->menu_buttons : NULL, buffer, size);
	case DIP: upper(buffer, size, settings_dip_value(s, it->dip)); return buffer;
	case ACCURACY: return s->accuracy ? "BOARD" : "MAME 1:1";
	case ASPECT: return s->aspect == ASPECT_MONITOR ? "4:3 MONITOR" : "SQUARE PIXELS";
	case SCALING: upper(buffer, size, settings_scaling_names[s->scaling]); return buffer;
	case SMOOTH: return s->smooth ? "SMOOTH" : "SHARP";
	case FULLSCREEN: return s->fullscreen ? "ON" : "OFF";
	case VOLUME: snprintf(buffer, size, "%d", s->volume); return buffer;
	case CONTROLS: return "ENTER";
	default: return "";
	}
}

static const char *hint_of(const item *it)
{
	switch (it->kind)
	{
	case DIP: return "READ AS THE BOARD STARTS: F3";
	case ACCURACY: return "FROM THE BOARD'S START: F3";
	case ASPECT: return "AS THE ARCADE'S MONITOR, OR 1:1";
	case RESET: return "THE BOARD'S POWER CYCLED";
	case ACTION: case HOTKEY: return "ENTER: CHANGE  DELETE: DEFAULT";
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
	char buffer[64];
	/* rows 2-26: a longer page scrolls with the selection */
	const int rows = 25;
	int first = 0;
	if (item_count > rows)
	{
		first = selected - rows / 2;
		first = first < 0 ? 0 : first > item_count - rows ? item_count - rows : first;
	}
	for (int i = first; i < item_count && row < 27; i++, row++)
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
		const char *value = i == selected && capturing ? "PRESS A KEY" : value_of(it, s, buffer, sizeof(buffer));
		/* a value too long for its column starts earlier, over the label's spaces (and ends at the screen's) */
		const int room = COLUMNS - 1 - (int)strlen(it->label) - 1;
		if ((int)strlen(value) > room && value == buffer)
			buffer[room] = 0;
		const int length = (int)strlen(value);
		const int column = length > COLUMNS - VALUE_COLUMN ? COLUMNS - length : VALUE_COLUMN;
		put_text(pixels, column, row, value, i == selected ? yellow : cyan);
	}
	const char *hint = capturing ? "OR A BUTTON. ESC: AS IT WAS" : hint_of(&items[selected]);
	put_text(pixels, (COLUMNS - (int)strlen(hint)) / 2, 27, hint, grey);
}
