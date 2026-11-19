/**
 *    Name:        ym2203.c
 *    Description: the YM2203 (gng/ym2203.h) as MAME 0.289 makes it with ymfm, at the medium fidelity MAME sets
 *    Version:     0.1.0
 *    Created:     19-11-2026
 *
 *    license:BSD-3-Clause
 *    After Aaron Giles's ymfm (BSD-3-Clause), followed function by function for what the YM2203 uses: ymfm_fm.ipp
 *    (abs_sin_attenuation, attenuation_to_volume, attenuation_increment, detune_adjustment; fm_operator,
 *    fm_channel::output_4op, fm_engine_base's clock, output and write), ymfm_opn.cpp (opn_registers_base<false>:
 *    write, cache_operator_data, compute_phase_step; ssg_resampler; ym2203: write_address, write_data, generate,
 *    update_prescale, clock_fm), ymfm_ssg.cpp (ssg_engine), ymfm.h (roundtrip_fp). The C keeps ymfm's types where
 *    they decide a result (the 16-bit envelope, the 16-bit operator outputs). ymfm's own code stays in the
 *    verification tools (tools/reference/mame/ymfm); tools/reference/sound/ymcheck.cpp compares the two.
 */
#include "gng/ym2203.h"

#include <string.h>

enum { EG_DEPRESS, EG_ATTACK, EG_DECAY, EG_SUSTAIN, EG_RELEASE, EG_REVERB };
enum { KEYON_NORMAL = 0, KEYON_RHYTHM = 1, KEYON_CSM = 2 };
#define EG_QUIET 0x380u
#define ALL_CHANNELS 7u

static inline uint32_t bits(uint32_t value, int start, int length)
{
	return (value >> start) & ((1u << length) - 1u);
}

static inline uint32_t bit(uint32_t value, int start)
{
	return (value >> start) & 1u;
}

/* ---- the tables (ymfm_fm.ipp) ---- */

static const uint16_t sin_table[256] =
{
	0x859,0x6c3,0x607,0x58b,0x52e,0x4e4,0x4a6,0x471,0x443,0x41a,0x3f5,0x3d3,0x3b5,0x398,0x37e,0x365,
	0x34e,0x339,0x324,0x311,0x2ff,0x2ed,0x2dc,0x2cd,0x2bd,0x2af,0x2a0,0x293,0x286,0x279,0x26d,0x261,
	0x256,0x24b,0x240,0x236,0x22c,0x222,0x218,0x20f,0x206,0x1fd,0x1f5,0x1ec,0x1e4,0x1dc,0x1d4,0x1cd,
	0x1c5,0x1be,0x1b7,0x1b0,0x1a9,0x1a2,0x19b,0x195,0x18f,0x188,0x182,0x17c,0x177,0x171,0x16b,0x166,
	0x160,0x15b,0x155,0x150,0x14b,0x146,0x141,0x13c,0x137,0x133,0x12e,0x129,0x125,0x121,0x11c,0x118,
	0x114,0x10f,0x10b,0x107,0x103,0x0ff,0x0fb,0x0f8,0x0f4,0x0f0,0x0ec,0x0e9,0x0e5,0x0e2,0x0de,0x0db,
	0x0d7,0x0d4,0x0d1,0x0cd,0x0ca,0x0c7,0x0c4,0x0c1,0x0be,0x0bb,0x0b8,0x0b5,0x0b2,0x0af,0x0ac,0x0a9,
	0x0a7,0x0a4,0x0a1,0x09f,0x09c,0x099,0x097,0x094,0x092,0x08f,0x08d,0x08a,0x088,0x086,0x083,0x081,
	0x07f,0x07d,0x07a,0x078,0x076,0x074,0x072,0x070,0x06e,0x06c,0x06a,0x068,0x066,0x064,0x062,0x060,
	0x05e,0x05c,0x05b,0x059,0x057,0x055,0x053,0x052,0x050,0x04e,0x04d,0x04b,0x04a,0x048,0x046,0x045,
	0x043,0x042,0x040,0x03f,0x03e,0x03c,0x03b,0x039,0x038,0x037,0x035,0x034,0x033,0x031,0x030,0x02f,
	0x02e,0x02d,0x02b,0x02a,0x029,0x028,0x027,0x026,0x025,0x024,0x023,0x022,0x021,0x020,0x01f,0x01e,
	0x01d,0x01c,0x01b,0x01a,0x019,0x018,0x017,0x017,0x016,0x015,0x014,0x014,0x013,0x012,0x011,0x011,
	0x010,0x00f,0x00f,0x00e,0x00d,0x00d,0x00c,0x00c,0x00b,0x00a,0x00a,0x009,0x009,0x008,0x008,0x007,
	0x007,0x007,0x006,0x006,0x005,0x005,0x005,0x004,0x004,0x004,0x003,0x003,0x003,0x002,0x002,0x002,
	0x002,0x001,0x001,0x001,0x001,0x001,0x001,0x001,0x000,0x000,0x000,0x000,0x000,0x000,0x000,0x000
};

