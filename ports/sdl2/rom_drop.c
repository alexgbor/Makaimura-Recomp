/**
 *    Name:        rom_drop.c
 *    Description: the window that asks for the ROM set (rom_drop.h)
 *    Version:     0.1.0
 *    Created:     20-11-2026
 *
 *    license:BSD-3-Clause
 *    After DonPachi Recomp's ports/sdl2/rom_drop.c: its window, its 5 x 7 font (capitals, digits, a few signs).
 */
#include "rom_drop.h"

#include <SDL.h>
#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
#endif

#define WINDOW_W 640
#define WINDOW_H 400

#define BACKGROUND 0x100C14u
#define GOLD       0xF0C040u
#define WHITE      0xF0F0F0u
#define GREY       0x9090A8u
#define RED        0xFF4848u
#define GREEN      0x60E070u

typedef struct
{
	char ch;
	const char *rows[7];
} glyph;

static const glyph font[] = {
	{ 'A', { ".###.", "#...#", "#...#", "#####", "#...#", "#...#", "#...#" } },
	{ 'B', { "####.", "#...#", "#...#", "####.", "#...#", "#...#", "####." } },
	{ 'C', { ".###.", "#...#", "#....", "#....", "#....", "#...#", ".###." } },
	{ 'D', { "####.", "#...#", "#...#", "#...#", "#...#", "#...#", "####." } },
	{ 'E', { "#####", "#....", "#....", "####.", "#....", "#....", "#####" } },
	{ 'F', { "#####", "#....", "#....", "####.", "#....", "#....", "#...." } },
	{ 'G', { ".###.", "#...#", "#....", "#.###", "#...#", "#...#", ".####" } },
	{ 'H', { "#...#", "#...#", "#...#", "#####", "#...#", "#...#", "#...#" } },
	{ 'I', { ".###.", "..#..", "..#..", "..#..", "..#..", "..#..", ".###." } },
	{ 'J', { "..###", "...#.", "...#.", "...#.", "...#.", "#..#.", ".##.." } },
	{ 'K', { "#...#", "#..#.", "#.#..", "##...", "#.#..", "#..#.", "#...#" } },
	{ 'L', { "#....", "#....", "#....", "#....", "#....", "#....", "#####" } },
	{ 'M', { "#...#", "##.##", "#.#.#", "#.#.#", "#...#", "#...#", "#...#" } },
	{ 'N', { "#...#", "#...#", "##..#", "#.#.#", "#..##", "#...#", "#...#" } },
	{ 'O', { ".###.", "#...#", "#...#", "#...#", "#...#", "#...#", ".###." } },
	{ 'P', { "####.", "#...#", "#...#", "####.", "#....", "#....", "#...." } },
	{ 'Q', { ".###.", "#...#", "#...#", "#...#", "#.#.#", "#..#.", ".##.#" } },
	{ 'R', { "####.", "#...#", "#...#", "####.", "#.#..", "#..#.", "#...#" } },
	{ 'S', { ".####", "#....", "#....", ".###.", "....#", "....#", "####." } },
	{ 'T', { "#####", "..#..", "..#..", "..#..", "..#..", "..#..", "..#.." } },
	{ 'U', { "#...#", "#...#", "#...#", "#...#", "#...#", "#...#", ".###." } },
	{ 'V', { "#...#", "#...#", "#...#", "#...#", "#...#", ".#.#.", "..#.." } },
	{ 'W', { "#...#", "#...#", "#...#", "#.#.#", "#.#.#", "#.#.#", ".#.#." } },
	{ 'X', { "#...#", "#...#", ".#.#.", "..#..", ".#.#.", "#...#", "#...#" } },
	{ 'Y', { "#...#", "#...#", ".#.#.", "..#..", "..#..", "..#..", "..#.." } },
	{ 'Z', { "#####", "....#", "...#.", "..#..", ".#...", "#....", "#####" } },
	{ '0', { ".###.", "#...#", "#..##", "#.#.#", "##..#", "#...#", ".###." } },
	{ '1', { "..#..", ".##..", "..#..", "..#..", "..#..", "..#..", ".###." } },
	{ '2', { ".###.", "#...#", "....#", "...#.", "..#..", ".#...", "#####" } },
	{ '3', { "#####", "...#.", "..#..", "...#.", "....#", "#...#", ".###." } },
	{ '4', { "...#.", "..##.", ".#.#.", "#..#.", "#####", "...#.", "...#." } },
	{ '5', { "#####", "#....", "####.", "....#", "....#", "#...#", ".###." } },
	{ '6', { "..##.", ".#...", "#....", "####.", "#...#", "#...#", ".###." } },
	{ '7', { "#####", "....#", "...#.", "..#..", ".#...", ".#...", ".#..." } },
	{ '8', { ".###.", "#...#", "#...#", ".###.", "#...#", "#...#", ".###." } },
	{ '9', { ".###.", "#...#", "#...#", ".####", "....#", "...#.", ".##.." } },
	{ '.', { ".....", ".....", ".....", ".....", ".....", ".##..", ".##.." } },
	{ ',', { ".....", ".....", ".....", ".....", ".##..", "..#..", ".#..." } },
	{ ':', { ".....", ".##..", ".##..", ".....", ".##..", ".##..", "....." } },
	{ '\'', { "..#..", "..#..", ".#...", ".....", ".....", ".....", "....." } },
	{ '-', { ".....", ".....", ".....", "#####", ".....", ".....", "....." } },
	{ '(', { "...#.", "..#..", ".#...", ".#...", ".#...", "..#..", "...#." } },
	{ ')', { ".#...", "..#..", "...#.", "...#.", "...#.", "..#..", ".#..." } },
	{ '/', { "....#", "....#", "...#.", "..#..", ".#...", "#....", "#...." } },
	{ '!', { "..#..", "..#..", "..#..", "..#..", "..#..", ".....", "..#.." } },
	{ '?', { ".###.", "#...#", "....#", "...#.", "..#..", ".....", "..#.." } },
	{ '_', { ".....", ".....", ".....", ".....", ".....", ".....", "#####" } },
};

