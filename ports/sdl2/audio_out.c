/**
 *    Name:        audio_out.c
 *    Description: the board's sound out to the host (audio_out.h)
 *    Version:     0.1.0
 *    Created:     19-11-2026
 *
 *    license:BSD-3-Clause
 *    The board's 125 kHz mix through resampler.c to the device's rate, queued (SDL_QueueAudio). The host's clock and
 *    the device's are not the same crystal: the step is nudged (at most 0.5%) to keep the sound queued near the
 *    latency asked for, so it neither grows nor runs dry. This is the port's own conversion, not MAME's (the game's
 *    samples at 125 kHz are MAME's, sample for sample).
 */
#include "audio_out.h"

#include "gng/sound.h"
#include "resampler.h"

#include <SDL.h>

#include <stdio.h>

static SDL_AudioDeviceID device;
static int rate;
static float gain;
static int target;                    /* the samples to keep queued */
static double error_average;
static resampler converter;
static int16_t output[8192];
/* what the queue did (audio_out_report) */
static long frames, dry_frames, low_frames;
static double queued_sum;
static int queued_low = -1, queued_high;

int audio_out_open(int volume, int latency_ms)
{
	SDL_AudioSpec want, have;
	SDL_zero(want);
	want.freq = 48000;
	want.format = AUDIO_S16SYS;
	want.channels = 1;
	want.samples = 512;
	device = SDL_OpenAudioDevice(NULL, 0, &want, &have, SDL_AUDIO_ALLOW_FREQUENCY_CHANGE);
	if (!device)
		return 0;
	rate = have.freq;
	target = rate * latency_ms / 1000;
	audio_out_volume(volume);
	resampler_init(&converter, GNG_SOUND_RATE, rate);
	error_average = 0;
	SDL_PauseAudioDevice(device, 0);
	return 1;
}

void audio_out_close(void)
{
	if (device)
		SDL_CloseAudioDevice(device);
	device = 0;
}

void audio_out_volume(int volume)
{
	gain = (float)(volume / 100.0);
}

void audio_out_sample(int16_t sample)
{
	if (device)
		resampler_push(&converter, sample);
}

void audio_out_pause(int paused)
{
	if (device)
		SDL_PauseAudioDevice(device, paused);
}

void audio_out_frame(double speed)
{
	if (!device)
		return;
	int queued = (int)(SDL_GetQueuedAudioSize(device) / sizeof(int16_t));
	if (queued > 4 * target)
	{
		SDL_ClearQueuedAudio(device);    /* (after a stall: from now) */
		queued = 0;
	}
	if (queued == 0)
	{
		/* at the start, or run dry (a stall): silence up to the latency at once, the nudges only keep it there */
		static const int16_t silence[4096];
		const int frame = (int)(rate / 59.637);
		int pad = target - frame > 0 ? target - frame : 0;
		while (pad > 0)
		{
			const int n = pad < 4096 ? pad : 4096;
			SDL_QueueAudio(device, silence, (Uint32)(n * sizeof(int16_t)));
			pad -= n;
		}
		queued = target - frame > 0 ? target - frame : 0;
	}
	/* the statistics, from the first two seconds on (the queue fills at the start) */
	if (++frames > 120)
	{
		dry_frames += queued == 0;
		low_frames += queued < rate / 100;
		queued_sum += queued;
		if (queued_low < 0 || queued < queued_low)
			queued_low = queued;
		if (queued > queued_high)
			queued_high = queued;
	}
	/* the queue's distance from its target, smoothed: more queued, more input an output sample */
	double error = (double)(queued - target) / target;
	error = error > 1 ? 1 : error < -1 ? -1 : error;
	error_average += (error - error_average) * 0.05;
	const double step = GNG_SOUND_RATE * speed / rate * (1 + 0.005 * error_average);
	const int count = resampler_pull(&converter, step, gain, output, (int)(sizeof(output) / sizeof(output[0])));
	if (count)
		SDL_QueueAudio(device, output, (Uint32)(count * sizeof(int16_t)));
}

void audio_out_report(void)
{
	if (device && frames > 120)
		printf("sound: %d Hz, queued %.1f-%.1f ms, %.1f on average (target %.1f); under 10 ms at %ld, dry at %ld of "
			"%ld frames\n", rate, queued_low * 1000.0 / rate, queued_high * 1000.0 / rate,
			queued_sum / (frames - 120) * 1000.0 / rate, target * 1000.0 / rate, low_frames, dry_frames, frames - 120);
}
