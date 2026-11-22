/**
 *    Name:        menu.h
 *    Description: the window's menu: the game's options (the DIP switches), the accuracy mode, the picture, the
 *                 sound, the controls (each key and button changed), the board's reset, quitting; drawn over the
 *                 paused game with the game's own font
 *    Version:     0.1.0
 *    Created:     19-11-2026
 *
 *    license:BSD-3-Clause
 */
#ifndef MAKAIMURA_MENU_H
#define MAKAIMURA_MENU_H

#include "settings.h"

#include <SDL.h>
#include <stdint.h>

enum { MENU_STAY, MENU_RESUME, MENU_RESET, MENU_QUIT, MENU_SAVE_STATE, MENU_LOAD_STATE, MENU_PRACTICE,
	MENU_REPLAY, MENU_DELETE_REPLAY, MENU_KEEP_REPLAY, MENU_DROP_REPLAY };

/* The REPLAYS page's entries (main.c, as the menu opens): each one's label and value, newest first */
#define MENU_REPLAYS 20
void menu_replays(int count, const char labels[][16], const char values[][16]);

/* After MENU_REPLAY or MENU_DELETE_REPLAY: the entry chosen (0 the first) */
int menu_replay_chosen(void);

/* The REPLAYS page again (after menu_replays: an entry deleted), the selection near 'entry' */
void menu_show_replays(int entry);

/* A game over: the menu opened on the question whether to keep its replay (MENU_KEEP_REPLAY, MENU_DROP_REPLAY) */
void menu_ask_keep_replay(void);

/* The practice's areas (the stages 0-6, <$72: area 7 the last one's room, which asks for the cross) */
#define PRACTICE_AREAS 7

/* The practice's weapons (<$73's values: 0 the torch, 1 the lance, 2 the axe, 3 the cross, 4 the dagger) */
#define PRACTICE_WEAPONS 5

/* The practice's lives: 0 the game options', or 1-PRACTICE_MOST_LIVES (<$60: Arthur's, the reserve's one fewer) */
#define PRACTICE_MOST_LIVES 9

/* A practice game: where it starts and what the player has there */
typedef struct practice_setup
{
	int area;       /* 0 to PRACTICE_AREAS - 1 (the stage, <$72) */
	int loop;       /* 0 the first, 1 the second (<$7F) */
	int weapon;     /* <$73 */
	int lives;      /* 0 the game options', else <$60 */
} practice_setup;

/* What a choice changed that the window applies at once (main.c) */
enum { MENU_CHANGED_NONE = 0, MENU_CHANGED_FULLSCREEN = 1, MENU_CHANGED_VOLUME = 2 };

void menu_open(void);

/* A key, a gamepad button or trigger (SDL_KEYDOWN, SDL_CONTROLLERBUTTONDOWN, SDL_CONTROLLERAXISMOTION) on the open
   menu: MENU_STAY or what to do; *changed says what of 's' changed that needs applying */
int menu_event(const SDL_Event *event, settings *s, int *changed);

/* The menu over the game's picture (256 x 224, 0x00RRGGBB), the picture darkened behind it */
void menu_draw(uint32_t *pixels, const settings *s);

/* The selected entry's name and what it does, for the side panel */
void menu_help(const char **title, const char **text);

/* After MENU_PRACTICE: the practice chosen */
practice_setup menu_practice_setup(void);

/* A weapon's name ("LANCE"), by its value in <$73 */
const char *menu_weapon_name(int weapon);

/* A short notice over the picture's bottom row ("STATE 1 SAVED"), in the menu's letters */
void menu_draw_notice(uint32_t *pixels, const char *text);

#endif