static const glyph *glyph_of(char ch)
{
	ch = (char)toupper((unsigned char)ch);
	for (size_t i = 0; i < sizeof(font) / sizeof(font[0]); i++)
		if (font[i].ch == ch)
			return &font[i];
	return NULL;    /* (a space, or a sign it has not: left blank) */
}

static void set_colour(SDL_Renderer *renderer, uint32_t colour)
{
	SDL_SetRenderDrawColor(renderer, colour >> 16 & 255u, colour >> 8 & 255u, colour & 255u, 255);
}

/* 'text' centred at 'y', each of its pixels 'scale' x 'scale' (a character 6 pixels wide, with its space); cut
   with "..." to what fits */
static void text_centred(SDL_Renderer *renderer, int y, const char *text, int scale, uint32_t colour)
{
	char line[160];
	const size_t most = (size_t)((WINDOW_W - 32) / (6 * scale));
	if (strlen(text) > most && most >= 4 && most < sizeof(line))
	{
		memcpy(line, text, most - 3);
		memcpy(line + most - 3, "...", 4);
		text = line;
	}
	const int width = (int)strlen(text) * 6 * scale - scale;
	int x = (WINDOW_W - width) / 2;
	set_colour(renderer, colour);
	for (const char *at = text; *at; at++, x += 6 * scale)
	{
		const glyph *shape = glyph_of(*at);
		if (!shape)
			continue;
		for (int row = 0; row < 7; row++)
			for (int column = 0; column < 5; column++)
				if (shape->rows[row][column] == '#')
				{
					const SDL_Rect pixel = { x + column * scale, y + row * scale, scale, scale };
					SDL_RenderFillRect(renderer, &pixel);
				}
	}
}

