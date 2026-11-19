/**
 *    Name:        resampler.h
 *    Description: a band-limited resampler: the board's 125 kHz mix to the sound device's rate
 *    Version:     0.1.0
 *    Created:     19-11-2026
 *
 *    license:BSD-3-Clause
 *    A windowed sinc (Kaiser, beta 8: about 80 dB down past the band), RESAMPLER_TAPS input samples an output
 *    sample, cut at 20 kHz (or 45% of a lower output rate), its kernel kept for RESAMPLER_PHASES places between two
 *    input samples and interpolated between them. Output sample n is the input at time n x step (no delay).
 */
#ifndef MAKAIMURA_RESAMPLER_H
#define MAKAIMURA_RESAMPLER_H

#include <stdint.h>

#define RESAMPLER_TAPS 128
#define RESAMPLER_PHASES 256
#define RESAMPLER_INPUT 16384

typedef struct resampler
{
	float kernel[RESAMPLER_PHASES + 1][RESAMPLER_TAPS];
	float input[RESAMPLER_INPUT];
	int count;            /* input[0 .. count) */
	double position;      /* the next output sample's place in input[] */
} resampler;

void resampler_init(resampler *r, double input_rate, int output_rate);

/* An input sample (dropped if the input is full: pull more often) */
void resampler_push(resampler *r, float sample);

/* The output samples the input has so far, 'step' input samples apart (the rates' ratio), times 'gain': how many */
int resampler_pull(resampler *r, double step, float gain, int16_t *out, int most);

#endif
