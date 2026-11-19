/**
 *    Name:        main.c
 *    Description: makaimura: the game in a window (SDL2): the translated program on the board model, its picture,
 *                 its sound, the controls
 *    Version:     0.1.0
 *    Created:     19-11-2026
 *
 *    license:BSD-3-Clause
 *    makaimura [REGIONS]   (default build/makaimurg.regions: python tools/makaimura_rom.py --extract FILE)
 *
 *    The board's own pace: a frame every 384 x 262 pixels of its 6 MHz clock (59.637 Hz), kept by the clock of the
 *    host; the picture shown at each VBLANK (the board's picture, MAME mode), the sound the board's 125 kHz mix
 *    averaged down to the device's rate (the host's conversion, not MAME's resampler).
 *    Controls (MAME's defaults, and more): arrows; Left Ctrl or Z throw (button 1), Left Alt or X jump (button 2);
 *    1 and 2 start, 5 and 6 coins, 9 service; a gamepad: the stick or the pad, A jump, B or X throw, Start, Back a
 *    coin. F3 resets the board; Escape quits.
 */
#include <SDL.h>

#include "gng/board.h"
#include "gng/sound.h"
#include "gng/video.h"
#include "m6809/cpu.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SCALE 3
#define FRAME_RATE (6000000.0 / (384.0 * 262.0))

static SDL_Window *window;
static SDL_Renderer *renderer;
static SDL_Texture *texture;
static SDL_AudioDeviceID audio;
static SDL_GameController *pad;
static int audio_rate = 48000;
static uint32_t pixels[GNG_SCREEN_WIDTH * GNG_SCREEN_HEIGHT];
static Uint64 next_frame;
static int reset_requested;

/* ---- the ROM regions ---- */

typedef struct region { char name[17]; uint32_t size; uint8_t *data; } region;
static region regions[16];
static int region_count;

static const uint8_t *find_region(const char *name)
{
	for (int i = 0; i < region_count; i++)
		if (!strcmp(regions[i].name, name))
			return regions[i].data;
	return NULL;
}

static int read_regions(const char *path)
{
	FILE *file = fopen(path, "rb");
	char magic[4];
	if (!file)
		return 0;
	if (fread(magic, 1, 4, file) != 4 || memcmp(magic, "MKRG", 4))
	{
		fclose(file);
		return 0;
	}
	while (region_count < 16)
	{
		region *r = &regions[region_count];
		uint8_t size[4];
		memset(r->name, 0, sizeof(r->name));
		if (fread(r->name, 1, 16, file) != 16 || fread(size, 1, 4, file) != 4)
			break;
		r->size = size[0] | (size[1] << 8) | (size[2] << 16) | ((uint32_t)size[3] << 24);
		r->data = malloc(r->size);
		if (!r->data || fread(r->data, 1, r->size, file) != r->size)
			break;
		region_count++;
	}
	fclose(file);
	return find_region("maincpu") != NULL;
}

/* ---- the sound: the board's 125 kHz mix averaged over each of the device's samples ---- */

static int16_t audio_buffer[8192];
static int audio_count;
static double audio_phase, audio_sum;
static int audio_terms;

static void on_sample(int16_t sample)
{
	audio_sum += sample;
	audio_terms++;
	audio_phase += (double)audio_rate / GNG_SOUND_RATE;
	if (audio_phase >= 1.0)
	{
		audio_phase -= 1.0;
		if (audio_count < (int)(sizeof(audio_buffer) / sizeof(audio_buffer[0])))
			audio_buffer[audio_count++] = (int16_t)(audio_sum / audio_terms);
		audio_sum = 0;
		audio_terms = 0;
	}
}

/* ---- the controls ---- */

static void read_controls(void)
{
	const Uint8 *keys = SDL_GetKeyboardState(NULL);
	uint8_t system = 0xFF, p1 = 0xFF;
	int left = keys[SDL_SCANCODE_LEFT], right = keys[SDL_SCANCODE_RIGHT];
	int up = keys[SDL_SCANCODE_UP], down = keys[SDL_SCANCODE_DOWN];
	int fire = keys[SDL_SCANCODE_LCTRL] || keys[SDL_SCANCODE_Z];
	int jump = keys[SDL_SCANCODE_LALT] || keys[SDL_SCANCODE_X];
	int start1 = keys[SDL_SCANCODE_1], start2 = keys[SDL_SCANCODE_2];
	int coin1 = keys[SDL_SCANCODE_5], coin2 = keys[SDL_SCANCODE_6], service = keys[SDL_SCANCODE_9];
	if (pad)
	{
		const Sint16 x = SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_LEFTX);
		const Sint16 y = SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_LEFTY);
		left |= x < -16000 || SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_DPAD_LEFT);
		right |= x > 16000 || SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_DPAD_RIGHT);
		up |= y < -16000 || SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_DPAD_UP);
		down |= y > 16000 || SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_DPAD_DOWN);
		jump |= SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_A);
		fire |= SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_B) ||
			SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_X);
		start1 |= SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_START);
		coin1 |= SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_BACK);
	}
	/* the board's joystick is 4-way (PORT_4WAY): one direction at a time, the vertical one first */
	if (up || down)
		left = right = 0;
	if (right) p1 &= ~0x01;
	if (left) p1 &= ~0x02;
	if (down) p1 &= ~0x04;
	if (up) p1 &= ~0x08;
	if (fire) p1 &= ~0x10;
	if (jump) p1 &= ~0x20;
	if (start1) system &= ~0x01;
	if (start2) system &= ~0x02;
	if (service) system &= ~0x20;
	if (coin1) system &= ~0x40;
	if (coin2) system &= ~0x80;
	gng.inputs.system = system;
	gng.inputs.p1 = p1;
}

