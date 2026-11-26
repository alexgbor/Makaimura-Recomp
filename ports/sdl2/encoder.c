/**
 *    Name:        encoder.c
 *    Description: a video file for sharing: H.264 and AAC in an MP4 (encoder.h)
 *    Version:     0.1.0
 *    Created:     26-11-2026
 *
 *    license:BSD-3-Clause
 *    Media Foundation (Windows), after Microsoft's "Tutorial: Using the Sink Writer to Encode Video": the sink
 *    writer given RGB32 pictures and PCM sound, H.264 (High profile, BT.709) and AAC (192 kb/s) out, its own
 *    converters in between. ffmpeg (elsewhere, or as the fallback): its rawvideo input from a pipe (bgr0: the bytes
 *    of a little-endian 0x00RRGGBB), the first of libx264, libopenh264, libvpx-vp9, mpeg4 it has.
 *    A frame's time is exact in 100 ns units (Media Foundation's): count x den x 10^7 / num, in integers.
 */
#include "encoder.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#define COBJMACROS
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <codecapi.h>
#define open_pipe _popen
#define close_pipe _pclose
#else
#include <signal.h>
#define open_pipe popen
#define close_pipe pclose
#endif

struct encoder
{
	int width, height;
	unsigned num, den, sample_rate;
	uint64_t frames, samples;
	int failed;
#ifdef _WIN32
	IMFSinkWriter *writer;
	DWORD video, audio;
	int com, mf;
#endif
	/* ffmpeg's */
	FILE *pipe, *wav;
	char ffmpeg[1024], path[1300], video_path[1320], wav_path[1320];
};

static void say(char *why, size_t size, const char *text)
{
	if (why && size)
		snprintf(why, size, "%s", text);
}

/* the frame's start in 100 ns units */
static int64_t frame_time(const encoder *e, uint64_t frame)
{
	const uint64_t q = (uint64_t)e->den * 10000000u;
	return (int64_t)(frame / e->num * q + frame % e->num * q / e->num);
}

/* ---- Media Foundation ---- */

