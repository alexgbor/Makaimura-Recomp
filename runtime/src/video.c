/**
 *    Name:        video.c
 *    Description: the board's picture (gng/video.h)
 *    Version:     0.1.0
 *    Created:     19-11-2026
 *
 *    license:BSD-3-Clause
 *    After MAME's src/mame/capcom/gng.cpp (BSD-3-Clause, Pierpaolo Prazzoli):
 *      charlayout    8x8, 2 planes {4, 0}, x {0,1,2,3,8,9,10,11}, y {0,16,...,112}, 16 bytes a char
 *      tilelayout    16x16, 3 planes {2/3, 1/3, 0/3 of the region}, x {0-7, 128-135}, y {0,8,...,120}, 32 bytes
 *      spritelayout  16x16, 4 planes {1/2+4, 1/2+0, 4, 0}, x {0-3, 8-11, 256-259, 264-267}, y {0,16,...,240},
 *                    64 bytes
 *      (MAME's gfx decoding: bit offsets count from each byte's high bit; the first plane is the pen's highest bit)
 *    The colours: tiles 00-3F (8 of 8), sprites 40-7F (4 of 16), chars 80-BF (16 of 4).
 *    screen_update: the background's back half (TILEMAP_DRAW_LAYER1), the sprites, its front half (LAYER0),
 *    the text layer. The background's split (video_start): group 0's pens all behind (front transmask FF, back
 *    00); group 1's pens 0 and 6 behind, the rest in front (front 41, back BE). The text layer's pen 3 is
 *    transparent; the sprites' pen 15.
 *    The background is 32x32 tiles of 16x16 scanned by columns, scrolled by 3B08-3B0B (16 bits each, wrapping at
 *    512); the text layer 32x32 of 8x8 by rows, not scrolled.
 *    The flipped screen (the LS259's Q0, inverted: flipped while Q0 is 0, so from power-on until the boot writes
 *    3D00): MAME 0.289's tilemaps flip around the visible area's centre (emu/tilemap.cpp draw_common: extents
 *    0+255+1 and 16+239+1, both 256) and gng.cpp's sprites go to 240-x, 240-y with their flips swapped, so the
 *    flipped picture is the unflipped one turned 180 degrees in the 256x256 bitmap, pixel for pixel.
 */
#include "gng/video.h"
#include "gng/board.h"

#include <string.h>

#define CHARS   1024    /* 0x4000 / 16 */
#define TILES   1024    /* 0x18000 / 3 / 32 */
#define SPRITES 1024    /* 0x20000 / 2 / 64 */

static uint8_t char_pens[CHARS][8][8];
static uint8_t tile_pens[TILES][16][16];
static uint8_t sprite_pens[SPRITES][16][16];

static inline int bit(const uint8_t *region, uint32_t offset)
{
	return (region[offset >> 3] >> (7 - (offset & 7))) & 1;
}

int gng_video_char_pen(int code, int x, int y)
{
	return char_pens[code & (CHARS - 1)][y & 7][x & 7];
}

void gng_video_decode(void)
{
	static const int char_x[8] = { 0, 1, 2, 3, 8, 9, 10, 11 };
	static const int sprite_x[16] = { 0, 1, 2, 3, 8, 9, 10, 11, 256, 257, 258, 259, 264, 265, 266, 267 };
	if (gng.roms.chars) {
		for (int c = 0; c < CHARS; c++)
			for (int y = 0; y < 8; y++)
				for (int x = 0; x < 8; x++) {
					const uint32_t base = (uint32_t)c * 128u + (uint32_t)y * 16u + (uint32_t)char_x[x];
					char_pens[c][y][x] = (uint8_t)((bit(gng.roms.chars, base + 4) << 1) | bit(gng.roms.chars, base));
				}
	}
	if (gng.roms.tiles) {
		const uint32_t third = 0x18000u / 3u * 8u;    /* in bits */
		for (int t = 0; t < TILES; t++)
			for (int y = 0; y < 16; y++)
				for (int x = 0; x < 16; x++) {
					const uint32_t base = (uint32_t)t * 256u + (uint32_t)y * 8u + (uint32_t)(x < 8 ? x : 128 + x - 8);
					tile_pens[t][y][x] = (uint8_t)((bit(gng.roms.tiles, base + 2 * third) << 2) |
						(bit(gng.roms.tiles, base + third) << 1) | bit(gng.roms.tiles, base));
				}
	}
	if (gng.roms.sprites) {
		const uint32_t half = 0x20000u / 2u * 8u;
		for (int s = 0; s < SPRITES; s++)
			for (int y = 0; y < 16; y++)
				for (int x = 0; x < 16; x++) {
					const uint32_t base = (uint32_t)s * 512u + (uint32_t)y * 16u + (uint32_t)sprite_x[x];
					sprite_pens[s][y][x] = (uint8_t)((bit(gng.roms.sprites, base + half + 4) << 3) |
						(bit(gng.roms.sprites, base + half) << 2) | (bit(gng.roms.sprites, base + 4) << 1) |
						bit(gng.roms.sprites, base));
				}
	}
}