#define X(a) (((a) | 0x400) << 2)
static const uint16_t power_table[256] =
{
	X(0x3fa),X(0x3f5),X(0x3ef),X(0x3ea),X(0x3e4),X(0x3df),X(0x3da),X(0x3d4),
	X(0x3cf),X(0x3c9),X(0x3c4),X(0x3bf),X(0x3b9),X(0x3b4),X(0x3ae),X(0x3a9),
	X(0x3a4),X(0x39f),X(0x399),X(0x394),X(0x38f),X(0x38a),X(0x384),X(0x37f),
	X(0x37a),X(0x375),X(0x370),X(0x36a),X(0x365),X(0x360),X(0x35b),X(0x356),
	X(0x351),X(0x34c),X(0x347),X(0x342),X(0x33d),X(0x338),X(0x333),X(0x32e),
	X(0x329),X(0x324),X(0x31f),X(0x31a),X(0x315),X(0x310),X(0x30b),X(0x306),
	X(0x302),X(0x2fd),X(0x2f8),X(0x2f3),X(0x2ee),X(0x2e9),X(0x2e5),X(0x2e0),
	X(0x2db),X(0x2d6),X(0x2d2),X(0x2cd),X(0x2c8),X(0x2c4),X(0x2bf),X(0x2ba),
	X(0x2b5),X(0x2b1),X(0x2ac),X(0x2a8),X(0x2a3),X(0x29e),X(0x29a),X(0x295),
	X(0x291),X(0x28c),X(0x288),X(0x283),X(0x27f),X(0x27a),X(0x276),X(0x271),
	X(0x26d),X(0x268),X(0x264),X(0x25f),X(0x25b),X(0x257),X(0x252),X(0x24e),
	X(0x249),X(0x245),X(0x241),X(0x23c),X(0x238),X(0x234),X(0x230),X(0x22b),
	X(0x227),X(0x223),X(0x21e),X(0x21a),X(0x216),X(0x212),X(0x20e),X(0x209),
	X(0x205),X(0x201),X(0x1fd),X(0x1f9),X(0x1f5),X(0x1f0),X(0x1ec),X(0x1e8),
	X(0x1e4),X(0x1e0),X(0x1dc),X(0x1d8),X(0x1d4),X(0x1d0),X(0x1cc),X(0x1c8),
	X(0x1c4),X(0x1c0),X(0x1bc),X(0x1b8),X(0x1b4),X(0x1b0),X(0x1ac),X(0x1a8),
	X(0x1a4),X(0x1a0),X(0x19c),X(0x199),X(0x195),X(0x191),X(0x18d),X(0x189),
	X(0x185),X(0x181),X(0x17e),X(0x17a),X(0x176),X(0x172),X(0x16f),X(0x16b),
	X(0x167),X(0x163),X(0x160),X(0x15c),X(0x158),X(0x154),X(0x151),X(0x14d),
	X(0x149),X(0x146),X(0x142),X(0x13e),X(0x13b),X(0x137),X(0x134),X(0x130),
	X(0x12c),X(0x129),X(0x125),X(0x122),X(0x11e),X(0x11b),X(0x117),X(0x114),
	X(0x110),X(0x10c),X(0x109),X(0x106),X(0x102),X(0x0ff),X(0x0fb),X(0x0f8),
	X(0x0f4),X(0x0f1),X(0x0ed),X(0x0ea),X(0x0e7),X(0x0e3),X(0x0e0),X(0x0dc),
	X(0x0d9),X(0x0d6),X(0x0d2),X(0x0cf),X(0x0cc),X(0x0c8),X(0x0c5),X(0x0c2),
	X(0x0be),X(0x0bb),X(0x0b8),X(0x0b5),X(0x0b1),X(0x0ae),X(0x0ab),X(0x0a8),
	X(0x0a4),X(0x0a1),X(0x09e),X(0x09b),X(0x098),X(0x094),X(0x091),X(0x08e),
	X(0x08b),X(0x088),X(0x085),X(0x082),X(0x07e),X(0x07b),X(0x078),X(0x075),
	X(0x072),X(0x06f),X(0x06c),X(0x069),X(0x066),X(0x063),X(0x060),X(0x05d),
	X(0x05a),X(0x057),X(0x054),X(0x051),X(0x04e),X(0x04b),X(0x048),X(0x045),
	X(0x042),X(0x03f),X(0x03c),X(0x039),X(0x036),X(0x033),X(0x030),X(0x02d),
	X(0x02a),X(0x028),X(0x025),X(0x022),X(0x01f),X(0x01c),X(0x019),X(0x016),
	X(0x014),X(0x011),X(0x00e),X(0x00b),X(0x008),X(0x006),X(0x003),X(0x000)
};
#undef X

