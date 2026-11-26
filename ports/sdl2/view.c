/**
 *    Name:        view.c
 *    Description: the window's view drawn by the CPU at a given size (view.h)
 *    Version:     0.1.0
 *    Created:     26-11-2026
 *
 *    license:BSD-3-Clause
 *    The scaling is the window's renderer's, done here: a texture's linear filter samples at the pixels' centres
 *    ((x + 0.5) * from / to - 0.5), its edges clamped; sharp pixels are the board's picture enlarged by whole numbers
 *    (each axis the least that reaches the size, as main.c's prescaled texture) and that filtered down.
 */
#include "view.h"

#include "crt.h"
#include "gng/video.h"
#include "hud.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

typedef struct tap { int a, b, w; } tap;    /* two source pixels and the second's weight (0-256) */

struct view
{
	crt_context *crt;
	/* what is beside the picture, last drawn: into which frame, its layout, the panels' pixels */
	const uint32_t *sides_out;
	int sides_width, sides_height, sides_picture, sides_panels;
	uint32_t left[HUD_W * HUD_H], right[HUD_W * HUD_H];
	tap *columns, *rows;
	int columns_size, rows_size;
};

view *view_create(void)
{
	view *v = calloc(1, sizeof(view));
	if (v && !(v->crt = crt_create()))
	{
		free(v);
		return NULL;
	}
	return v;
}

void view_destroy(view *v)
{
	if (!v)
		return;
	crt_destroy(v->crt);
	free(v->columns);
	free(v->rows);
	free(v);
}

static int picture_width(const view_style *style, int height)
{
	const double aspect = style->monitor_aspect ? 4.0 / 3.0 : (double)GNG_SCREEN_WIDTH / GNG_SCREEN_HEIGHT;
	return (int)lround(height * aspect / 2) * 2;
}

int view_width(const view_style *style, int height)
{
	return style->panels ? (int)lround(height * 16.0 / 9.0 / 2) * 2 : picture_width(style, height);
}

/* a source axis of 'from' pixels, each 'times' wide, filtered to 'to' */
static void make_taps(tap *taps, int from, int times, int to)
{
	const int wide = from * times;
	for (int x = 0; x < to; x++)
	{
		const double u = (x + 0.5) * wide / to - 0.5;
		const int i = (int)floor(u);
		const int first = i < 0 ? 0 : i >= wide ? wide - 1 : i;
		const int second = i + 1 < 0 ? 0 : i + 1 >= wide ? wide - 1 : i + 1;
		taps[x].a = first / times;
		taps[x].b = second / times;
		taps[x].w = taps[x].a == taps[x].b ? 0 : (int)lround((u - i) * 256);
	}
}

static inline uint32_t mix(uint32_t p, uint32_t q, int w)
{
	if (w == 0 || p == q)
		return p;
	const uint32_t rb = ((p & 0xFF00FF) * (uint32_t)(256 - w) + (q & 0xFF00FF) * (uint32_t)w) >> 8 & 0xFF00FF;
	const uint32_t g = ((p & 0x00FF00) * (uint32_t)(256 - w) + (q & 0x00FF00) * (uint32_t)w) >> 8 & 0x00FF00;
	return rb | g;
}

/* 'source' (sw x sh), each pixel kx x ky first, filtered into out's rectangle (x0, y0, dw x dh) */
static void scale(view *v, const uint32_t *source, int sw, int sh, int kx, int ky, uint32_t *out, int stride,
	int x0, int y0, int dw, int dh)
{
	if (v->columns_size < dw)
	{
		free(v->columns);
		v->columns = malloc(sizeof(tap) * (size_t)dw);
		v->columns_size = v->columns ? dw : 0;
	}
	if (v->rows_size < dh)
	{
		free(v->rows);
		v->rows = malloc(sizeof(tap) * (size_t)dh);
		v->rows_size = v->rows ? dh : 0;
	}
	if (!v->columns || !v->rows)
		return;
	make_taps(v->columns, sw, kx, dw);
	make_taps(v->rows, sh, ky, dh);
	for (int y = 0; y < dh; y++)
	{
		const tap r = v->rows[y];
		uint32_t *line = out + (size_t)(y0 + y) * stride + x0;
		if (y > 0 && r.a == v->rows[y - 1].a && r.b == v->rows[y - 1].b && r.w == v->rows[y - 1].w)
		{
			/* (an enlarged pixel's rows: the line above again) */
			memcpy(line, line - stride, sizeof(uint32_t) * (size_t)dw);
			continue;
		}
		const uint32_t *above = source + (size_t)r.a * sw, *below = source + (size_t)r.b * sw;
		for (int x = 0; x < dw; x++)
		{
			const tap c = v->columns[x];
			const uint32_t top = mix(above[c.a], above[c.b], c.w);
			line[x] = r.w ? mix(top, mix(below[c.a], below[c.b], c.w), r.w) : top;
		}
	}
}

void view_draw(view *v, const view_style *style, const uint32_t *picture, const uint32_t *left,
	const uint32_t *right, uint32_t *out, int width, int height)
{
	int w = picture_width(style, height);
	if (w > width)
		w = width;
	const int x = (width - w) / 2;
	if (style->crt)
		crt_render_in(v->crt, picture, GNG_SCREEN_WIDTH, GNG_SCREEN_HEIGHT, out + x, width, w, height, style->crt);
	else if (style->smooth)
		scale(v, picture, GNG_SCREEN_WIDTH, GNG_SCREEN_HEIGHT, 1, 1, out, width, x, 0, w, height);
	else
		scale(v, picture, GNG_SCREEN_WIDTH, GNG_SCREEN_HEIGHT, (w + GNG_SCREEN_WIDTH - 1) / GNG_SCREEN_WIDTH,
			(height + GNG_SCREEN_HEIGHT - 1) / GNG_SCREEN_HEIGHT, out, width, x, 0, w, height);

	/* beside the picture: black and the panels, drawn again only when they change ('out' is the same frame each
	   time: what is there stays) */
	const int panels = left && right;
	if (out == v->sides_out && width == v->sides_width && height == v->sides_height && w == v->sides_picture
		&& panels == v->sides_panels && (!panels || (!memcmp(left, v->left, sizeof(v->left))
			&& !memcmp(right, v->right, sizeof(v->right)))))
		return;
	v->sides_out = out;
	v->sides_width = width;
	v->sides_height = height;
	v->sides_picture = w;
	v->sides_panels = panels;
	if (panels)
	{
		memcpy(v->left, left, sizeof(v->left));
		memcpy(v->right, right, sizeof(v->right));
	}
	for (int y = 0; y < height; y++)
	{
		memset(out + (size_t)y * width, 0, sizeof(uint32_t) * (size_t)x);
		memset(out + (size_t)y * width + x + w, 0, sizeof(uint32_t) * (size_t)(width - x - w));
	}
	/* the panels, as main.c's draw_panels places them */
	const int side = x;
	if (!panels || side < HUD_W / 2)
		return;
	const int margin = side / 16 + 2;
	double factor = (double)(side - 2 * margin) / HUD_W;
	if (factor * HUD_H > height - 2 * margin)
		factor = (double)(height - 2 * margin) / HUD_H;
	const int pw = (int)(HUD_W * factor), ph = (int)(HUD_H * factor);
	if (pw <= 0 || ph <= 0)
		return;
	scale(v, left, HUD_W, HUD_H, 1, 1, out, width, (side - pw) / 2, (height - ph) / 2, pw, ph);
	scale(v, right, HUD_W, HUD_H, 1, 1, out, width, x + w + (width - x - w - pw) / 2, (height - ph) / 2, pw, ph);
}
