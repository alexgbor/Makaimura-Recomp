/**
 *    Name:        view.h
 *    Description: the window's view drawn by the CPU at a given size: the picture (sharp, smooth or the CRT's) and
 *                 the side panels, as the window shows them, for the recordings (recorder.h)
 *    Version:     0.1.0
 *    Created:     26-11-2026
 *
 *    license:BSD-3-Clause
 *    The same layout as the window's (main.c's picture_place and draw_panels, at "fit" scaling): the picture as
 *    high as the frame, at the window's aspect, centred; each panel as large as its side lets it, a margin around it.
 *    Sharp pixels as the window draws them: each pixel enlarged by whole numbers first, then the rest smoothed.
 */
#ifndef MAKAIMURA_VIEW_H
#define MAKAIMURA_VIEW_H

#include <stdint.h>

typedef struct view_style
{
	int monitor_aspect;      /* 4:3 (else the board's square pixels) */
	int smooth;              /* a soft picture instead of sharp pixels */
	int crt;                 /* crt.h's style (CRT_OFF: none) */
	int panels;              /* the side panels beside it */
} view_style;

typedef struct view view;

view *view_create(void);
void view_destroy(view *v);

/* The frame's width for 'height' lines in 'style' (even: the video encoders' need): 16:9 with the panels, the
   picture's own width without them */
int view_width(const view_style *style, int height);

/* The board's picture (GNG_SCREEN_WIDTH x GNG_SCREEN_HEIGHT) and the panels (hud.h's HUD_W x HUD_H; NULL: none)
   into 'out' (width x height, 0x00RRGGBB) */
void view_draw(view *v, const view_style *style, const uint32_t *picture, const uint32_t *left,
	const uint32_t *right, uint32_t *out, int width, int height);

#endif