static const uint32_t increment_table[64] =
{
	0x00000000, 0x00000000, 0x10101010, 0x10101010, 0x10101010, 0x10101010, 0x11101110, 0x11101110,
	0x10101010, 0x10111010, 0x11101110, 0x11111110, 0x10101010, 0x10111010, 0x11101110, 0x11111110,
	0x10101010, 0x10111010, 0x11101110, 0x11111110, 0x10101010, 0x10111010, 0x11101110, 0x11111110,
	0x10101010, 0x10111010, 0x11101110, 0x11111110, 0x10101010, 0x10111010, 0x11101110, 0x11111110,
	0x10101010, 0x10111010, 0x11101110, 0x11111110, 0x10101010, 0x10111010, 0x11101110, 0x11111110,
	0x10101010, 0x10111010, 0x11101110, 0x11111110, 0x10101010, 0x10111010, 0x11101110, 0x11111110,
	0x11111111, 0x21112111, 0x21212121, 0x22212221, 0x22222222, 0x42224222, 0x42424242, 0x44424442,
	0x44444444, 0x84448444, 0x84848484, 0x88848884, 0x88888888, 0x88888888, 0x88888888, 0x88888888
};

static const uint8_t detune_table[32][4] =
{
	{ 0,  0,  1,  2 },  { 0,  0,  1,  2 },  { 0,  0,  1,  2 },  { 0,  0,  1,  2 },
	{ 0,  1,  2,  2 },  { 0,  1,  2,  3 },  { 0,  1,  2,  3 },  { 0,  1,  2,  3 },
	{ 0,  1,  2,  4 },  { 0,  1,  3,  4 },  { 0,  1,  3,  4 },  { 0,  1,  3,  5 },
	{ 0,  2,  4,  5 },  { 0,  2,  4,  6 },  { 0,  2,  4,  6 },  { 0,  2,  5,  7 },
	{ 0,  2,  5,  8 },  { 0,  3,  6,  8 },  { 0,  3,  6,  9 },  { 0,  3,  7, 10 },
	{ 0,  4,  8, 11 },  { 0,  4,  8, 12 },  { 0,  4,  9, 13 },  { 0,  5, 10, 14 },
	{ 0,  5, 11, 16 },  { 0,  6, 12, 17 },  { 0,  6, 13, 19 },  { 0,  7, 14, 20 },
	{ 0,  8, 16, 22 },  { 0,  8, 16, 22 },  { 0,  8, 16, 22 },  { 0,  8, 16, 22 }
};

static const int16_t ssg_amplitudes[32] =
{
	    0,   32,   78,  141,  178,  222,  262,  306,
	  369,  441,  509,  585,  701,  836,  965, 1112,
	 1334, 1595, 1853, 2146, 2576, 3081, 3576, 4135,
	 5000, 6006, 7023, 8155, 9963,11976,14132,16382
};

static uint16_t waveform[0x400];
static int waveform_ready;

static uint32_t abs_sin_attenuation(uint32_t input)
{
	if (bit(input, 8))
		input = ~input;
	return sin_table[input & 0xff];
}

static uint32_t attenuation_to_volume(uint32_t input)
{
	return (uint32_t)power_table[input & 0xff] >> (input >> 8);
}

static uint32_t attenuation_increment(uint32_t rate, uint32_t index)
{
	return bits(increment_table[rate], (int)(4 * index), 4);
}

static int32_t detune_adjustment(uint32_t detune, uint32_t keycode)
{
	const int32_t result = detune_table[keycode][detune & 3];
	return bit(detune, 2) ? -result : result;
}

static uint32_t effective_rate(uint32_t rawrate, uint32_t ksr)
{
	if (rawrate == 0)
		return 0;
	return rawrate + ksr < 63 ? rawrate + ksr : 63;
}

static int16_t roundtrip_fp(int32_t value)
{
	if (value < -32768)
		return -32768;
	if (value > 32767)
		return 32767;
	const int32_t scanvalue = value ^ (value >> 31);
	uint32_t scan = (uint32_t)scanvalue << 17;
	int zeros = 32;
	while (scan)
	{
		zeros--;
		scan >>= 1;
	}
	int exponent = 7 - zeros;
	if (exponent < 1)
		exponent = 1;
	exponent -= 1;
	const int32_t mask = (1 << exponent) - 1;
	return (int16_t)(value & ~mask);
}