/* The background's pixel at (x, y) of its 512x512 map: its pen, colour and group */
static inline void background_pixel(int x, int y, int *pen, int *colour, int *group)
{
	const int column = (x >> 4) & 31, row = (y >> 4) & 31;
	const int index = column * 32 + row;    /* TILEMAP_SCAN_COLS */
	const uint8_t attr = gng.bg_ram[index + 0x400];
	const int code = gng.bg_ram[index] + ((attr & 0xC0) << 2);
	int tx = x & 15, ty = y & 15;
	if (attr & 0x10)
		tx = 15 - tx;
	if (attr & 0x20)
		ty = 15 - ty;
	*pen = tile_pens[code][ty][tx];
	*colour = attr & 0x07;
	*group = (attr & 0x08) >> 3;
}

void gng_video_render_indexed(uint8_t *out)
{
	/* the whole 256 lines are drawn as MAME's bitmap is (cliprect: the visible lines 16-239) */
	static uint8_t screen[256][256];
	static uint8_t front[256][256];     /* the background's front half: 0xFF where transparent */
	const int scroll_x = gng.scroll_x[0] + 256 * gng.scroll_x[1];
	const int scroll_y = gng.scroll_y[0] + 256 * gng.scroll_y[1];

	for (int y = GNG_SCREEN_FIRST_LINE; y < GNG_SCREEN_FIRST_LINE + GNG_SCREEN_HEIGHT; y++) {
		for (int x = 0; x < 256; x++) {
			int pen, colour, group;
			background_pixel((x + scroll_x) & 511, (y + scroll_y) & 511, &pen, &colour, &group);
			const uint8_t index = (uint8_t)(colour * 8 + pen);
			/* back half (LAYER1): group 0 every pen, group 1 pens 0 and 6 (back transmask BE) */
			const int behind = group == 0 || pen == 0 || pen == 6;
			/* front half (LAYER0): group 0 none (FF), group 1 all but pens 0 and 6 (41) */
			if (behind)
				screen[y][x] = index;
			front[y][x] = behind ? 0xFF : index;
		}
	}

	/* the sprites, from the buffer's last to its first (the first on top): MAME's 128; the board's DMA copies 96
	   (board mode: docs/accuracy.md #1) */
	const int bytes = gng.accuracy == GNG_ACCURACY_BOARD ? (int)GNG_DMA_SPRITE_BYTES : 0x200;
	for (int offs = bytes - 4; offs >= 0; offs -= 4) {
		const uint8_t *s = &gng.sprite_buffer[offs];
		const uint8_t attr = s[1];
		const int sx = s[3] - 0x100 * (attr & 0x01);
		const int sy = s[2];
		const int flip_x = attr & 0x04, flip_y = attr & 0x08;
		const int code = s[0] + ((attr << 2) & 0x300);
		const int colour = (attr >> 4) & 3;
		for (int py = 0; py < 16; py++) {
			const int y = sy + py;
			if (y < GNG_SCREEN_FIRST_LINE || y >= GNG_SCREEN_FIRST_LINE + GNG_SCREEN_HEIGHT)
				continue;
			for (int px = 0; px < 16; px++) {
				const int x = sx + px;
				if (x < 0 || x > 255)
					continue;
				const uint8_t pen = sprite_pens[code][flip_y ? 15 - py : py][flip_x ? 15 - px : px];
				if (pen != 15)
					screen[y][x] = (uint8_t)(0x40 + colour * 16 + pen);
			}
		}
	}

	for (int y = GNG_SCREEN_FIRST_LINE; y < GNG_SCREEN_FIRST_LINE + GNG_SCREEN_HEIGHT; y++) {
		for (int x = 0; x < 256; x++) {
			if (front[y][x] != 0xFF)
				screen[y][x] = front[y][x];
			/* the text layer: 32x32 by rows, pen 3 transparent */
			const int index = (y >> 3) * 32 + (x >> 3);
			const uint8_t attr = gng.fg_ram[index + 0x400];
			const int code = gng.fg_ram[index] + ((attr & 0xC0) << 2);
			int tx = x & 7, ty = y & 7;
			if (attr & 0x10)
				tx = 7 - tx;
			if (attr & 0x20)
				ty = 7 - ty;
			const uint8_t pen = char_pens[code][ty][tx];
			if (pen != 3)
				screen[y][x] = (uint8_t)(0x80 + (attr & 0x0F) * 4 + pen);
		}
	}

	const int flipped = !(gng.ls259 & 0x01);
	for (int y = GNG_SCREEN_FIRST_LINE; y < GNG_SCREEN_FIRST_LINE + GNG_SCREEN_HEIGHT; y++) {
		uint8_t *line = out + (size_t)(y - GNG_SCREEN_FIRST_LINE) * GNG_SCREEN_WIDTH;
		if (!flipped)
			memcpy(line, screen[y], GNG_SCREEN_WIDTH);
		else
			for (int x = 0; x < 256; x++)
				line[x] = screen[255 - y][255 - x];
	}
}

