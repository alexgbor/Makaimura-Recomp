/**
 *    Name:        inflate.c
 *    Description: DEFLATE (RFC 1951) decompression and CRC-32 (gng/inflate.h)
 *    Version:     0.1.0
 *    Created:     19-11-2026
 *
 *    license:BSD-3-Clause
 *    From RFC 1951: the three kinds of block (stored, fixed codes, dynamic codes), each Huffman code canonical, so
 *    known by how many codes it has of each length and its symbols in code order; a code read a bit at a time, its
 *    first bit the highest. Speed does not matter: it runs once, as the game starts, on a few hundred KB.
 */
#include "gng/inflate.h"

#define MAX_BITS     15
#define MAX_LITERALS 288
#define MAX_LENGTHS  (286 + 30)

typedef struct stream
{
	const uint8_t *in;
	size_t in_size, in_at;
	uint32_t bit_buffer;
	int bit_count;
	uint8_t *out;
	size_t out_size, out_at;
	int broken;
} stream;

typedef struct huffman
{
	uint16_t count[MAX_BITS + 1];       /* the codes of each length */
	uint16_t symbol[MAX_LITERALS];      /* the symbols in code order */
} huffman;

static const uint16_t length_base[29] = {
	3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258 };
static const uint8_t length_extra[29] = {
	0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0 };
static const uint16_t distance_base[30] = {
	1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097,
	6145, 8193, 12289, 16385, 24577 };
static const uint8_t distance_extra[30] = {
	0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13 };
/* the order the code lengths' code lengths come in */
static const uint8_t length_order[19] = { 16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15 };

/* 'n' bits, the first one lowest */
static uint32_t bits(stream *s, int n)
{
	while (s->bit_count < n)
	{
		if (s->in_at >= s->in_size)
		{
			s->broken = 1;
			return 0;
		}
		s->bit_buffer |= (uint32_t)s->in[s->in_at++] << s->bit_count;
		s->bit_count += 8;
	}
	const uint32_t value = s->bit_buffer & ((1u << n) - 1u);
	s->bit_buffer >>= n;
	s->bit_count -= n;
	return value;
}

/* The code of 'n' symbols with these lengths (0: not used); 0 if it has more codes than its lengths allow */
static int build(huffman *h, const uint8_t *lengths, int n)
{
	uint16_t offset[MAX_BITS + 1];
	for (int length = 0; length <= MAX_BITS; length++)
		h->count[length] = 0;
	for (int i = 0; i < n; i++)
		h->count[lengths[i]]++;
	int left = 1;
	for (int length = 1; length <= MAX_BITS; length++)
	{
		left = left * 2 - h->count[length];
		if (left < 0)
			return 0;
	}
	offset[1] = 0;
	for (int length = 1; length < MAX_BITS; length++)
		offset[length + 1] = (uint16_t)(offset[length] + h->count[length]);
	for (int i = 0; i < n; i++)
		if (lengths[i])
			h->symbol[offset[lengths[i]]++] = (uint16_t)i;
	return 1;
}

/* The next symbol in code 'h', or -1 */
static int decode(stream *s, const huffman *h)
{
	int code = 0, first = 0, index = 0;
	for (int length = 1; length <= MAX_BITS; length++)
	{
		code |= (int)bits(s, 1);
		const int count = h->count[length];
		if (code - first < count)
			return h->symbol[index + code - first];
		index += count;
		first = (first + count) << 1;
		code <<= 1;
		if (s->broken)
			return -1;
	}
	return -1;
}

static void put(stream *s, uint8_t value)
{
	if (s->out_at >= s->out_size)
		s->broken = 1;
	else
		s->out[s->out_at++] = value;
}

static void stored(stream *s)
{
	s->bit_buffer = 0;    /* (the rest of the byte) */
	s->bit_count = 0;
	if (s->in_at + 4 > s->in_size)
	{
		s->broken = 1;
		return;
	}
	const unsigned length = s->in[s->in_at] | s->in[s->in_at + 1] << 8;
	const unsigned complement = s->in[s->in_at + 2] | s->in[s->in_at + 3] << 8;
	s->in_at += 4;
	if ((length ^ 0xFFFFu) != complement || s->in_at + length > s->in_size)
	{
		s->broken = 1;
		return;
	}
	for (unsigned i = 0; i < length && !s->broken; i++)
		put(s, s->in[s->in_at++]);
}