/* ---- the OPN's registers (opn_registers_base<false>) ---- */

static inline uint32_t reg_byte(const ym2203 *c, uint32_t offset, int start, int count, uint32_t extra)
{
	return bits(c->regs[(offset + extra) & 0xFF], start, count);
}

static inline uint32_t reg_word(const ym2203 *c, uint32_t o1, int s1, int c1, uint32_t o2, int s2, int c2, uint32_t extra)
{
	return (reg_byte(c, o1, s1, c1, extra) << c2) | reg_byte(c, o2, s2, c2, extra);
}

static int regs_write(ym2203 *c, uint32_t index, uint8_t data, uint32_t *channel, uint32_t *opmask)
{
	if ((index & 0xf0) == 0xa0)
	{
		if (bits(index, 0, 2) == 3)
			return 0;
		const uint32_t latchindex = 0xb8 | bit(index, 3);
		if (bit(index, 2))
			c->regs[latchindex] = data & 0x3f;
		else
		{
			c->regs[index] = data;
			c->regs[index | 4] = c->regs[latchindex];
		}
		return 0;
	}
	if ((index & 0xf8) == 0xb8)
		return 0;
	c->regs[index] = data;
	if (index == 0x28)
	{
		*channel = bits(data, 0, 2);
		if (*channel == 3)
			return 0;
		*opmask = bits(data, 4, 4);
		return 1;
	}
	return 0;
}

static uint32_t compute_phase_step(const ym2203_operator *op)
{
	const uint32_t fnum = bits(op->block_freq, 0, 11) << 1;
	const uint32_t block = bits(op->block_freq, 11, 3);
	uint32_t phase_step = (fnum << block) >> 2;
	phase_step += (uint32_t)op->detune;
	phase_step &= 0x1ffff;
	return (phase_step * op->multiple) >> 1;
}

static void cache_operator_data(const ym2203 *c, ym2203_operator *op)
{
	const uint32_t choffs = op->choffs, opoffs = op->opoffs;
	uint32_t block_freq = op->block_freq = reg_word(c, 0xa4, 0, 6, 0xa0, 0, 8, choffs);
	if (reg_byte(c, 0x27, 6, 2, 0) != 0 && choffs == 2)
	{
		if (opoffs == 2)
			block_freq = op->block_freq = reg_word(c, 0xac, 0, 6, 0xa8, 0, 8, 1);
		else if (opoffs == 10)
			block_freq = op->block_freq = reg_word(c, 0xac, 0, 6, 0xa8, 0, 8, 2);
		else if (opoffs == 6)
			block_freq = op->block_freq = reg_word(c, 0xac, 0, 6, 0xa8, 0, 8, 0);
	}
	uint32_t keycode = bits(block_freq, 10, 4) << 1;
	keycode |= bit(0xfe80, (int)bits(block_freq, 7, 4));
	op->detune = detune_adjustment(reg_byte(c, 0x30, 4, 3, opoffs), keycode);
	op->multiple = reg_byte(c, 0x30, 0, 4, opoffs) * 2;
	if (op->multiple == 0)
		op->multiple = 1;
	op->phase_step = compute_phase_step(op);
	op->total_level = reg_byte(c, 0x40, 0, 7, opoffs) << 3;
	op->eg_sustain = reg_byte(c, 0x80, 4, 4, opoffs);
	op->eg_sustain |= (op->eg_sustain + 1) & 0x10;
	op->eg_sustain <<= 5;
	const uint32_t ksrval = keycode >> (reg_byte(c, 0x50, 6, 2, opoffs) ^ 3);
	op->eg_rate[EG_ATTACK] = (uint8_t)effective_rate(reg_byte(c, 0x50, 0, 5, opoffs) * 2, ksrval);
	op->eg_rate[EG_DECAY] = (uint8_t)effective_rate(reg_byte(c, 0x60, 0, 5, opoffs) * 2, ksrval);
	op->eg_rate[EG_SUSTAIN] = (uint8_t)effective_rate(reg_byte(c, 0x70, 0, 5, opoffs) * 2, ksrval);
	op->eg_rate[EG_RELEASE] = (uint8_t)effective_rate(reg_byte(c, 0x80, 0, 4, opoffs) * 4 + 2, ksrval);
}

/* ---- the operators (fm_operator) ---- */