/* ---- board mode: the raster ---- */

#define LINE_CLOCKS  384u
#define FRAME_CLOCKS (384u * 262u)    /* = 4 x GNG_CPU_CYCLES_PER_FRAME */

static uint32_t raster_frame[GNG_SCREEN_HEIGHT][GNG_SCREEN_WIDTH];    /* the frame the beam draws */
static uint32_t raster_done[GNG_SCREEN_HEIGHT][GNG_SCREEN_WIDTH];     /* the last one it finished */
static uint64_t raster_at;                                            /* pixel clocks drawn, from power-on */

static inline uint32_t colour_of(int index)
{
	/* RGBx_444: the 3800 byte RRRRGGGG, the 3900 byte BBBBxxxx; 4 bits to 8 as pal4bit */
	const int r = gng.palette_ext[index] >> 4, g = gng.palette_ext[index] & 15, b = gng.palette_base[index] >> 4;
	return ((uint32_t)(r * 17) << 16) | ((uint32_t)(g * 17) << 8) | (uint32_t)(b * 17);
}

/* Line y (of the 256) as gng_video_render_indexed draws it, unflipped: the same layers, the same order */
static void screen_line(int y, uint8_t *out)
{
	uint8_t front[256];
	const int scroll_x = gng.scroll_x[0] + 256 * gng.scroll_x[1];
	const int scroll_y = gng.scroll_y[0] + 256 * gng.scroll_y[1];
	for (int x = 0; x < 256; x++) {
		int pen, colour, group;
		background_pixel((x + scroll_x) & 511, (y + scroll_y) & 511, &pen, &colour, &group);
		const uint8_t index = (uint8_t)(colour * 8 + pen);
		const int behind = group == 0 || pen == 0 || pen == 6;
		out[x] = behind ? index : 0;
		front[x] = behind ? 0xFF : index;
	}
	const int bytes = gng.accuracy == GNG_ACCURACY_BOARD ? (int)GNG_DMA_SPRITE_BYTES : 0x200;
	for (int offs = bytes - 4; offs >= 0; offs -= 4) {
		const uint8_t *s = &gng.sprite_buffer[offs];
		const int py = y - s[2];
		if (py < 0 || py > 15)
			continue;
		const uint8_t attr = s[1];
		const int sx = s[3] - 0x100 * (attr & 0x01);
		const int code = s[0] + ((attr << 2) & 0x300);
		const int colour = (attr >> 4) & 3;
		for (int px = 0; px < 16; px++) {
			const int x = sx + px;
			if (x < 0 || x > 255)
				continue;
			const uint8_t pen = sprite_pens[code][attr & 0x08 ? 15 - py : py][attr & 0x04 ? 15 - px : px];
			if (pen != 15)
				out[x] = (uint8_t)(0x40 + colour * 16 + pen);
		}
	}
	for (int x = 0; x < 256; x++) {
		if (front[x] != 0xFF)
			out[x] = front[x];
		const int index = (y >> 3) * 32 + (x >> 3);
		const uint8_t attr = gng.fg_ram[index + 0x400];
		const int code = gng.fg_ram[index] + ((attr & 0xC0) << 2);
		int tx = x & 7, ty = y & 7;
		if (attr & 0x10)
			tx = 7 - tx;
		if (attr & 0x20)
			ty = 7 - ty;
		const uint8_t pen = char_pens[code][ty][tx];
		if (pen != 3)
			out[x] = (uint8_t)(0x80 + (attr & 0x0F) * 4 + pen);
	}
}

