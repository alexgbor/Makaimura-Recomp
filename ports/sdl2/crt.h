/**
 *    Name:        crt.h
 *    Description: the picture as an arcade's CRT shows it: the beam's scanlines, wider where the picture is bright,
 *                 the phosphors' mask, in linear light
 *    Version:     0.1.0
 *    Created:     20-11-2026
 *
 *    license:BSD-3-Clause
 *    Drawn by the CPU at the size the picture has on the screen (its rows shared among threads), so that each scanline and
 *    the mask's triads land on whole screen pixels.
 */
#ifndef MAKAIMURA_CRT_H
#define MAKAIMURA_CRT_H

#include <stdint.h>

enum
{
	CRT_OFF,
	CRT_SCANLINES,    /* the beam's scanlines alone, no mask (first: the look that holds best) */
	CRT_ARCADE,       /* a standard-resolution arcade monitor: a slot mask, a soft, round beam */
	CRT_PVM,          /* a professional monitor: an aperture grille, a sharper beam */
	CRT_STYLES
};

/* The picture 'source' (sw x sh, 0x00RRGGBB) drawn into 'out' (dw x dh) in 'style' (the window's: one thread) */
void crt_render(const uint32_t *source, int sw, int sh, uint32_t *out, int dw, int dh, int style);

/* The same with its own buffers, for another thread (the recording's: view.h), into a larger frame ('stride'
   pixels a row) */
typedef struct crt_context crt_context;
crt_context *crt_create(void);
void crt_destroy(crt_context *context);
void crt_render_in(crt_context *context, const uint32_t *source, int sw, int sh, uint32_t *out, int stride, int dw,
	int dh, int style);

#endif
