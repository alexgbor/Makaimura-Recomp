/**
 *    Name:        encoder.h
 *    Description: a video file for sharing: H.264 pictures and AAC sound in an MP4, by the system's own encoders
 *    Version:     0.1.0
 *    Created:     26-11-2026
 *
 *    license:BSD-3-Clause
 *    Windows: Media Foundation's sink writer (its H.264 and AAC encoders, a hardware one when the system has it;
 *    nothing for the package to carry). Elsewhere, or when Media Foundation cannot: the ffmpeg program found in the
 *    PATH (or named by the MAKAIMURA_FFMPEG environment variable), the pictures through a pipe, the sound into a
 *    WAV beside them, both put together in the MP4 as it closes.
 *    The frames at a fixed rate (num / den a second): each picture's time is its count's, whatever the host's pace.
 */
#ifndef MAKAIMURA_ENCODER_H
#define MAKAIMURA_ENCODER_H

#include <stddef.h>
#include <stdint.h>

typedef struct encoder encoder;

/* An MP4 at 'path' (width x height, even), 'num' / 'den' frames a second, 'bitrate' bits a second for the
   picture; the sound 16-bit mono at 'sample_rate'. NULL if it cannot, why in 'why' */
encoder *encoder_open(const char *path, int width, int height, unsigned num, unsigned den, unsigned bitrate,
	unsigned sample_rate, char *why, size_t why_size);

/* the next frame's picture (0x00RRGGBB, top row first); 0 if the encoder failed */
int encoder_picture(encoder *e, const uint32_t *pixels);

/* the sound that goes with the pictures so far */
int encoder_sound(encoder *e, const int16_t *samples, int count);

/* the file finished; 0 if it could not be */
int encoder_close(encoder *e);

/* (for the messages) what encodes: "Media Foundation" or "ffmpeg" */
const char *encoder_name(const encoder *e);

#endif