static inline uint32_t ssg_eg_enable(const ym2203 *c, const ym2203_operator *op) { return reg_byte(c, 0x90, 3, 1, op->opoffs); }
static inline uint32_t ssg_eg_mode(const ym2203 *c, const ym2203_operator *op) { return reg_byte(c, 0x90, 0, 3, op->opoffs); }

static void op_reset(ym2203_operator *op)
{
	op->phase = 0;
	op->env_attenuation = 0x3ff;
	op->env_state = EG_RELEASE;
	op->ssg_inverted = 0;
	op->key_state = 0;
	op->keyon_live = 0;
}

static void start_attack(const ym2203 *c, ym2203_operator *op, int is_restart)
{
	if (op->env_state == EG_ATTACK)
		return;
	op->env_state = EG_ATTACK;
	if (!is_restart)
		op->ssg_inverted = (uint8_t)(ssg_eg_enable(c, op) & bit(ssg_eg_mode(c, op), 2));
	if (!is_restart)
		op->phase = 0;
	if (op->eg_rate[EG_ATTACK] >= 62)
		op->env_attenuation = 0;
}

static void start_release(ym2203_operator *op)
{
	if (op->env_state >= EG_RELEASE)
		return;
	op->env_state = EG_RELEASE;
	if (op->ssg_inverted)
	{
		op->env_attenuation = (uint16_t)((0x200 - op->env_attenuation) & 0x3ff);
		op->ssg_inverted = 0;
	}
}

static void clock_keystate(const ym2203 *c, ym2203_operator *op, uint32_t keystate)
{
	if ((keystate ^ op->key_state) != 0)
	{
		op->key_state = (uint8_t)keystate;
		if (keystate != 0)
			start_attack(c, op, 0);
		else
			start_release(op);
	}
}

static int op_prepare(const ym2203 *c, ym2203_operator *op)
{
	cache_operator_data(c, op);
	clock_keystate(c, op, op->keyon_live != 0);
	op->keyon_live &= (uint8_t)~(1 << KEYON_CSM);
	return op->env_state != EG_RELEASE || op->env_attenuation < EG_QUIET;
}

static void clock_ssg_eg_state(const ym2203 *c, ym2203_operator *op)
{
	if (!bit(op->env_attenuation, 9))
		return;
	const uint32_t mode = ssg_eg_mode(c, op);
	if (bit(mode, 0))
	{
		op->ssg_inverted = (uint8_t)(bit(mode, 2) ^ bit(mode, 1));
		if (op->env_state != EG_ATTACK)
			op->env_attenuation = op->ssg_inverted ? 0x200 : 0x3ff;
	}
	else
	{
		op->ssg_inverted ^= (uint8_t)bit(mode, 1);
		if (op->env_state == EG_DECAY || op->env_state == EG_SUSTAIN)
			start_attack(c, op, 1);
		if (bit(mode, 1) == 0)
			op->phase = 0;
	}
	if (op->env_state == EG_RELEASE)
		op->env_attenuation = 0x3ff;
}

static void clock_envelope(const ym2203 *c, ym2203_operator *op, uint32_t env_counter)
{
	if (op->env_state == EG_ATTACK && op->env_attenuation == 0)
		op->env_state = EG_DECAY;
	if (op->env_state == EG_DECAY && op->env_attenuation >= op->eg_sustain)
		op->env_state = EG_SUSTAIN;
	const uint32_t rate = op->eg_rate[op->env_state];
	const uint32_t rate_shift = rate >> 2;
	env_counter <<= rate_shift;
	if (bits(env_counter, 0, 11) != 0)
		return;
	const uint32_t relevant_bits = bits(env_counter, rate_shift <= 11 ? 11 : (int)rate_shift, 3);
	const uint32_t increment = attenuation_increment(rate, relevant_bits);
	if (op->env_state == EG_ATTACK)
	{
		if (rate < 62)
			op->env_attenuation = (uint16_t)(op->env_attenuation + (((uint32_t)~(int)op->env_attenuation * increment) >> 4));
	}
	else
	{
		if (!ssg_eg_enable(c, op))
			op->env_attenuation = (uint16_t)(op->env_attenuation + increment);
		else if (op->env_attenuation < 0x200)
			op->env_attenuation = (uint16_t)(op->env_attenuation + 4 * increment);
		if (op->env_attenuation >= 0x400)
			op->env_attenuation = 0x3ff;
	}
}

static void op_clock(const ym2203 *c, ym2203_operator *op, uint32_t env_counter)
{
	if (ssg_eg_enable(c, op))
		clock_ssg_eg_state(c, op);
	else
		op->ssg_inverted = 0;
	if (bits(env_counter, 0, 2) == 0)
		clock_envelope(c, op, env_counter >> 2);
	op->phase += op->phase_step;
}

