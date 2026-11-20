/**
 *    Name:        crt.c
 *    Description: the picture as an arcade's CRT shows it (crt.h)
 *    Version:     0.1.0
 *    Created:     20-11-2026
 *
 *    license:BSD-3-Clause
 *    In linear light (the tube's gamma, 2.4, undone first and the display's, 2.2, put back last):
 *    - each output column samples its source line through a narrow Gaussian (the beam's horizontal spot);
 *    - each source line is a beam whose vertical profile is a Gaussian whose width grows with its brightness
 *      (a bright line blooms over the dark gap, a dark one is thin: the scanlines show most in the dark, as on a
 *      tube); an output row adds the two lines nearest it;
 *    - the phosphors' mask (a slot mask or an aperture grille) at the output's own pixels; the light the gaps and
 *      the mask take given back in part by a gain (a tube is dimmer than the picture), what then goes over white
 *      rolled off softly;
 *    - a light vignette.
 */
#include "crt.h"

#include <SDL.h>

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define LEVELS 1024        /* the linear light's steps in the tables */
#define DISTANCES 64       /* a beam's distances from its line's centre, 0 to 1 line */

typedef struct
{
	float spot;            /* the horizontal spot's sigma, in source pixels */
	float thin, wide;      /* the beam's vertical sigma for black and for white, in lines */
	float mask;            /* the mask's dark phosphors' light (1: no mask) */
	float vignette;
} look;

static const look looks[CRT_STYLES] = {
	{ 0 },
	{ 0.34f, 0.18f, 0.34f, 1.00f, 0.06f },    /* scanlines alone */
	{ 0.38f, 0.20f, 0.36f, 0.62f, 0.08f },    /* arcade: a soft spot, a round, blooming beam, a slot mask */
	{ 0.30f, 0.16f, 0.31f, 0.58f, 0.05f },    /* PVM: a sharper spot and beam, an aperture grille */
};

static float to_linear[256];
#define TONES 8192         /* the output light 0-2 in steps, to the display's byte (the roll-off over white in it) */
static uint8_t tone[TONES];
static float beam[CRT_STYLES][DISTANCES + 1][LEVELS];    /* a line's light at a distance, by its level */
static int tables_ready;
static const float zero[LEVELS];    /* (no line beyond the picture's edges) */

static void make_tables(void)
{
	for (int i = 0; i < 256; i++)
		to_linear[i] = powf(i / 255.0f, 2.4f);
	for (int i = 0; i < TONES; i++)
	{
		float light = 2.0f * i / (TONES - 1);
		/* what goes over white rolled off softly */
		if (light > 0.85f)
			light = 0.85f + 0.15f * (1.0f - expf(-(light - 0.85f) / 0.15f));
		tone[i] = (uint8_t)lrintf(255.0f * powf(light, 1.0f / 2.2f));
	}
	for (int s = 1; s < CRT_STYLES; s++)
		for (int d = 0; d <= DISTANCES; d++)
			for (int l = 0; l < LEVELS; l++)
			{
				const float level = (float)l / (LEVELS - 1), distance = (float)d / DISTANCES;
				/* the beam's width: from thin to wide with the square root of the light (it blooms early) */
				const float sigma = looks[s].thin + (looks[s].wide - looks[s].thin) * sqrtf(level);
				beam[s][d][l] = level * expf(-0.5f * distance * distance / (sigma * sigma));
			}
	tables_ready = 1;
}

/* the horizontal pass's buffers: each source line sampled at the output's columns, in linear light */
static uint16_t *lines;        /* (each source line at the output's columns: its linear light's level, 0-LEVELS-1) */
static float *factors;         /* (the mask, the gain and the vignette's columns: 4 rows' patterns x columns x 3) */
static int factors_style, factors_dw;
static int lines_width, lines_height;
static int *taps_start;
static float *taps_weight;
static int taps_style, taps_sw, taps_dw;
#define TAPS 5

