/**
 *    Name:        recorder.c
 *    Description: the game recorded into an uncompressed AVI (recorder.h)
 *    Version:     0.1.0
 *    Created:     19-11-2026
 *
 *    license:BSD-3-Clause
 *    The file, after Microsoft's AVI RIFF reference and the OpenDML AVI File Format Extensions 1.02:
 *      RIFF 'AVI '  LIST 'hdrl' (avih; a strl for the picture: strh, strf, vprp, indx; one for the sound: strh,
 *                   strf, indx; LIST 'odml': dmlh)  LIST 'movi' (00db picture, 01wb sound, frame by frame; ix00,
 *                   ix01: this part's indexes)  idx1
 *      RIFF 'AVIX'  LIST 'movi' (the same, from 1 GB on), as many as it takes
 *    Each stream's indx (the "super index") lists its ix chunks; the sizes and lengths are written as it closes.
 */
#if !defined(_MSC_VER) && !defined(_FILE_OFFSET_BITS)
#define _FILE_OFFSET_BITS 64             /* (fseeko past 2 GB) */
#endif
#include "recorder.h"

#include "gng/sound.h"
#include "gng/video.h"
#include "resampler.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _MSC_VER
#define file_seek _fseeki64
#define file_tell _ftelli64
#else
#define file_seek fseeko
#define file_tell ftello
#endif

#define WIDTH GNG_SCREEN_WIDTH
#define HEIGHT GNG_SCREEN_HEIGHT
#define PICTURE_BYTES (WIDTH * HEIGHT * 3)
#define FRAME_SCALE 100608u              /* 384 x 262 pixels of 6 MHz: a frame (gng.cpp's screen) */
#define FRAME_RATE 6000000u
#define SUPER_ENTRIES 256                /* the ix chunks a stream's indx has room for: 256 GB */
#define PART_LIMIT 0x3F000000ll          /* a RIFF part's size before the next one starts (under 1 GB) */

typedef struct entry { uint32_t offset, size; } entry;
typedef struct super_entry { int64_t offset; uint32_t size, duration; } super_entry;

typedef struct stream
{
	entry *entries;                      /* this part's chunks: their data's offset from the part's movi */
	int count, capacity;
	super_entry parts[SUPER_ENTRIES];
	int part_count;
	int64_t indx_at;                     /* where its indx is */
	int64_t length_at;                   /* its strh's dwLength */
	uint32_t length;                     /* frames, or samples */
	uint32_t part_length;
} stream;

static FILE *file;
static stream picture, sound;
static int64_t riff_at, movi_at;         /* this part's RIFF and its movi list (the 'movi' word's place) */
static int part;                         /* 0: the first RIFF ('AVI ') */
static int64_t avih_frames_at, dmlh_frames_at;
static uint32_t first_part_frames;
static entry *old_index;                 /* idx1: the first part's chunks, both streams (flagged by id) */
static uint8_t *old_index_ids;
static int old_count, old_capacity;
static resampler converter;
static int16_t samples[4096];
static uint8_t sample_bytes[2 * 4096];
static uint8_t row_buffer[PICTURE_BYTES];

static void put16(uint32_t v) { const uint8_t b[2] = { (uint8_t)v, (uint8_t)(v >> 8) }; fwrite(b, 1, 2, file); }
static void put32(uint32_t v)
{
	const uint8_t b[4] = { (uint8_t)v, (uint8_t)(v >> 8), (uint8_t)(v >> 16), (uint8_t)(v >> 24) };
	fwrite(b, 1, 4, file);
}
static void put64(int64_t v) { put32((uint32_t)v); put32((uint32_t)((uint64_t)v >> 32)); }
static void word(const char *fourcc) { fwrite(fourcc, 1, 4, file); }

static void patch32(int64_t at, uint32_t v)
{
	const int64_t here = file_tell(file);
	file_seek(file, at, SEEK_SET);
	put32(v);
	file_seek(file, here, SEEK_SET);
}