static uint32_t envelope_attenuation(const ym2203_operator *op)
{
	uint32_t result = op->env_attenuation >> op->eg_shift;
	if (op->ssg_inverted)
		result = (0x200 - result) & 0x3ff;
	result += op->total_level;
	return result < 0x3ff ? result : 0x3ff;
}

static int32_t compute_volume(const ym2203_operator *op, uint32_t phase)
{
	if (op->env_attenuation > EG_QUIET)
		return 0;
	const uint32_t sin_attenuation = waveform[phase & 0x3ff];
	const uint32_t env_attenuation = envelope_attenuation(op) << 2;
	const int32_t result = (int32_t)attenuation_to_volume((sin_attenuation & 0x7fff) + env_attenuation);
	return bit(sin_attenuation, 15) ? -result : result;
}

static inline uint32_t op_phase(const ym2203_operator *op) { return op->phase >> 10; }

/* ---- the channels (fm_channel, four operators) ---- */

#define ALGORITHM(op2in, op3in, op4in, op1out, op2out, op3out) \
	((op2in) | ((op3in) << 1) | ((op4in) << 4) | ((op1out) << 7) | ((op2out) << 8) | ((op3out) << 9))
static const uint16_t algorithm_ops[8] =
{
	ALGORITHM(1,2,3, 0,0,0), ALGORITHM(0,5,3, 0,0,0), ALGORITHM(0,2,6, 0,0,0), ALGORITHM(1,0,7, 0,0,0),
	ALGORITHM(1,0,3, 0,1,0), ALGORITHM(1,1,1, 0,1,1), ALGORITHM(1,0,0, 0,1,1), ALGORITHM(0,0,0, 1,1,1)
};
#undef ALGORITHM

static inline int32_t clamp32(int32_t value, int32_t low, int32_t high)
{
	return value < low ? low : value > high ? high : value;
}

static int32_t channel_output(ym2203 *c, ym2203_channel *ch, int32_t clipmax)
{
	const ym2203_operator *op0 = &c->ops[ch->op[0]], *op1 = &c->ops[ch->op[1]];
	const ym2203_operator *op2 = &c->ops[ch->op[2]], *op3 = &c->ops[ch->op[3]];
	int32_t opmod = 0;
	const uint32_t feedback = reg_byte(c, 0xb0, 3, 3, ch->choffs);
	if (feedback != 0)
		opmod = (ch->feedback[0] + ch->feedback[1]) >> (10 - feedback);
	ch->feedback_in = (int16_t)compute_volume(op0, op_phase(op0) + (uint32_t)opmod);
	const int32_t op1value = ch->feedback_in;
	const uint32_t ops = algorithm_ops[reg_byte(c, 0xb0, 0, 3, ch->choffs)];
	int16_t opout[8];
	opout[0] = 0;
	opout[1] = (int16_t)op1value;
	opmod = opout[bits(ops, 0, 1)] >> 1;
	opout[2] = (int16_t)compute_volume(op1, op_phase(op1) + (uint32_t)opmod);
	opout[5] = (int16_t)(opout[1] + opout[2]);
	opmod = opout[bits(ops, 1, 3)] >> 1;
	opout[3] = (int16_t)compute_volume(op2, op_phase(op2) + (uint32_t)opmod);
	opout[6] = (int16_t)(opout[1] + opout[3]);
	opout[7] = (int16_t)(opout[2] + opout[3]);
	opmod = opout[bits(ops, 4, 3)] >> 1;
	int32_t result = compute_volume(op3, op_phase(op3) + (uint32_t)opmod);
	const int32_t clipmin = -clipmax - 1;
	if (bit(ops, 7))
		result = clamp32(result + opout[1], clipmin, clipmax);
	if (bit(ops, 8))
		result = clamp32(result + opout[2], clipmin, clipmax);
	if (bit(ops, 9))
		result = clamp32(result + opout[3], clipmin, clipmax);
	return result;
}

/* ---- the engine (fm_engine_base) ---- */

static void fm_write(ym2203 *c, uint32_t regnum, uint8_t data)
{
	if (regnum == 0x27)
	{
		/* engine_mode_write: the register, and the timers (not made: gng/ym2203.h) */
		uint32_t channel, opmask;
		c->modified_channels = ALL_CHANNELS;
		regs_write(c, 0x27, data, &channel, &opmask);
		return;
	}
	c->modified_channels = ALL_CHANNELS;
	uint32_t channel = 0, opmask = 0;
	if (regs_write(c, regnum, data, &channel, &opmask) && channel < 3)
	{
		ym2203_channel *ch = &c->channels[channel];
		for (int i = 0; i < 4; i++)
		{
			ym2203_operator *op = &c->ops[ch->op[i]];
			op->keyon_live = (uint8_t)((op->keyon_live & ~(1 << KEYON_NORMAL)) | (bit(opmask, i) << KEYON_NORMAL));
		}
	}
}