/* The beam's line 'line' (16-239) as the board shows it now: the flipped screen turns it 180 degrees */
static void beam_line(int line, uint8_t *out)
{
	if (gng.ls259 & 0x01) {
		screen_line(line, out);
		return;
	}
	uint8_t source[256];
	screen_line(255 - line, source);
	for (int x = 0; x < 256; x++)
		out[x] = source[255 - x];
}

static void draw_until(uint64_t target)
{
	uint8_t indexes[256];
	while (raster_at < target) {
		const uint32_t in = (uint32_t)(raster_at % FRAME_CLOCKS);
		const uint32_t line = in / LINE_CLOCKS, h = in % LINE_CLOCKS;
		const uint64_t line_end = raster_at - h + LINE_CLOCKS;
		const uint64_t stop = target < line_end ? target : line_end;
		if (line >= GNG_SCREEN_FIRST_LINE && line < GNG_SCREEN_FIRST_LINE + GNG_SCREEN_HEIGHT && h < 256) {
			const uint32_t end = h + (uint32_t)(stop - raster_at) < 256 ? h + (uint32_t)(stop - raster_at) : 256;
			beam_line((int)line, indexes);
			for (uint32_t x = h; x < end; x++)
				raster_frame[line - GNG_SCREEN_FIRST_LINE][x] = colour_of(indexes[x]);
		}
		raster_at = stop;
		if (stop == line_end && line == GNG_SCREEN_FIRST_LINE + GNG_SCREEN_HEIGHT - 1)
			memcpy(raster_done, raster_frame, sizeof(raster_done));
	}
}

void gng_video_reset(void)
{
	raster_at = 0;
	memset(raster_frame, 0, sizeof(raster_frame));
	memset(raster_done, 0, sizeof(raster_done));
}

void gng_video_raster_to(uint64_t cycle)
{
	draw_until(4 * cycle);
}

int gng_video_palette_write(uint64_t cycle, uint16_t address, uint8_t value)
{
	const uint64_t start = 4 * (cycle - 1);
	const uint32_t in = (uint32_t)(start % FRAME_CLOCKS);
	const uint32_t line = in / LINE_CLOCKS, h = in % LINE_CLOCKS;
	if (line < GNG_SCREEN_FIRST_LINE || line >= GNG_SCREEN_FIRST_LINE + GNG_SCREEN_HEIGHT) {
		draw_until(4 * cycle);
		return 0;
	}
	draw_until(start);
	gng.palette_conflicts++;
	uint8_t indexes[256];
	beam_line((int)line, indexes);
	const int red_green = address < 0x3900u;
	for (uint32_t k = 0; k < 4; k++) {
		/* (a bus cycle is 4 of the line's 384 pixel clocks: it never crosses a line's end) */
		const uint32_t x = h + k;
		const int entry = x < 256 ? indexes[x] : 0;
		if (red_green)
			gng.palette_ext[entry] = value;
		else
			gng.palette_base[entry] = value;
		if (x < 256 && start + k >= raster_at) {
			/* the pixel shows the data bus: the written byte for its half of the colour, the RAM's other half
			   (jtgng_colmix shows a blue write's low nibble; the blue RAM keeps its high one, as MAME's BBBBxxxx:
			   the high nibble here, the one the RAM stores) */
			raster_frame[line - GNG_SCREEN_FIRST_LINE][x] = colour_of(entry);
		}
	}
	if (raster_at < 4 * cycle)
		raster_at = 4 * cycle;
	return 1;
}

void gng_video_render(uint32_t *pixels)
{
	if (gng.accuracy == GNG_ACCURACY_BOARD) {
		/* the last frame the beam finished: hosts call this at the VBLANK, when it has just finished one */
		if (gng.clock)
			draw_until(4 * gng.clock());
		memcpy(pixels, raster_done, sizeof(raster_done));
		return;
	}
	static uint8_t indexes[GNG_SCREEN_WIDTH * GNG_SCREEN_HEIGHT];
	uint32_t colours[256];
	for (int i = 0; i < 256; i++) {
		/* RGBx_444: the 3800 byte RRRRGGGG, the 3900 byte BBBBxxxx; 4 bits to 8 as pal4bit */
		const int r = gng.palette_ext[i] >> 4, g = gng.palette_ext[i] & 15, b = gng.palette_base[i] >> 4;
		colours[i] = ((uint32_t)(r * 17) << 16) | ((uint32_t)(g * 17) << 8) | (uint32_t)(b * 17);
	}
	gng_video_render_indexed(indexes);
	for (int i = 0; i < GNG_SCREEN_WIDTH * GNG_SCREEN_HEIGHT; i++)
		pixels[i] = colours[indexes[i]];
}
