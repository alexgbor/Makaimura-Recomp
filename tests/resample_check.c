/**
 *    Name:        resample_check.c
 *    Description: resample_check IN.wav RATE OUT.raw: the window's resampler (ports/sdl2/resampler.c) on a WAV of
 *                 the board's mix (gng_headless --wav: 125 kHz, mono, 16 bits), at the rates' exact ratio, out as
 *                 raw 16-bit samples; tools/check_resampler.py compares it with scipy's
 *    Version:     0.1.0
 *    Created:     19-11-2026
 *
 *    license:BSD-3-Clause
 */
#include "resampler.h"

#include <stdio.h>
#include <stdlib.h>

static resampler converter;

int main(int argc, char **argv)
{
	if (argc != 4)
	{
		fprintf(stderr, "resample_check IN.wav RATE OUT.raw\n");
		return 2;
	}
	FILE *in = fopen(argv[1], "rb"), *out = fopen(argv[3], "wb");
	const int rate = atoi(argv[2]);
	if (!in || !out || rate <= 0 || fseek(in, 44, SEEK_SET))
	{
		fprintf(stderr, "resample_check: cannot read %s or write %s\n", argv[1], argv[3]);
		return 2;
	}
	resampler_init(&converter, 125000.0, rate);
	const double step = 125000.0 / rate;
	int16_t samples[2096], output[2048];
	size_t got;
	long total = 0;
	while ((got = fread(samples, sizeof(int16_t), sizeof(samples) / sizeof(samples[0]), in)) > 0)
	{
		for (size_t i = 0; i < got; i++)
			resampler_push(&converter, samples[i]);
		int count;
		while ((count = resampler_pull(&converter, step, 1.0f, output, 2048)) > 0)
		{
			fwrite(output, sizeof(int16_t), (size_t)count, out);
			total += count;
		}
	}
	fclose(in);
	fclose(out);
	printf("%ld samples at %d Hz\n", total, rate);
	return 0;
}