static void fm_clock(ym2203 *c)
{
	c->total_clocks++;
	if (c->modified_channels != 0 || c->prepare_count++ >= 4096)
	{
		c->active_channels = 0;
		for (int ch = 0; ch < 3; ch++)
		{
			int active = 0;
			for (int i = 0; i < 4; i++)
				active |= op_prepare(c, &c->ops[c->channels[ch].op[i]]);
			if (active)
				c->active_channels |= 1u << ch;
		}
		c->modified_channels = c->prepare_count = 0;
	}
	if (bits(++c->env_counter, 0, 2) == 3)
		c->env_counter += 1;
	for (int ch = 0; ch < 3; ch++)
	{
		ym2203_channel *channel = &c->channels[ch];
		channel->feedback[0] = channel->feedback[1];
		channel->feedback[1] = channel->feedback_in;
		for (int i = 0; i < 4; i++)
			op_clock(c, &c->ops[channel->op[i]], c->env_counter);
	}
}

static void clock_fm(ym2203 *c)
{
	fm_clock(c);
	int32_t sum = 0;
	for (int ch = 0; ch < 3; ch++)
		if (bit(c->active_channels, ch))
			sum += channel_output(c, &c->channels[ch], 32767);
	c->last_fm = roundtrip_fp(sum);
}

/* ---- the SSG (ssg_engine) ---- */

static void ssg_clock(ym2203 *c)
{
	const uint8_t *r = c->ssg_regs;
	for (int chan = 0; chan < 3; chan++)
	{
		c->tone_count[chan]++;
		if (c->tone_count[chan] >= (uint32_t)(r[2 * chan] | (bits(r[2 * chan + 1], 0, 4) << 8)))
		{
			c->tone_state[chan] ^= 1;
			c->tone_count[chan] = 0;
		}
	}
	c->noise_count++;
	if ((c->noise_count >> 1) >= bits(r[0x06], 0, 5) && c->noise_count != 1)
	{
		c->noise_state ^= (bit(c->noise_state, 0) ^ bit(c->noise_state, 3)) << 17;
		c->noise_state >>= 1;
		c->noise_count = 0;
	}
	c->envelope_count++;
	if (c->envelope_count >= (uint32_t)(r[0x0b] | (r[0x0c] << 8)))
	{
		c->envelope_state++;
		c->envelope_count = 0;
	}
}

static void ssg_output(ym2203 *c, int32_t out[3])
{
	const uint8_t *r = c->ssg_regs;
	const uint32_t hold = bit(r[0x0d], 0), alternate = bit(r[0x0d], 1), attack_bit = bit(r[0x0d], 2);
	const uint32_t cont = bit(r[0x0d], 3);
	uint32_t envelope_volume;
	if ((hold | (cont ^ 1)) && c->envelope_state >= 32)
	{
		c->envelope_state = 32;
		envelope_volume = ((attack_bit ^ alternate) & cont) ? 31 : 0;
	}
	else
	{
		uint32_t attack = attack_bit;
		if (alternate)
			attack ^= bit(c->envelope_state, 5);
		envelope_volume = (c->envelope_state & 31) ^ (attack ? 0 : 31);
	}
	for (int chan = 0; chan < 3; chan++)
	{
		const uint32_t noise_on = bit(r[0x07], 3 + chan) | c->noise_state;
		const uint32_t tone_on = bit(r[0x07], chan) | c->tone_state[chan];
		uint32_t volume;
		if ((noise_on & tone_on) == 0)
			volume = 0;
		else if (bit(r[0x08 + chan], 4))
			volume = envelope_volume;
		else
		{
			volume = bits(r[0x08 + chan], 0, 4) * 2;
			if (volume != 0)
				volume |= 1;
		}
		out[chan] = ssg_amplitudes[volume];
	}
}

static void ssg_clock_and_add(ym2203 *c, int32_t sum[3], int32_t scale)
{
	ssg_clock(c);
	ssg_output(c, c->ssg_last);
	for (int i = 0; i < 3; i++)
		sum[i] += c->ssg_last[i] * scale;
}

static void ssg_add_last(const ym2203 *c, int32_t sum[3], int32_t scale)
{
	for (int i = 0; i < 3; i++)
		sum[i] += c->ssg_last[i] * scale;
}