/* a chunk or list begun: its size's place (patched by end) */
static int64_t begin(const char *fourcc, const char *list_type)
{
	word(fourcc);
	const int64_t at = file_tell(file);
	put32(0);
	if (list_type)
		word(list_type);
	return at;
}

static void end(int64_t size_at)
{
	patch32(size_at, (uint32_t)(file_tell(file) - size_at - 4));
}

static void add(entry **list, int *count, int *capacity, uint32_t offset, uint32_t size)
{
	if (*count == *capacity)
	{
		*capacity = *capacity ? *capacity * 2 : 4096;
		*list = realloc(*list, (size_t)*capacity * sizeof(entry));
	}
	(*list)[(*count)++] = (entry){ offset, size };
}

/* a stream's indx, room for SUPER_ENTRIES ix chunks: AVI_INDEX_OF_INDEXES, 4 longs an entry */
static void super_index(stream *s, const char *id)
{
	const int64_t size_at = begin("indx", NULL);
	s->indx_at = size_at + 4;
	put16(4);
	fputc(0, file);                      /* bIndexSubType */
	fputc(0, file);                      /* bIndexType: AVI_INDEX_OF_INDEXES */
	put32(0);                            /* nEntriesInUse */
	word(id);
	put32(0); put32(0); put32(0);
	for (int i = 0; i < SUPER_ENTRIES; i++)
	{
		put64(0);
		put32(0);
		put32(0);
	}
	end(size_at);
}

static void write_super_index(const stream *s)
{
	const int64_t here = file_tell(file);
	file_seek(file, s->indx_at + 4, SEEK_SET);
	put32((uint32_t)s->part_count);
	file_seek(file, s->indx_at + 24, SEEK_SET);
	for (int i = 0; i < s->part_count; i++)
	{
		put64(s->parts[i].offset);
		put32(s->parts[i].size);
		put32(s->parts[i].duration);
	}
	file_seek(file, here, SEEK_SET);
}

/* a part's index of a stream's chunks (ix00, ix01): AVI_INDEX_OF_CHUNKS, from the part's movi */
static void standard_index(stream *s, const char *index_id, const char *chunk_id)
{
	if (s->part_count == SUPER_ENTRIES)
		return;
	const int64_t at = file_tell(file);
	const int64_t size_at = begin(index_id, NULL);
	put16(2);
	fputc(0, file);
	fputc(1, file);                      /* AVI_INDEX_OF_CHUNKS */
	put32((uint32_t)s->count);
	word(chunk_id);
	put64(movi_at);
	put32(0);
	for (int i = 0; i < s->count; i++)
	{
		put32(s->entries[i].offset);
		put32(s->entries[i].size);           /* (bit 31 clear: each chunk a key frame) */
	}
	end(size_at);
	s->parts[s->part_count++] = (super_entry){ at, (uint32_t)(file_tell(file) - at), s->part_length };
	s->count = 0;
	s->part_length = 0;
}

static void begin_part(void)
{
	riff_at = begin("RIFF", part == 0 ? "AVI " : "AVIX");
	if (part == 0)
		return;
	movi_at = begin("LIST", "movi") + 4;
}

static void end_part(void)
{
	standard_index(&picture, "ix00", "00db");
	standard_index(&sound, "ix01", "01wb");
	end(movi_at - 4);
	if (part == 0)
	{
		/* idx1: each chunk's header from the 'movi' word, AVIIF_KEYFRAME */
		const int64_t size_at = begin("idx1", NULL);
		for (int i = 0; i < old_count; i++)
		{
			word(old_index_ids[i] ? "01wb" : "00db");
			put32(0x10);
			put32(old_index[i].offset);
			put32(old_index[i].size);
		}
		end(size_at);
	}
	end(riff_at);
	part++;
}