/* what the set still lacks, as up to three lines ('\n' between): which of the two zips its files come from (the
   clone's program, mj*.bin; the parent's sound, graphics and PROMs, the rest) */
static void describe(const gng_rom_set *set, char *out, size_t size)
{
	char names[512];
	const int missing = gng_rom_set_missing(set, names, sizeof(names));
	int clone_files = 0;
	for (const char *at = names; (at = strstr(at, "mj0")) != NULL; at++)
		clone_files++;
	const int clone = clone_files > 0, parent = missing > clone_files;
	snprintf(out, size, "%d OF THE %d FILES FOUND.\nNOW DROP %s%s%s.\n%s", GNG_ROM_FILES - missing, GNG_ROM_FILES,
		clone ? "MAKAIMURG.ZIP" : "", clone && parent ? " AND " : "", parent ? "GNG.ZIP" : "", names);
}

static void draw(SDL_Renderer *renderer, const char *status, uint32_t status_colour)
{
	set_colour(renderer, BACKGROUND);
	SDL_RenderClear(renderer);
	text_centred(renderer, 54 + 4, "MAKAIMURA", 8, 0x401010u);    /* (its shadow) */
	text_centred(renderer, 54, "MAKAIMURA", 8, GOLD);
	text_centred(renderer, 150, "DROP THE ARCADE ROM SET HERE", 3, WHITE);
	text_centred(renderer, 190, "MAME'S MAKAIMURG.ZIP AND ITS PARENT GNG.ZIP", 2, GREY);
	text_centred(renderer, 212, "BOTH AT ONCE, OR ONE AFTER THE OTHER.", 2, GREY);
	if (status)
	{
		/* its lines: the first in the status's colour, the next white, the last (the files' names) small */
		char copy[800];
		int y = 254, line = 0;
		snprintf(copy, sizeof(copy), "%s", status);
		for (char *at = copy, *next; at && *at; at = next, line++)
		{
			if ((next = strchr(at, '\n')) != NULL)
				*next++ = '\0';
			const int last = next == NULL && line >= 2;
			text_centred(renderer, y, at, last ? 1 : 2, line == 0 ? status_colour : last ? GREY : WHITE);
			y += last ? 12 : 24;
		}
	}
	SDL_RenderPresent(renderer);
}

static int is_folder(const char *path)
{
	struct stat info;
	return stat(path, &info) == 0 && (info.st_mode & S_IFDIR);
}

static int copy_file(const char *from, const char *to)
{
	FILE *in = fopen(from, "rb"), *out = in ? fopen(to, "wb") : NULL;
	int good = in && out;
	char buffer[65536];
	size_t got;
	while (good && (got = fread(buffer, 1, sizeof(buffer), in)) > 0)
		good = fwrite(buffer, 1, got, out) == got;
	if (in)
		fclose(in);
	if (out)
		good &= fclose(out) == 0;
	return good;
}

/* the dropped .zips kept in the program's rom folder, by their own names */
static void keep_zips(char dropped[][1024], int count, const char *keep_folder)
{
#ifdef _WIN32
	_mkdir(keep_folder);
#else
	mkdir(keep_folder, 0777);
#endif
	for (int i = 0; i < count; i++)
	{
		if (is_folder(dropped[i]))
			continue;
		const char *slash = strrchr(dropped[i], '/'), *back = strrchr(dropped[i], '\\');
		const char *name = back > slash ? back + 1 : slash ? slash + 1 : dropped[i];
		char to[1400];
		snprintf(to, sizeof(to), "%s/%s", keep_folder, name);
		if (strcmp(to, dropped[i]) && !copy_file(dropped[i], to))
			fprintf(stderr, "makaimura: %s not copied into %s\n", dropped[i], keep_folder);
	}
}

static char check_drops[2048];
static const char *check_shot;
static int checking;

void rom_drop_check(const char *drops, const char *shot)
{
	snprintf(check_drops, sizeof(check_drops), "%s", drops ? drops : "");
	check_shot = shot;
	checking = 1;
}