static void horizontal_taps(int style, int sw, int dw)
{
	if (taps_start && taps_style == style && taps_sw == sw && taps_dw == dw)
		return;
	free(taps_start);
	free(taps_weight);
	taps_start = malloc(sizeof(int) * (size_t)dw);
	taps_weight = malloc(sizeof(float) * (size_t)dw * TAPS);
	const float sigma = looks[style].spot;
	for (int x = 0; x < dw; x++)
	{
		const float u = (x + 0.5f) * sw / dw - 0.5f;
		const int first = (int)floorf(u) - TAPS / 2 + 1;
		float total = 0;
		for (int k = 0; k < TAPS; k++)
		{
			const float d = (first + k) - u;
			total += taps_weight[x * TAPS + k] = expf(-0.5f * d * d / (sigma * sigma));
		}
		for (int k = 0; k < TAPS; k++)
			taps_weight[x * TAPS + k] /= total;
		taps_start[x] = first;
	}
	taps_style = style;
	taps_sw = sw;
	taps_dw = dw;
}

/* the mask's light for a sub-pixel (c: 0 red, 1 green, 2 blue) at an output pixel */
static inline float mask_at(int style, int x, int y, int c, float dark)
{
	if (style == CRT_PVM)
		return x % 3 == c ? 1.0f : dark;    /* an aperture grille: R G B stripes */
	if (style == CRT_ARCADE)
	{
		/* a slot mask: R G B triads, every other one half a slot down, a dark row between the slots */
		const int triad = x / 3, row = (y + (triad & 1) * 2) % 4;
		return (x % 3 == c ? 1.0f : dark) * (row == 3 ? 0.75f : 1.0f);
	}
	return 1.0f;
}

/* the frame's work, shared by the passes' threads */
typedef struct
{
	const uint32_t *source;
	uint32_t *out;
	int sw, sh, dw, dh, style;
	float gain;
} frame;

typedef struct
{
	const frame *f;
	void (*rows)(const frame *f, int first, int end);
	int first, end;
} share;

static int run_share(void *data)
{
	share *s = data;
	s->rows(s->f, s->first, s->end);
	return 0;
}

/* rows [0, count) of a pass, cut among the CPU's cores (the last cut on this thread) */
static void parallel(const frame *f, void (*rows)(const frame *, int, int), int count)
{
	enum { MOST = 16 };
	int threads = SDL_GetCPUCount();
	threads = threads < 1 ? 1 : threads > 8 ? 8 : threads;
	share shares[MOST];
	SDL_Thread *started[MOST] = { 0 };
	for (int t = 0; t < threads; t++)
	{
		shares[t] = (share){ f, rows, count * t / threads, count * (t + 1) / threads };
		if (t < threads - 1)
			started[t] = SDL_CreateThread(run_share, "crt", &shares[t]);
		if (!started[t])
			run_share(&shares[t]);
	}
	for (int t = 0; t < threads - 1; t++)
		if (started[t])
			SDL_WaitThread(started[t], NULL);
}

static void horizontal_rows(const frame *fr, int first, int end);
static void vertical_rows(const frame *fr, int first, int end);

void crt_render(const uint32_t *source, int sw, int sh, uint32_t *out, int dw, int dh, int style)
{
	if (style <= CRT_OFF || style >= CRT_STYLES || dw <= 0 || dh <= 0)
		return;
	if (!tables_ready)
		make_tables();
	horizontal_taps(style, sw, dw);
	if (!lines || lines_width != dw || lines_height != sh)
	{
		free(lines);
		lines = malloc(sizeof(uint16_t) * 3 * (size_t)dw * (size_t)sh);
		lines_width = dw;
		lines_height = sh;
	}
	const look *l = &looks[style];

	frame fr = { source, out, sw, sh, dw, dh, style, 1.0f };
	parallel(&fr, horizontal_rows, sh);

	/* the mask's light given back: its average over a triad (and the slot's dark row) */
	const float mask_mean = style == CRT_ARCADE ? (1.0f + 2.0f * l->mask) / 3.0f * (3.0f + 0.75f) / 4.0f
		: style == CRT_PVM ? (1.0f + 2.0f * l->mask) / 3.0f : 1.0f;
	/* (the beams' light on a flat field: about their width x 2.5 lines' worth; the mask's taken in part) */
	fr.gain = 1.30f * (1.0f + 0.7f * (1.0f / mask_mean - 1.0f));
	if (!factors || factors_style != style || factors_dw != dw)
	{
		free(factors);
		factors = malloc(sizeof(float) * 4 * 3 * (size_t)dw);
		for (int r = 0; r < 4; r++)
			for (int x = 0; x < dw; x++)
			{
				const float nx = 2.0f * x / dw - 1.0f;
				for (int c = 0; c < 3; c++)
					factors[((size_t)r * dw + x) * 3 + c] = fr.gain * mask_at(style, x, r, c, l->mask) *
						(1.0f - 0.5f * l->vignette * nx * nx);
			}
		factors_style = style;
		factors_dw = dw;
	}

	parallel(&fr, vertical_rows, dh);
}

