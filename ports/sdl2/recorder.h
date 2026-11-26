/**
 *    Name:        recorder.h
 *    Description: the game recorded, frame for frame: as a video to share (the window's view: the picture as it is
 *                 shown, the side panels; an MP4) or as the board's own picture and sound (an uncompressed AVI)
 *    Version:     0.2.0
 *    Created:     19-11-2026
 *
 *    license:BSD-3-Clause
 *    The board's own frames (256 x 224, 24-bit RGB, at its 59.637 Hz: 100608 / 6000000 s each) and its mix
 *    (resampler.c's, from 125 kHz to 48 kHz, 16-bit mono), in the board's time: what the board did, whatever the
 *    host's pace (a pause, the menu, a slow host take nothing out and add nothing). The picture's 4:3 is in the file
 *    (its 'vprp': players and ffmpeg show the pixels 7:6). OpenDML (AVI 2.0): past 1 GB the file goes on in further
 *    RIFF chunks, each indexed, so a recording has no length limit but the disk's; the first one also has the old
 *    index (idx1) for the players that read only that.
 *    The video (any other name than .avi): the window's view drawn by the CPU at a fixed size (view.h: 16:9 with the
 *    panels, the picture sharp, smooth or the CRT's as the window has it), H.264 and AAC in an MP4 (encoder.h), in
 *    the board's time as the AVI. Drawn and encoded on a thread of its own, a few frames behind the game; the game
 *    waits for it only if it falls that far behind.
 */
#ifndef MAKAIMURA_RECORDER_H
#define MAKAIMURA_RECORDER_H

#include <stddef.h>
#include <stdint.h>

#include "view.h"

#define RECORDER_SOUND_RATE 48000

/* A recording to 'path' started: the board's AVI if it ends in .avi, else the video, 'height' lines high (its
   width view_width's for 'style'). 0 if it could not be, why in 'why' */
int recorder_open(const char *path, int height, const view_style *style, char *why, size_t why_size);

/* The board's mix, sample for sample (gng/sound.h's on_sample, 125 kHz) */
void recorder_sample(int16_t sample);

/* A frame's picture (XRGB8888, GNG_SCREEN_WIDTH x GNG_SCREEN_HEIGHT) and the sound up to it; for the video the
   panels (hud.h's HUD_W x HUD_H, NULL: none) and the view's style too. 0 if the video's encoder failed */
int recorder_frame(const uint32_t *pixels, const uint32_t *left, const uint32_t *right, const view_style *style);

/* The file finished (its headers and indexes; the video's last frames encoded): playable. 0 if it is not */
int recorder_close(void);

int recorder_on(void);

/* the recording is the video (the panels wanted with each frame) */
int recorder_video(void);

/* (for the messages) why the video failed, what encodes it */
const char *recorder_problem(void);
const char *recorder_encoder(void);

/* (for the messages) the frames recorded */
uint32_t recorder_frames(void);

#endif