/* the window's picture into a BMP */
static void shoot(SDL_Renderer *renderer, const char *path)
{
	SDL_Surface *shot = SDL_CreateRGBSurfaceWithFormat(0, WINDOW_W, WINDOW_H, 32, SDL_PIXELFORMAT_XRGB8888);
	if (shot && SDL_RenderReadPixels(renderer, NULL, SDL_PIXELFORMAT_XRGB8888, shot->pixels, shot->pitch) == 0)
		SDL_SaveBMP(shot, path);
	SDL_FreeSurface(shot);
}

bool rom_drop_screen(gng_rom_set *set, const char *keep_folder, char *folder, size_t folder_size)
{
	SDL_Window *window = SDL_CreateWindow("Makaimura: the ROM set", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
		WINDOW_W, WINDOW_H, checking ? SDL_WINDOW_HIDDEN : 0);
	char *next_drop = check_drops;
	if (!window)
		return false;
	SDL_Renderer *renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
	if (!renderer)
		renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
	if (!renderer)
	{
		SDL_DestroyWindow(window);
		return false;
	}
	static char dropped[16][1024];
	int dropped_count = 0;
	char status[800] = "";
	uint32_t status_colour = WHITE;
	bool whole = false, running = true;
	folder[0] = 0;
	SDL_EventState(SDL_DROPFILE, SDL_ENABLE);
	if (gng_rom_set_missing(set, NULL, 0) < GNG_ROM_FILES)
	{
		/* (some of it was found beside the program: what is left) */
		describe(set, status, sizeof(status));
		status_colour = GOLD;
	}
	while (running && !whole)
	{
		SDL_Event event;
		draw(renderer, status[0] ? status : NULL, status_colour);
		if (checking)
		{
			/* (a check: the next place dropped, or the picture and the end) */
			if (!*next_drop)
			{
				if (check_shot)
					shoot(renderer, check_shot);
				break;
			}
			char *comma = strchr(next_drop, ',');
			if (comma)
				*comma = 0;
			SDL_zero(event);
			event.type = SDL_DROPFILE;
			event.drop.file = SDL_strdup(next_drop);
			next_drop = comma ? comma + 1 : next_drop + strlen(next_drop);
		}
		else if (!SDL_WaitEventTimeout(&event, 250))
			continue;
		if (event.type == SDL_QUIT || (event.type == SDL_WINDOWEVENT && event.window.event == SDL_WINDOWEVENT_CLOSE) ||
			(event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_ESCAPE))
			running = false;
		else if (event.type == SDL_DROPFILE)
		{
			draw(renderer, "READING...", WHITE);
			const int before = gng_rom_set_missing(set, NULL, 0);
			gng_rom_set_add(set, event.drop.file);
			const int now = gng_rom_set_missing(set, NULL, 0);
			if (now < before && dropped_count < 16)
				snprintf(dropped[dropped_count++], sizeof(dropped[0]), "%s", event.drop.file);
			if (now == 0)
			{
				whole = true;
				keep_zips(dropped, dropped_count, keep_folder);
				for (int i = 0; i < dropped_count; i++)
					if (is_folder(dropped[i]))
						snprintf(folder, folder_size, "%s", dropped[i]);
				draw(renderer, "FOUND IT ALL! STARTING...", GREEN);
				if (checking && check_shot)
					shoot(renderer, check_shot);
				SDL_Delay(checking ? 0 : 700);
			}
			else if (now == before)
			{
				snprintf(status, sizeof(status), "THAT HAS NONE OF THE SET'S FILES.\n");
				describe(set, status + strlen(status), sizeof(status) - strlen(status));
				status_colour = RED;
			}
			else
			{
				describe(set, status, sizeof(status));
				status_colour = GOLD;
			}
			SDL_free(event.drop.file);
		}
	}
	SDL_DestroyRenderer(renderer);
	SDL_DestroyWindow(window);
	return whole;
}