int recorder_open(const char *path)
{
	file = fopen(path, "wb");
	if (!file)
		return 0;
	memset(&picture, 0, sizeof(picture));
	memset(&sound, 0, sizeof(sound));
	part = 0;
	old_count = 0;
	first_part_frames = 0;
	resampler_init(&converter, GNG_SOUND_RATE, RECORDER_SOUND_RATE);

	begin_part();
	const int64_t hdrl = begin("LIST", "hdrl");
	/* avih: MainAVIHeader */
	const int64_t avih = begin("avih", NULL);
	put32(FRAME_SCALE / 6);              /* dwMicroSecPerFrame: 16768 */
	put32((PICTURE_BYTES + RECORDER_SOUND_RATE / 50 * 2) * 60);
	put32(0);
	put32(0x10 | 0x100);                 /* AVIF_HASINDEX, AVIF_ISINTERLEAVED */
	avih_frames_at = file_tell(file);
	put32(0);                            /* dwTotalFrames (the first part's) */
	put32(0);
	put32(2);
	put32(PICTURE_BYTES + 8);
	put32(WIDTH);
	put32(HEIGHT);
	put32(0); put32(0); put32(0); put32(0);
	end(avih);

	/* the picture: 24-bit RGB, bottom-up */
	const int64_t video = begin("LIST", "strl");
	const int64_t strh = begin("strh", NULL);
	word("vids");
	word("DIB ");
	put32(0);
	put16(0); put16(0);
	put32(0);
	put32(FRAME_SCALE);                  /* dwScale / dwRate: 100608 / 6000000 s a frame, 59.637 Hz */
	put32(FRAME_RATE);
	put32(0);
	picture.length_at = file_tell(file);
	put32(0);                            /* dwLength */
	put32(PICTURE_BYTES);
	put32(0xFFFFFFFFu);
	put32(0);
	put16(0); put16(0); put16(WIDTH); put16(HEIGHT);
	end(strh);
	const int64_t strf = begin("strf", NULL);
	put32(40);
	put32(WIDTH);
	put32(HEIGHT);
	put16(1);
	put16(24);
	put32(0);                            /* BI_RGB */
	put32(PICTURE_BYTES);
	put32(0); put32(0); put32(0); put32(0);
	end(strf);
	/* vprp: VIDEO_PROP_HEADER, the frame's 4:3 (the arcade's monitor) */
	const int64_t vprp = begin("vprp", NULL);
	put32(0);                            /* VideoFormatToken: unknown */
	put32(0);                            /* VideoStandard: unknown */
	put32(60);                           /* dwVerticalRefreshRate */
	put32(384);                          /* dwHTotalInT */
	put32(262);                          /* dwVTotalInLines */
	put32(4u << 16 | 3u);                /* dwFrameAspectRatio: 4:3 */
	put32(WIDTH);
	put32(HEIGHT);
	put32(1);                            /* nbFieldPerFrame */
	put32(HEIGHT); put32(WIDTH);         /* CompressedBMHeight, CompressedBMWidth */
	put32(HEIGHT); put32(WIDTH);         /* ValidBMHeight, ValidBMWidth */
	put32(0); put32(0); put32(0); put32(0);
	end(vprp);
	super_index(&picture, "00db");
	end(video);

	/* the sound: 16-bit mono PCM at 48 kHz */
	const int64_t audio = begin("LIST", "strl");
	const int64_t strh2 = begin("strh", NULL);
	word("auds");
	put32(0);
	put32(0);
	put16(0); put16(0);
	put32(0);
	put32(2);                            /* dwScale: nBlockAlign; dwRate: nAvgBytesPerSec */
	put32(RECORDER_SOUND_RATE * 2);
	put32(0);
	sound.length_at = file_tell(file);
	put32(0);                            /* dwLength, in samples */
	put32(RECORDER_SOUND_RATE / 10 * 2);
	put32(0xFFFFFFFFu);
	put32(2);                            /* dwSampleSize */
	put16(0); put16(0); put16(0); put16(0);
	end(strh2);
	const int64_t strf2 = begin("strf", NULL);
	put16(1);                            /* WAVE_FORMAT_PCM */
	put16(1);
	put32(RECORDER_SOUND_RATE);
	put32(RECORDER_SOUND_RATE * 2);
	put16(2);
	put16(16);
	put16(0);
	end(strf2);
	super_index(&sound, "01wb");
	end(audio);

	const int64_t odml = begin("LIST", "odml");
	const int64_t dmlh = begin("dmlh", NULL);
	dmlh_frames_at = file_tell(file);
	put32(0);                            /* dwTotalFrames: all parts' */
	for (int i = 0; i < 61; i++)
		put32(0);
	end(dmlh);
	end(odml);
	end(hdrl);

	movi_at = begin("LIST", "movi") + 4;
	return 1;
}

