/**
 *    Name:        ym2203.h
 *    Description: the YM2203 (OPN): three FM channels of four operators and the SSG's three square channels, as
 *                 MAME 0.289 makes them (ymfm, at the medium fidelity MAME sets for the YM2203)
 *    Version:     0.1.0
 *    Created:     19-11-2026
 *
 *    license:BSD-3-Clause
 *    After Aaron Giles's ymfm (BSD-3-Clause): ymfm_opn.cpp's ym2203 (generate, update_prescale, clock_fm, the
 *    prescaler's addresses 2D-2F), ymfm_fm.ipp's fm_engine_base, fm_channel and fm_operator for opn_registers (no
 *    LFO, three channels), ymfm_opn.cpp's opn_registers and ssg_resampler, ymfm_ssg.cpp's ssg_engine. Checked sample
 *    for sample against ymfm (tools/reference/sound/ymcheck.cpp).
 *    Not made: the timers and the status (nothing on this board reads the chips and their IRQ is not connected), so
 *    neither CSM (the timer A's key-on: the sound program never writes register 27).
 *    One sample out a call, at the chip's output rate (clock / 12: 125 kHz at 1.5 MHz): FM, then SSG A, B, C.
 */
#ifndef GNG_YM2203_H
#define GNG_YM2203_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ym2203_operator
{
	uint32_t opoffs, choffs;
	uint32_t phase;                 /* 10.10 */
	uint16_t env_attenuation;       /* 4.6 */
	uint32_t env_state;
	uint8_t ssg_inverted, key_state, keyon_live;
	/* opdata_cache */
	uint32_t phase_step, total_level, block_freq, multiple, eg_sustain;
	int32_t detune;
	uint8_t eg_rate[6];
	uint8_t eg_shift;
} ym2203_operator;

typedef struct ym2203_channel
{
	uint32_t choffs;
	int16_t feedback[2];
	int16_t feedback_in;
	int op[4];
} ym2203_channel;

typedef struct ym2203
{
	/* the FM engine */
	uint8_t regs[0x100];
	ym2203_operator ops[12];
	ym2203_channel channels[3];
	uint32_t env_counter;
	uint8_t clock_prescale;
	uint8_t total_clocks;
	uint32_t active_channels, modified_channels, prepare_count;
	/* the SSG */
	uint8_t ssg_regs[0x10];
	uint32_t tone_count[3], tone_state[3];
	uint32_t envelope_count, envelope_state, noise_count, noise_state;
	/* ym2203 */
	uint8_t address;
	uint8_t fm_samples_per_output;
	int32_t last_fm;
	uint32_t sampindex;             /* the SSG resampler's */
	int resampler;                  /* its ratio: 43 (4:3), 23 (2:3), 13 (1:3) */
	int32_t ssg_last[3];
} ym2203;

/* The chip as ymfm's constructor and MAME's device_start leave it (medium fidelity, prescale 6), then reset */
void ym2203_init(ym2203 *chip);
void ym2203_reset(ym2203 *chip);
void ym2203_write(ym2203 *chip, unsigned offset, uint8_t data);
/* One output sample: out[0] the FM, out[1-3] the SSG's A, B, C */
void ym2203_generate(ym2203 *chip, int32_t out[4]);

#ifdef __cplusplus
}
#endif

#endif