static void compressed(stream *s, const huffman *literals, const huffman *distances)
{
	while (!s->broken)
	{
		int symbol = decode(s, literals);
		if (symbol < 0)
			break;
		if (symbol < 256)
		{
			put(s, (uint8_t)symbol);
			continue;
		}
		if (symbol == 256)
			return;
		symbol -= 257;
		if (symbol >= 29)
			break;
		const unsigned length = length_base[symbol] + bits(s, length_extra[symbol]);
		const int code = decode(s, distances);
		if (code < 0 || code >= 30)
			break;
		const size_t distance = distance_base[code] + bits(s, distance_extra[code]);
		if (distance > s->out_at)
			break;
		for (unsigned i = 0; i < length && !s->broken; i++)
			put(s, s->out[s->out_at - distance]);
	}
	s->broken = 1;
}

static void fixed(stream *s)
{
	static huffman literals, distances;
	static int built;
	if (!built)
	{
		uint8_t lengths[MAX_LITERALS];
		int i = 0;
		for (; i < 144; i++) lengths[i] = 8;
		for (; i < 256; i++) lengths[i] = 9;
		for (; i < 280; i++) lengths[i] = 7;
		for (; i < 288; i++) lengths[i] = 8;
		build(&literals, lengths, 288);
		for (i = 0; i < 30; i++) lengths[i] = 5;
		build(&distances, lengths, 30);
		built = 1;
	}
	compressed(s, &literals, &distances);
}

static void dynamic(stream *s)
{
	uint8_t lengths[MAX_LENGTHS];
	huffman code_lengths, literals, distances;
	const int literal_count = (int)bits(s, 5) + 257;
	const int distance_count = (int)bits(s, 5) + 1;
	const int length_count = (int)bits(s, 4) + 4;
	if (literal_count > 286 || distance_count > 30)
	{
		s->broken = 1;
		return;
	}
	for (int i = 0; i < 19; i++)
		lengths[length_order[i]] = (uint8_t)(i < length_count ? bits(s, 3) : 0);
	if (!build(&code_lengths, lengths, 19))
	{
		s->broken = 1;
		return;
	}
	const int total = literal_count + distance_count;
	int at = 0;
	while (at < total && !s->broken)
	{
		const int symbol = decode(s, &code_lengths);
		if (symbol < 0)
		{
			s->broken = 1;
			return;
		}
		if (symbol < 16)
		{
			lengths[at++] = (uint8_t)symbol;
			continue;
		}
		uint8_t value = 0;
		int repeat;
		if (symbol == 16)
		{
			if (at == 0)
			{
				s->broken = 1;
				return;
			}
			value = lengths[at - 1];
			repeat = 3 + (int)bits(s, 2);
		}
		else if (symbol == 17)
			repeat = 3 + (int)bits(s, 3);
		else
			repeat = 11 + (int)bits(s, 7);
		if (at + repeat > total)
		{
			s->broken = 1;
			return;
		}
		while (repeat--)
			lengths[at++] = value;
	}
	if (s->broken || lengths[256] == 0 || !build(&literals, lengths, literal_count) ||
		!build(&distances, lengths + literal_count, distance_count))
	{
		s->broken = 1;
		return;
	}
	compressed(s, &literals, &distances);
}

long gng_inflate(const uint8_t *in, size_t in_size, uint8_t *out, size_t out_size)
{
	stream s = { 0 };
	s.in = in;
	s.in_size = in_size;
	s.out = out;
	s.out_size = out_size;
	int last;
	do
	{
		last = (int)bits(&s, 1);
		switch (bits(&s, 2))
		{
		case 0: stored(&s); break;
		case 1: fixed(&s); break;
		case 2: dynamic(&s); break;
		default: s.broken = 1; break;
		}
	} while (!last && !s.broken);
	return s.broken ? -1 : (long)s.out_at;
}

uint32_t gng_crc32(const uint8_t *data, size_t size)
{
	static uint32_t table[256];
	if (!table[1])
		for (uint32_t i = 0; i < 256; i++)
		{
			uint32_t c = i;
			for (int k = 0; k < 8; k++)
				c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
			table[i] = c;
		}
	uint32_t crc = 0xFFFFFFFFu;
	for (size_t i = 0; i < size; i++)
		crc = table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
	return crc ^ 0xFFFFFFFFu;
}