/* ---- the board's VBLANK: the picture, the sound, the controls, the pace ---- */

static void on_latch(gng_board *board, uint8_t value)
{
	(void)board;
	gng_sound_latch(2 * cpu.cycles, value);
}

static void on_ls259(gng_board *board, unsigned output, unsigned value)
{
	(void)board;
	if (output == 1)
		gng_sound_q1(2 * cpu.cycles, (int)value);
}

static void on_vblank(uint64_t frame)
{
	SDL_Event event;
	gng_sound_run_until(2 * (GNG_VBLANK_CYCLE + frame * GNG_CPU_CYCLES_PER_FRAME));
	if (audio && audio_count)
	{
		SDL_QueueAudio(audio, audio_buffer, (Uint32)(audio_count * sizeof(int16_t)));
		audio_count = 0;
	}
	gng_video_render(pixels);
	SDL_UpdateTexture(texture, NULL, pixels, GNG_SCREEN_WIDTH * 4);
	SDL_RenderClear(renderer);
	SDL_RenderCopy(renderer, texture, NULL, NULL);
	SDL_RenderPresent(renderer);

	while (SDL_PollEvent(&event))
	{
		if (event.type == SDL_QUIT || (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_ESCAPE))
			m6809_request_stop();
		else if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_F3)
		{
			reset_requested = 1;
			m6809_request_stop();
		}
		else if (event.type == SDL_CONTROLLERDEVICEADDED && !pad)
			pad = SDL_GameControllerOpen(event.cdevice.which);
	}
	read_controls();

	/* the board's pace: wait for this frame's time (after a long pause, start again from now) */
	const Uint64 frequency = SDL_GetPerformanceFrequency();
	const Uint64 period = (Uint64)(frequency / FRAME_RATE);
	Uint64 now = SDL_GetPerformanceCounter();
	if (next_frame == 0 || now > next_frame + 4 * period)
		next_frame = now;
	next_frame += period;
	while ((now = SDL_GetPerformanceCounter()) < next_frame)
	{
		const Uint64 left = next_frame - now;
		if (left * 1000 / frequency > 2)
			SDL_Delay((Uint32)(left * 1000 / frequency - 1));
	}
}

static void on_fault(const char *what, uint16_t pc)
{
	char message[256];
	snprintf(message, sizeof(message), "The program went where the translation has no code:\n%s at %04X (bank %u)",
		what, pc, gng.bank);
	fprintf(stderr, "makaimura: %s\n", message);
	SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Makaimura", message, window);
}

static void power_on(void)
{
	gng_roms roms = { 0 };
	roms.maincpu = find_region("maincpu");
	roms.audiocpu = find_region("audiocpu");
	roms.chars = find_region("chars");
	roms.tiles = find_region("tiles");
	roms.sprites = find_region("sprites");
	gng_power_on(&roms);
	gng_video_decode();
	gng_snd.on_sample = on_sample;
	gng_sound_power_on(roms.audiocpu, gng.accuracy == GNG_ACCURACY_MAME);
	gng.on_sound_latch = on_latch;
	gng.on_ls259 = on_ls259;
	m6809_host host = { on_vblank, NULL, on_fault };
	m6809_power_on(&host);
}

int main(int argc, char *argv[])
{
	const char *path = argc > 1 ? argv[1] : "build/makaimurg.regions";
	if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER) != 0)
	{
		fprintf(stderr, "makaimura: %s\n", SDL_GetError());
		return 1;
	}
	int found = read_regions(path);
	if (!found && argc <= 1)
	{
		/* started from its own folder (build/bin): the regions are in build/ */
		char *base = SDL_GetBasePath();
		if (base)
		{
			static char beside[1024];
			snprintf(beside, sizeof(beside), "%s../makaimurg.regions", base);
			SDL_free(base);
			region_count = 0;
			found = read_regions(beside);
		}
	}
	if (!found || !find_region("audiocpu"))
	{
		char message[512];
		snprintf(message, sizeof(message), "No ROM regions in %s.\nMake them from your makaimurg.zip and gng.zip with:\n"
			"python tools/makaimura_rom.py --extract %s", path, path);
		SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Makaimura", message, NULL);
		return 1;
	}
	window = SDL_CreateWindow("Makaimura", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
		GNG_SCREEN_WIDTH * SCALE, GNG_SCREEN_HEIGHT * SCALE, SDL_WINDOW_RESIZABLE);
	renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
	SDL_RenderSetLogicalSize(renderer, GNG_SCREEN_WIDTH, GNG_SCREEN_HEIGHT);
	SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
	texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_XRGB8888, SDL_TEXTUREACCESS_STREAMING,
		GNG_SCREEN_WIDTH, GNG_SCREEN_HEIGHT);

	SDL_AudioSpec want, have;
	SDL_zero(want);
	want.freq = 48000;
	want.format = AUDIO_S16SYS;
	want.channels = 1;
	want.samples = 1024;
	audio = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
	if (audio)
	{
		audio_rate = have.freq;
		SDL_PauseAudioDevice(audio, 0);
	}
	for (int i = 0; i < SDL_NumJoysticks() && !pad; i++)
		if (SDL_IsGameController(i))
			pad = SDL_GameControllerOpen(i);

	do
	{
		reset_requested = 0;
		power_on();
		m6809_run(UINT64_MAX);
	} while (reset_requested);

	if (audio)
		SDL_CloseAudioDevice(audio);
	SDL_Quit();
	return 0;
}
