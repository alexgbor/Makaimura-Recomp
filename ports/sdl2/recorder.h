/**
 *    Name:        recorder.h
 *    Description: the game recorded: the board's picture and sound, frame for frame, into an uncompressed AVI
 *    Version:     0.1.0
 *    Created:     19-11-2026
 *
 *    license:BSD-3-Clause
 *    The board's own frames (256 x 224, 24-bit RGB, at its 59.637 Hz: 100608 / 6000000 s each) and its mix
 *    (resampler.c's, from 125 kHz to 48 kHz, 16-bit mono), in the board's time: what the board did, whatever the
 *    host's pace (a pause, the menu, a slow host take nothing out and add nothing). The picture's 4:3 is in the file
 *    (its 'vprp': players and ffmpeg show the pixels 7:6). OpenDML (AVI 2.0): past 1 GB the file goes on in further
 *    RIFF chunks, each indexed, so a recording has no length limit but the disk's; the first one also has the old
 *    index (idx1) for the players that read only that.
 */
#ifndef MAKAIMURA_RECORDER_H
#define MAKAIMURA_RECORDER_H

#include <stdint.h>

#define RECORDER_SOUND_RATE 48000

/* A recording to 'path' started (0 if the file could not be made) */
int recorder_open(const char *path);

/* The board's mix, sample for sample (gng/sound.h's on_sample, 125 kHz) */
void recorder_sample(int16_t sample);

/* A frame's picture (XRGB8888, GNG_SCREEN_WIDTH x GNG_SCREEN_HEIGHT) and the sound up to it */
void recorder_frame(const uint32_t *pixels);

/* The file finished (its headers and indexes): playable */
void recorder_close(void);

int recorder_on(void);

/* (for the messages) the frames recorded */
uint32_t recorder_frames(void);

#endif
