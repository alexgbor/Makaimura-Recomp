/**
 *    Name:        video.h
 *    Description: the board's picture: the background's back half, the sprites, the background's front half and
 *                 the text layer, drawn as MAME's gng.cpp draws them
 *    Version:     0.1.0
 *    Created:     19-11-2026
 *
 *    license:BSD-3-Clause
 *    After MAME's src/mame/capcom/gng.cpp (BSD-3-Clause, Pierpaolo Prazzoli): the gfx layouts, get_fg_tile_info,
 *    get_bg_tile_info, video_start's transparencies, draw_sprites and screen_update; emu/tilemap.cpp's split
 *    layers and scrolling; emu/emupal.cpp's RGBx_444.
 */
#ifndef GNG_VIDEO_H
#define GNG_VIDEO_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define GNG_SCREEN_WIDTH  256
#define GNG_SCREEN_HEIGHT 224    /* lines 16-239 of the 256 drawn */
#define GNG_SCREEN_FIRST_LINE 16

/* The graphics ROMs decoded into a pen a pixel (once, after gng_power_on with the ROMs) */
void gng_video_decode(void);

/* The picture as the board shows it at this VBLANK: 256x224 pixels of 0x00RRGGBB */
void gng_video_render(uint32_t *pixels);

/* The same as palette indexes (0-255): what the checks compare, before the colours */
void gng_video_render_indexed(uint8_t *indexes);

/* A pixel of the text layer's character 'code' (0-1023): its pen, 0-3 (3 transparent), after gng_video_decode */
int gng_video_char_pen(int code, int x, int y);

#ifdef __cplusplus
}
#endif

#endif
