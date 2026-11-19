/**
 *    Name:        audio_out.h
 *    Description: the board's sound out to the host: its 125 kHz mix resampled to the device's rate, queued
 *    Version:     0.1.0
 *    Created:     19-11-2026
 *
 *    license:BSD-3-Clause
 */
#ifndef MAKAIMURA_AUDIO_OUT_H
#define MAKAIMURA_AUDIO_OUT_H

#include <stdint.h>

/* The device opened (0 if there is none: the game runs silent) */
int audio_out_open(int volume, int latency_ms);
void audio_out_close(void);

/* A sample of the board's mix (gng/sound.h's on_sample) */
void audio_out_sample(int16_t sample);

/* A frame's end: its samples resampled and queued. 'speed' is how fast the board runs against its own time
   (1: its 59.637 Hz; a display's refresh over it when the frames follow the display) */
void audio_out_frame(double speed);

void audio_out_pause(int paused);
void audio_out_volume(int volume);

/* (for checks) how the queue went: its lowest and highest, the frames it was found empty */
void audio_out_report(void);

#endif