/* ssg_resampler<output_data, 1, false>: one output sample */
static void ssg_resample(ym2203 *c, int32_t out[3])
{
	int32_t sum[3] = { 0, 0, 0 };
	int32_t divisor = 3;
	if (c->resampler == 43)
	{
		const int32_t step = (int32_t)bits(c->sampindex, 0, 2);
		ssg_add_last(c, sum, step);
		if (step != 3)
			ssg_clock_and_add(c, sum, 3 - step);
	}
	else if (c->resampler == 23)
	{
		if (bit(c->sampindex, 0) == 0)
		{
			ssg_clock_and_add(c, sum, 2);
			ssg_clock_and_add(c, sum, 1);
		}
		else
		{
			ssg_add_last(c, sum, 1);
			ssg_clock_and_add(c, sum, 2);
		}
	}
	else
	{
		/* 1:3 (prescale 2; the program does not select it): three SSG samples an output one */
		for (int i = 0; i < 3; i++)
			ssg_clock_and_add(c, sum, 1);
	}
	for (int i = 0; i < 3; i++)
		out[i] = sum[i] / divisor;
	c->sampindex++;
}

/* ---- ym2203 ---- */

static void update_prescale(ym2203 *c, uint8_t prescale)
{
	c->clock_prescale = prescale;
	/* medium fidelity */
	switch (prescale)
	{
	default:
	case 6: c->fm_samples_per_output = 6; c->resampler = 43; break;
	case 3: c->fm_samples_per_output = 3; c->resampler = 23; break;
	case 2: c->fm_samples_per_output = 2; c->resampler = 13; break;
	}
}

void ym2203_init(ym2203 *c)
{
	if (!waveform_ready)
	{
		for (uint32_t index = 0; index < 0x400; index++)
			waveform[index] = (uint16_t)(abs_sin_attenuation(index) | (bit(index, 9) << 15));
		waveform_ready = 1;
	}
	memset(c, 0, sizeof(*c));
	c->clock_prescale = 6;
	c->active_channels = ALL_CHANNELS;
	c->modified_channels = ALL_CHANNELS;
	static const int map[3][4] = { { 0, 6, 3, 9 }, { 1, 7, 4, 10 }, { 2, 8, 5, 11 } };
	for (int ch = 0; ch < 3; ch++)
	{
		c->channels[ch].choffs = (uint32_t)ch;
		for (int i = 0; i < 4; i++)
			c->channels[ch].op[i] = map[ch][i];
	}
	for (int opnum = 0; opnum < 12; opnum++)
	{
		c->ops[opnum].opoffs = (uint32_t)(opnum + opnum / 3);
		op_reset(&c->ops[opnum]);
	}
	for (int ch = 0; ch < 3; ch++)
		for (int i = 0; i < 4; i++)
			c->ops[c->channels[ch].op[i]].choffs = (uint32_t)ch;
	c->noise_state = 1;
	update_prescale(c, 6);
}

void ym2203_reset(ym2203 *c)
{
	/* fm_engine_base::reset (the envelope counter, the prepare state and the resampler's index run on) */
	memset(c->regs, 0, sizeof(c->regs));
	fm_write(c, 0x27, 0);
	for (int ch = 0; ch < 3; ch++)
	{
		c->channels[ch].feedback[0] = c->channels[ch].feedback[1] = 0;
		c->channels[ch].feedback_in = 0;
	}
	for (int opnum = 0; opnum < 12; opnum++)
		op_reset(&c->ops[opnum]);
	/* ssg_engine::reset */
	memset(c->ssg_regs, 0, sizeof(c->ssg_regs));
	for (int chan = 0; chan < 3; chan++)
		c->tone_count[chan] = c->tone_state[chan] = 0;
	c->envelope_count = c->envelope_state = 0;
	c->noise_count = 0;
	c->noise_state = 1;
}

void ym2203_write(ym2203 *c, unsigned offset, uint8_t data)
{
	if ((offset & 1) == 0)
	{
		c->address = data;
		if (c->address >= 0x2d && c->address <= 0x2f)
		{
			if (c->address == 0x2d)
				update_prescale(c, 6);
			else if (c->address == 0x2e && c->clock_prescale == 6)
				update_prescale(c, 3);
			else if (c->address == 0x2f)
				update_prescale(c, 2);
		}
		return;
	}
	if (c->address < 0x10)
	{
		c->ssg_regs[c->address & 0x0f] = data;
		if ((c->address & 0x0f) == 0x0d)
			c->envelope_state = 0;
	}
	else
		fm_write(c, c->address, data);
}

void ym2203_generate(ym2203 *c, int32_t out[4])
{
	if (c->sampindex % c->fm_samples_per_output == 0)
		clock_fm(c);
	out[0] = c->last_fm;
	ssg_resample(c, out + 1);
}
