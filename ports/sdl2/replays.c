/**
 *    Name:        replays.c
 *    Description: the replays kept (replays.h)
 *    Version:     0.1.0
 *    Created:     20-11-2026
 *
 *    license:BSD-3-Clause
 */
#include "replays.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <dirent.h>
#endif

static int newest_first(const void *a, const void *b)
{
	return strcmp((const char *)b, (const char *)a);
}

/* every replay's name (the .inp files'), newest first */
static int every(const char *folder, char (**names)[REPLAY_NAME])
{
	int count = 0, capacity = 0;
	*names = NULL;
#ifdef _WIN32
	char pattern[1400];
	snprintf(pattern, sizeof(pattern), "%s/*.inp", folder);
	WIN32_FIND_DATAA found;
	HANDLE search = FindFirstFileA(pattern, &found);
	if (search == INVALID_HANDLE_VALUE)
		return 0;
	do
	{
		const char *file = found.cFileName;
#else
	DIR *dir = opendir(folder);
	if (!dir)
		return 0;
	for (struct dirent *entry; (entry = readdir(dir)) != NULL;)
	{
		const char *file = entry->d_name;
#endif
		const size_t length = strlen(file);
		if (length < 5 || length - 4 >= REPLAY_NAME || strcmp(file + length - 4, ".inp"))
			continue;
		if (count == capacity)
		{
			capacity = capacity ? 2 * capacity : 64;
			*names = realloc(*names, (size_t)capacity * REPLAY_NAME);
		}
		snprintf((*names)[count], REPLAY_NAME, "%.*s", (int)(length - 4), file);
		count++;
#ifdef _WIN32
	} while (FindNextFileA(search, &found));
	FindClose(search);
#else
	}
	closedir(dir);
#endif
	if (count)
		qsort(*names, (size_t)count, REPLAY_NAME, newest_first);
	return count;
}

int replays_list(const char *folder, char names[][REPLAY_NAME], int max)
{
	char (*all)[REPLAY_NAME];
	const int count = every(folder, &all);
	const int shown = count < max ? count : max;
	for (int i = 0; i < shown; i++)
		memcpy(names[i], all[i], REPLAY_NAME);
	free(all);
	return shown;
}

void replays_delete(const char *folder, const char *name)
{
	char path[1400];
	snprintf(path, sizeof(path), "%s/%s.inp", folder, name);
	remove(path);
	snprintf(path, sizeof(path), "%s/%s.state", folder, name);
	remove(path);
}

void replays_describe(const char *folder, const char *name, char *label, int label_size, char *value, int value_size)
{
	int month = 0, day = 0, hour = 0, minute = 0, seconds = 0;
	char mode[16] = "";
	sscanf(name, "%*4d%2d%2d-%2d%2d%*2d-%15[a-z]-%d", &month, &day, &hour, &minute, mode, &seconds);
	snprintf(label, (size_t)label_size, "%02d-%02d %02d:%02d", month, day, hour, minute);
	/* (its script's first lines say where it starts) */
	char path[1400], line[128];
	int from_state = 0;
	snprintf(path, sizeof(path), "%s/%s.inp", folder, name);
	FILE *script = fopen(path, "r");
	for (int i = 0; script && i < 4 && fgets(line, sizeof(line), script); i++)
		from_state |= strstr(line, "# from a loaded state") != NULL;
	if (script)
		fclose(script);
	snprintf(value, (size_t)value_size, "%s%s %d:%02d", !strcmp(mode, "board") ? "BOARD" : "MAME",
		from_state ? "+" : "", seconds / 60, seconds % 60);
}