int recorder_on(void)
{
	return file != NULL;
}

uint32_t recorder_frames(void)
{
	return picture.length;
}

void recorder_sample(int16_t sample)
{
	if (file)
		resampler_push(&converter, sample);
}

static void chunk(stream *s, const char *id, int is_sound, const void *data, uint32_t size, uint32_t length)
{
	const int64_t at = file_tell(file);
	word(id);
	put32(size);
	fwrite(data, 1, size, file);
	if (size & 1)
		fputc(0, file);
	add(&s->entries, &s->count, &s->capacity, (uint32_t)(at + 8 - movi_at), size);
	if (part == 0)
	{
		add(&old_index, &old_count, &old_capacity, (uint32_t)(at - movi_at), size);
		old_index_ids = realloc(old_index_ids, (size_t)old_capacity);
		old_index_ids[old_count - 1] = (uint8_t)is_sound;
	}
	s->length += length;
	s->part_length += length;
}

void recorder_frame(const uint32_t *pixels)
{
	if (!file)
		return;
	if (file_tell(file) - riff_at > PART_LIMIT)
	{
		if (part == 0)
			first_part_frames = picture.length;
		end_part();
		begin_part();
	}
	/* bottom-up, B G R */
	for (int y = 0; y < HEIGHT; y++)
	{
		const uint32_t *in = pixels + (HEIGHT - 1 - y) * WIDTH;
		uint8_t *out = row_buffer + y * WIDTH * 3;
		for (int x = 0; x < WIDTH; x++)
		{
			out[3 * x] = (uint8_t)in[x];
			out[3 * x + 1] = (uint8_t)(in[x] >> 8);
			out[3 * x + 2] = (uint8_t)(in[x] >> 16);
		}
	}
	chunk(&picture, "00db", 0, row_buffer, PICTURE_BYTES, 1);
	/* the sound up to this VBLANK, at a fixed step: 125000 / 48000 (the board's time, no host clock in it) */
	const int count = resampler_pull(&converter, (double)GNG_SOUND_RATE / RECORDER_SOUND_RATE, 1.0f, samples,
		(int)(sizeof(samples) / sizeof(samples[0])));
	if (count)
	{
		for (int i = 0; i < count; i++)
		{
			sample_bytes[2 * i] = (uint8_t)samples[i];
			sample_bytes[2 * i + 1] = (uint8_t)((uint16_t)samples[i] >> 8);
		}
		chunk(&sound, "01wb", 1, sample_bytes, (uint32_t)count * 2, (uint32_t)count);
	}
}

void recorder_close(void)
{
	if (!file)
		return;
	if (part == 0)
		first_part_frames = picture.length;
	end_part();
	write_super_index(&picture);
	write_super_index(&sound);
	patch32(picture.length_at, picture.length);
	patch32(sound.length_at, sound.length);
	patch32(avih_frames_at, first_part_frames);
	patch32(dmlh_frames_at, picture.length);
	fclose(file);
	file = NULL;
	free(picture.entries);
	free(sound.entries);
	free(old_index);
	free(old_index_ids);
	picture.entries = sound.entries = old_index = NULL;
	old_index_ids = NULL;
	old_capacity = 0;
}
