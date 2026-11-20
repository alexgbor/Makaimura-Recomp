/**
 *    Name:        replays.h
 *    Description: the replays kept: the folder's files (replays/ beside the settings), newest first, and the oldest
 *                 let go past a number
 *    Version:     0.1.0
 *    Created:     20-11-2026
 *
 *    license:BSD-3-Clause
 *    A replay is an input script (the controls as the board read them, the DIP switches, the kept ranking's writes:
 *    makaimura --inputs takes it) named YYYYMMDD-HHMMSS-MODE-SECONDS.inp; if it starts from a state (a slot's, a
 *    practice's), that state beside it with the same name, .state. main.c writes them as a game is played.
 */
#ifndef MAKAIMURA_REPLAYS_H
#define MAKAIMURA_REPLAYS_H

#define REPLAYS_KEPT 50
#define REPLAY_NAME 64

/* The replays in 'folder', newest first: their names without .inp (at most 'max'); how many */
int replays_list(const char *folder, char names[][REPLAY_NAME], int max);

/* Past REPLAYS_KEPT, the oldest replays (their .inp and .state) removed */
void replays_prune(const char *folder);

/* "20261120-102530-board-0754" as "11-20 10:25" and "BOARD 12:34" (a state's start: a "+" after the mode) */
void replays_describe(const char *folder, const char *name, char *label, int label_size, char *value, int value_size);

#endif