static void horizontal_rows(const frame *fr, int first, int end)
{
	const uint32_t *source = fr->source;
	uint32_t *out = fr->out;
	const int sw = fr->sw, sh = fr->sh, dw = fr->dw, dh = fr->dh, style = fr->style;
	const look *l = &looks[style];
	(void)source; (void)out; (void)dh; (void)l;
	/* the horizontal pass: each source line at the output's columns */
	for (int y = first; y < end; y++)
	{
		const uint32_t *row = source + (size_t)y * sw;
		uint16_t *outline = lines + (size_t)y * dw * 3;
		for (int x = 0; x < dw; x++)
		{
			float r = 0, g = 0, b = 0;
			const int first = taps_start[x];
			for (int k = 0; k < TAPS; k++)
			{
				int sx = first + k;
				sx = sx < 0 ? 0 : sx >= sw ? sw - 1 : sx;
				const uint32_t p = row[sx];
				const float w = taps_weight[x * TAPS + k];
				r += w * to_linear[(p >> 16) & 255];
				g += w * to_linear[(p >> 8) & 255];
				b += w * to_linear[p & 255];
			}
			outline[3 * x] = (uint16_t)(r * (LEVELS - 1) + 0.5f);
			outline[3 * x + 1] = (uint16_t)(g * (LEVELS - 1) + 0.5f);
			outline[3 * x + 2] = (uint16_t)(b * (LEVELS - 1) + 0.5f);
		}
	}

}

static void vertical_rows(const frame *fr, int first, int end)
{
	const uint32_t *source = fr->source;
	uint32_t *out = fr->out;
	const int sw = fr->sw, sh = fr->sh, dw = fr->dw, dh = fr->dh, style = fr->style;
	const look *l = &looks[style];
	(void)source; (void)out; (void)dh; (void)l;
	/* the vertical pass: the two beams nearest each output row, the mask, back to the display's gamma */
	for (int oy = first; oy < end; oy++)
	{
		const float v = (oy + 0.5f) * sh / dh - 0.5f;
		const int above = (int)floorf(v);
		const float f = v - above;
		const int da = (int)lrintf(f * DISTANCES), db = DISTANCES - da;
		const uint16_t *la = lines + (size_t)(above < 0 ? 0 : above) * dw * 3;
		const uint16_t *lb = lines + (size_t)(above + 1 >= sh ? sh - 1 : above + 1) * dw * 3;
		const float *ba = above < 0 ? zero : beam[style][da], *bb = above + 1 >= sh ? zero : beam[style][db];
		const float ny = 2.0f * oy / dh - 1.0f;
		const float row_shade = (1.0f - 0.5f * l->vignette * ny * ny) * (TONES - 1) / 2.0f;
		const float *factor = factors + (size_t)(oy & 3) * dw * 3;
		uint32_t *outrow = out + (size_t)oy * dw;
		for (int x = 0; x < dw; x++)
		{
			uint32_t pixel = 0;
			for (int c = 0; c < 3; c++)
			{
				const int i = 3 * x + c;
				const float light = (ba[la[i]] + bb[lb[i]]) * factor[i] * row_shade;
				const int index = (int)light;
				pixel = pixel << 8 | tone[index < TONES ? index : TONES - 1];
			}
			outrow[x] = pixel;
		}
	}
}
