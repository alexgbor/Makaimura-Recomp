/**
 *    Name:        replays.h
 *    Description: the replays kept: the folder's files (replays/ beside the settings), newest first
 *    Version:     0.1.0
 *    Created:     20-11-2026
 *
 *    license:BSD-3-Clause
 *    A replay is a game, from the coin or start that began it (or the state it was played from: a slot's, a
 *    practice's) to its game over: the board's state there (NAME.state) and an input script from it (NAME.inp: the
 *    controls as the board read them, the kept ranking's writes; makaimura --inputs takes it), NAME being
 *    YYYYMMDD-HHMMSS-MODE-SECONDS. main.c writes them as a game is played and keeps them if the player says so.
 */
#ifndef MAKAIMURA_REPLAYS_H
#define MAKAIMURA_REPLAYS_H

#define REPLAY_NAME 64

/* The replays in 'folder', newest first: their names without .inp (at most 'max'); how many */
int replays_list(const char *folder, char names[][REPLAY_NAME], int max);

/* A replay's files removed */
void replays_delete(const char *folder, const char *name);

/* "20261120-102530-board-0754" as "11-20 10:25" and "BOARD 12:34" (from a slot's or a practice's state: a "+"
   after the mode) */
void replays_describe(const char *folder, const char *name, char *label, int label_size, char *value, int value_size);

#endif
