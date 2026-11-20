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
	MENU_REPLAY };

/* The REPLAYS page's entries (main.c, as the menu opens): each one's label and value, newest first */
#define MENU_REPLAYS 20
void menu_replays(int count, const char labels[][16], const char values[][16]);

/* After MENU_REPLAY: the entry chosen (0 the first) */
int menu_replay_chosen(void);

/* The practice's areas (the first loop's: the stages 0-5, <$72) */
#define PRACTICE_AREAS 6

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

/* After MENU_PRACTICE: the area chosen (0 the first) */
int menu_practice_area(void);

/* A short notice over the picture's bottom row ("STATE 1 SAVED"), in the menu's letters */
void menu_draw_notice(uint32_t *pixels, const char *text);

#endif
