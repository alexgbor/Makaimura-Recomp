/**
 *    Name:        resampler.c
 *    Description: a band-limited resampler (resampler.h)
 *    Version:     0.1.0
 *    Created:     19-11-2026
 *
 *    license:BSD-3-Clause
 */
#include "resampler.h"

#include <math.h>
#include <string.h>

#define BETA 8.0
#define PI 3.14159265358979323846
#define HALF (RESAMPLER_TAPS / 2)

static double bessel_i0(double x)
{
	double sum = 1, term = 1;
	for (int k = 1; k < 40; k++)
	{
		term *= (x / (2 * k)) * (x / (2 * k));
		sum += term;
	}
	return sum;
}

void resampler_init(resampler *r, double input_rate, int output_rate)
{
	const double cutoff = fmin(20000.0, 0.45 * output_rate) / input_rate;    /* cycles an input sample */
	for (int p = 0; p <= RESAMPLER_PHASES; p++)
	{
		const double fraction = (double)p / RESAMPLER_PHASES;
		double sum = 0;
		for (int j = 0; j < RESAMPLER_TAPS; j++)
		{
			/* tap j is input sample at - HALF + 1 + j, against the output's place at + fraction */
			const double x = (j - HALF + 1) - fraction;
			const double t = x / HALF;
			const double window = fabs(t) >= 1 ? 0 : bessel_i0(BETA * sqrt(1 - t * t)) / bessel_i0(BETA);
			const double sinc = x == 0 ? 1 : sin(2 * PI * cutoff * x) / (2 * PI * cutoff * x);
			r->kernel[p][j] = (float)(2 * cutoff * sinc * window);
			sum += r->kernel[p][j];
		}
		for (int j = 0; j < RESAMPLER_TAPS; j++)
			r->kernel[p][j] = (float)(r->kernel[p][j] / sum);    /* (a gain of 1 at 0 Hz) */
	}
	/* silence before the first sample, so the first output is the first input's time */
	memset(r->input, 0, sizeof(r->input));
	r->count = HALF - 1;
	r->position = HALF - 1;
}

void resampler_push(resampler *r, float sample)
{
	if (r->count < RESAMPLER_INPUT)
		r->input[r->count++] = sample;
}

int resampler_pull(resampler *r, double step, float gain, int16_t *out, int most)
{
	int count = 0;
	while (count < most)
	{
		const int at = (int)r->position;
		if (at + HALF >= r->count)
			break;
		const double place = (r->position - at) * RESAMPLER_PHASES;
		const int phase = (int)place;
		const float mix = (float)(place - phase);
		const float *a = r->kernel[phase], *b = r->kernel[phase + 1];
		const float *in = r->input + at - HALF + 1;
		float sum = 0;
		for (int j = 0; j < RESAMPLER_TAPS; j++)
			sum += in[j] * (a[j] + (b[j] - a[j]) * mix);
		sum *= gain;
		out[count++] = (int16_t)(sum > 32767.0f ? 32767 : sum < -32768.0f ? -32768 : lrintf(sum));
		r->position += step;
	}
	/* keep the input the next output samples still need */
	const int drop = (int)r->position - HALF + 1;
	if (drop > 0)
	{
		const int keep = r->count > drop ? r->count - drop : 0;
		memmove(r->input, r->input + (r->count - keep), (size_t)keep * sizeof(float));
		r->count = keep;
		r->position -= drop;
	}
	return count;
}