#ifdef _WIN32
static int mf_open(encoder *e, unsigned bitrate, char *why, size_t why_size)
{
	HRESULT hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
	e->com = SUCCEEDED(hr);
	hr = MFStartup(MF_VERSION, MFSTARTUP_LITE);
	if (FAILED(hr))
	{
		say(why, why_size, "Media Foundation is not there");
		return 0;
	}
	e->mf = 1;
	wchar_t wide[1300];
	if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, e->path, -1, wide, 1300)
		&& !MultiByteToWideChar(CP_ACP, 0, e->path, -1, wide, 1300))
		return 0;
	IMFAttributes *attributes = NULL;
	MFCreateAttributes(&attributes, 1);
	if (attributes)
		IMFAttributes_SetUINT32(attributes, &MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS, TRUE);
	hr = MFCreateSinkWriterFromURL(wide, NULL, attributes, &e->writer);
	if (attributes)
		IMFAttributes_Release(attributes);
	if (FAILED(hr))
	{
		say(why, why_size, "the MP4 could not be made");
		return 0;
	}

	const UINT64 size = (UINT64)e->width << 32 | (UINT32)e->height;
	const UINT64 rate = (UINT64)e->num << 32 | e->den;
	const UINT64 square = (UINT64)1 << 32 | 1;
	IMFMediaType *out = NULL, *in = NULL;
	/* the picture: H.264 High, BT.709 (an HD picture's), progressive */
	MFCreateMediaType(&out);
	IMFMediaType_SetGUID(out, &MF_MT_MAJOR_TYPE, &MFMediaType_Video);
	IMFMediaType_SetGUID(out, &MF_MT_SUBTYPE, &MFVideoFormat_H264);
	IMFMediaType_SetUINT32(out, &MF_MT_AVG_BITRATE, bitrate);
	IMFMediaType_SetUINT32(out, &MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
	IMFMediaType_SetUINT32(out, &MF_MT_MPEG2_PROFILE, eAVEncH264VProfile_High);
	IMFMediaType_SetUINT64(out, &MF_MT_FRAME_SIZE, size);
	IMFMediaType_SetUINT64(out, &MF_MT_FRAME_RATE, rate);
	IMFMediaType_SetUINT64(out, &MF_MT_PIXEL_ASPECT_RATIO, square);
	IMFMediaType_SetUINT32(out, &MF_MT_YUV_MATRIX, MFVideoTransferMatrix_BT709);
	IMFMediaType_SetUINT32(out, &MF_MT_VIDEO_PRIMARIES, MFVideoPrimaries_BT709);
	IMFMediaType_SetUINT32(out, &MF_MT_TRANSFER_FUNCTION, MFVideoTransFunc_709);
	IMFMediaType_SetUINT32(out, &MF_MT_VIDEO_NOMINAL_RANGE, MFNominalRange_16_235);
	hr = IMFSinkWriter_AddStream(e->writer, out, &e->video);
	IMFMediaType_Release(out);
	if (SUCCEEDED(hr))
	{
		MFCreateMediaType(&in);
		IMFMediaType_SetGUID(in, &MF_MT_MAJOR_TYPE, &MFMediaType_Video);
		IMFMediaType_SetGUID(in, &MF_MT_SUBTYPE, &MFVideoFormat_RGB32);
		IMFMediaType_SetUINT32(in, &MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
		IMFMediaType_SetUINT64(in, &MF_MT_FRAME_SIZE, size);
		IMFMediaType_SetUINT64(in, &MF_MT_FRAME_RATE, rate);
		IMFMediaType_SetUINT64(in, &MF_MT_PIXEL_ASPECT_RATIO, square);
		IMFMediaType_SetUINT32(in, &MF_MT_DEFAULT_STRIDE, (UINT32)(e->width * 4));    /* (top row first) */
		IMFMediaType_SetUINT32(in, &MF_MT_YUV_MATRIX, MFVideoTransferMatrix_BT709);
		IMFMediaType_SetUINT32(in, &MF_MT_VIDEO_NOMINAL_RANGE, MFNominalRange_0_255);
		hr = IMFSinkWriter_SetInputMediaType(e->writer, e->video, in, NULL);
		IMFMediaType_Release(in);
	}
	if (FAILED(hr))
	{
		say(why, why_size, "no H.264 encoder for this size");
		return 0;
	}
	/* the sound: AAC at 192 kb/s from 16-bit PCM */
	MFCreateMediaType(&out);
	IMFMediaType_SetGUID(out, &MF_MT_MAJOR_TYPE, &MFMediaType_Audio);
	IMFMediaType_SetGUID(out, &MF_MT_SUBTYPE, &MFAudioFormat_AAC);
	IMFMediaType_SetUINT32(out, &MF_MT_AUDIO_BITS_PER_SAMPLE, 16);
	IMFMediaType_SetUINT32(out, &MF_MT_AUDIO_SAMPLES_PER_SECOND, e->sample_rate);
	IMFMediaType_SetUINT32(out, &MF_MT_AUDIO_NUM_CHANNELS, 1);
	IMFMediaType_SetUINT32(out, &MF_MT_AUDIO_AVG_BYTES_PER_SECOND, 24000);
	hr = IMFSinkWriter_AddStream(e->writer, out, &e->audio);
	IMFMediaType_Release(out);
	if (SUCCEEDED(hr))
	{
		MFCreateMediaType(&in);
		IMFMediaType_SetGUID(in, &MF_MT_MAJOR_TYPE, &MFMediaType_Audio);
		IMFMediaType_SetGUID(in, &MF_MT_SUBTYPE, &MFAudioFormat_PCM);
		IMFMediaType_SetUINT32(in, &MF_MT_AUDIO_BITS_PER_SAMPLE, 16);
		IMFMediaType_SetUINT32(in, &MF_MT_AUDIO_SAMPLES_PER_SECOND, e->sample_rate);
		IMFMediaType_SetUINT32(in, &MF_MT_AUDIO_NUM_CHANNELS, 1);
		IMFMediaType_SetUINT32(in, &MF_MT_AUDIO_BLOCK_ALIGNMENT, 2);
		IMFMediaType_SetUINT32(in, &MF_MT_AUDIO_AVG_BYTES_PER_SECOND, e->sample_rate * 2);
		hr = IMFSinkWriter_SetInputMediaType(e->writer, e->audio, in, NULL);
		IMFMediaType_Release(in);
	}
	if (FAILED(hr))
	{
		say(why, why_size, "no AAC encoder");
		return 0;
	}
	if (FAILED(IMFSinkWriter_BeginWriting(e->writer)))
	{
		say(why, why_size, "the encoders did not start");
		return 0;
	}
	return 1;
}

static void mf_end(encoder *e, int finish)
{
	if (e->writer)
	{
		if (finish && FAILED(IMFSinkWriter_Finalize(e->writer)))
			e->failed = 1;
		IMFSinkWriter_Release(e->writer);
		e->writer = NULL;
	}
	if (e->mf)
		MFShutdown();
	if (e->com)
		CoUninitialize();
	e->mf = e->com = 0;
}

static int mf_write(encoder *e, DWORD stream, const void *data, DWORD size, int64_t time, int64_t duration)
{
	IMFMediaBuffer *buffer = NULL;
	IMFSample *sample = NULL;
	BYTE *at = NULL;
	HRESULT hr = MFCreateMemoryBuffer(size, &buffer);
	if (SUCCEEDED(hr))
		hr = IMFMediaBuffer_Lock(buffer, &at, NULL, NULL);
	if (SUCCEEDED(hr))
	{
		memcpy(at, data, size);    /* (a little-endian 0x00RRGGBB is RGB32's B G R X; PCM as it is) */
		IMFMediaBuffer_Unlock(buffer);
		hr = IMFMediaBuffer_SetCurrentLength(buffer, size);
	}
	if (SUCCEEDED(hr))
		hr = MFCreateSample(&sample);
	if (SUCCEEDED(hr))
		hr = IMFSample_AddBuffer(sample, buffer);
	if (SUCCEEDED(hr))
		hr = IMFSample_SetSampleTime(sample, time);
	if (SUCCEEDED(hr))
		hr = IMFSample_SetSampleDuration(sample, duration);
	if (SUCCEEDED(hr))
		hr = IMFSinkWriter_WriteSample(e->writer, stream, sample);
	if (sample)
		IMFSample_Release(sample);
	if (buffer)
		IMFMediaBuffer_Release(buffer);
	return SUCCEEDED(hr);
}
#endif

/* ---- ffmpeg ---- */

static void put16(FILE *f, unsigned v) { fputc((int)(v & 255), f); fputc((int)(v >> 8 & 255), f); }
static void put32(FILE *f, uint32_t v) { put16(f, v & 0xFFFF); put16(f, v >> 16); }

static void wav_header(FILE *f, unsigned rate, uint32_t bytes)
{
	fwrite("RIFF", 1, 4, f);
	put32(f, 36 + bytes);
	fwrite("WAVEfmt ", 1, 8, f);
	put32(f, 16);
	put16(f, 1);
	put16(f, 1);
	put32(f, rate);
	put32(f, rate * 2);
	put16(f, 2);
	put16(f, 16);
	fwrite("data", 1, 4, f);
	put32(f, bytes);
}

/* the command's line through the shell: Windows' cmd.exe wants the whole of it quoted once more */
static FILE *run(const char *command, const char *mode)
{
#ifdef _WIN32
	char quoted[8300];
	snprintf(quoted, sizeof(quoted), "\"%s\"", command);
	return open_pipe(quoted, mode);
#else
	return open_pipe(command, mode);
#endif
}

static int ffmpeg_open(encoder *e, unsigned bitrate, char *why, size_t why_size)
{
	const char *named = getenv("MAKAIMURA_FFMPEG");
	snprintf(e->ffmpeg, sizeof(e->ffmpeg), "%s", named && *named ? named : "ffmpeg");
	/* the encoders it has */
	char command[8192], line[512];
	snprintf(command, sizeof(command), "\"%s\" -hide_banner -encoders 2>&1", e->ffmpeg);
	FILE *list = run(command, "r");
	int has[4] = { 0 };
	static const char *const codecs[4] = { "libx264", "libopenh264", "libvpx-vp9", "mpeg4" };
	if (list)
	{
		while (fgets(line, sizeof(line), list))
			for (int i = 0; i < 4; i++)
			{
				const char *at = strstr(line, codecs[i]);
				const size_t n = strlen(codecs[i]);
				if (at && at > line && at[-1] == ' ' && (at[n] == ' ' || at[n] == '\n' || at[n] == '\r'))
					has[i] = 1;
			}
		close_pipe(list);
	}
	int codec = 0;
	while (codec < 4 && !has[codec])
		codec++;
	if (codec == 4)
	{
		say(why, why_size, "no ffmpeg found to encode the video (install it, or set MAKAIMURA_FFMPEG)");
		return 0;
	}
	char options[256];
	if (codec == 0)
		snprintf(options, sizeof(options), "-c:v libx264 -preset veryfast -crf 18 -maxrate %u -bufsize %u", bitrate,
			2 * bitrate);
	else
		snprintf(options, sizeof(options), "-c:v %s -b:v %u", codecs[codec], bitrate);
	snprintf(e->video_path, sizeof(e->video_path), "%s.video.mp4", e->path);
	snprintf(e->wav_path, sizeof(e->wav_path), "%s.sound.wav", e->path);
	if (!(e->wav = fopen(e->wav_path, "wb")))
	{
		say(why, why_size, "the sound's file could not be made");
		return 0;
	}
	wav_header(e->wav, e->sample_rate, 0);
#ifndef _WIN32
	signal(SIGPIPE, SIG_IGN);    /* (ffmpeg gone: the writes fail, the program goes on) */
#endif
	snprintf(command, sizeof(command),
		"\"%s\" -hide_banner -loglevel error -y -f rawvideo -pix_fmt bgr0 -s %dx%d -framerate %u/%u -i - "
		"-vf scale=out_color_matrix=bt709:out_range=tv %s -pix_fmt yuv420p -colorspace bt709 -color_primaries bt709 "
		"-color_trc bt709 -color_range tv \"%s\"",
		e->ffmpeg, e->width, e->height, e->num, e->den, options, e->video_path);
#ifdef _WIN32
	e->pipe = run(command, "wb");
#else
	e->pipe = run(command, "w");    /* (glibc's popen takes no "b") */
#endif
	if (!e->pipe)
	{
		say(why, why_size, "ffmpeg did not start");
		return 0;
	}
	return 1;
}

static int ffmpeg_close(encoder *e)
{
	int ok = !e->failed;
	if (e->pipe && close_pipe(e->pipe) != 0)
		ok = 0;
	e->pipe = NULL;
	if (e->wav)
	{
		fseek(e->wav, 0, SEEK_SET);
		wav_header(e->wav, e->sample_rate, (uint32_t)(e->samples * 2));
		fclose(e->wav);
		e->wav = NULL;
	}
	if (ok)
	{
		char command[8192];
		snprintf(command, sizeof(command),
			"\"%s\" -hide_banner -loglevel error -y -i \"%s\" -i \"%s\" -map 0:v -map 1:a -c:v copy -c:a aac "
			"-b:a 192k -movflags +faststart \"%s\"", e->ffmpeg, e->video_path, e->wav_path, e->path);
		FILE *mux = run(command, "r");
		char line[256];
		if (!mux)
			ok = 0;
		else
		{
			while (fgets(line, sizeof(line), mux))
				;
			ok = close_pipe(mux) == 0;
		}
	}
	remove(e->video_path);
	remove(e->wav_path);
	return ok;
}

/* ---- either ---- */

encoder *encoder_open(const char *path, int width, int height, unsigned num, unsigned den, unsigned bitrate,
	unsigned sample_rate, char *why, size_t why_size)
{
	encoder *e = calloc(1, sizeof(encoder));
	if (!e)
		return NULL;
	e->width = width;
	e->height = height;
	e->num = num;
	e->den = den;
	e->sample_rate = sample_rate;
	snprintf(e->path, sizeof(e->path), "%s", path);
	say(why, why_size, "");
#ifdef _WIN32
	const char *named = getenv("MAKAIMURA_FFMPEG");
	if (!(named && *named))
	{
		if (mf_open(e, bitrate, why, why_size))
			return e;
		mf_end(e, 0);
		remove(path);
	}
#endif
	if (ffmpeg_open(e, bitrate, why, why_size))
		return e;
	if (e->wav)
		fclose(e->wav);
	remove(e->wav_path);
	free(e);
	return NULL;
}

const char *encoder_name(const encoder *e)
{
#ifdef _WIN32
	if (e->writer)
		return "Media Foundation";
#endif
	(void)e;
	return "ffmpeg";
}

int encoder_picture(encoder *e, const uint32_t *pixels)
{
	if (e->failed)
		return 0;
	const size_t bytes = (size_t)e->width * (size_t)e->height * 4;
#ifdef _WIN32
	if (e->writer)
	{
		const int64_t time = frame_time(e, e->frames);
		if (!mf_write(e, e->video, pixels, (DWORD)bytes, time, frame_time(e, e->frames + 1) - time))
			e->failed = 1;
		e->frames++;
		return !e->failed;
	}
#endif
	/* (bgr0: the little-endian 0x00RRGGBB's bytes as they are) */
	if (fwrite(pixels, 1, bytes, e->pipe) != bytes)
		e->failed = 1;
	e->frames++;
	return !e->failed;
}

int encoder_sound(encoder *e, const int16_t *samples, int count)
{
	if (e->failed || count <= 0)
		return !e->failed;
#ifdef _WIN32
	if (e->writer)
	{
		const int64_t time = (int64_t)(e->samples * 10000000u / e->sample_rate);
		const int64_t next = (int64_t)((e->samples + (uint64_t)count) * 10000000u / e->sample_rate);
		if (!mf_write(e, e->audio, samples, (DWORD)count * 2, time, next - time))
			e->failed = 1;
		e->samples += (uint64_t)count;
		return !e->failed;
	}
#endif
	for (int i = 0; i < count; i++)
		put16(e->wav, (uint16_t)samples[i]);
	e->samples += (uint64_t)count;
	return 1;
}

int encoder_close(encoder *e)
{
	if (!e)
		return 0;
	int ok;
#ifdef _WIN32
	if (e->writer)
	{
		mf_end(e, 1);
		ok = !e->failed;
		free(e);
		return ok;
	}
#endif
	ok = ffmpeg_close(e);
	free(e);
	return ok;
}
