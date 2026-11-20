/**
 *    Name:        gsound.cpp
 *    Description: the reference sound board (gsound.h): MAME 0.289's Z80 (tools/reference/mame/z80) and ymfm's
 *                 YM2203 (tools/reference/mame/ymfm, MAME 0.289's 3rdparty copy)
 *    Version:     0.1.0
 *    Created:     19-11-2026
 *
 *    license:BSD-3-Clause
 *    MAME's Z80 core is compiled here inside a namespace of its own, over arcade-decomp's stand-ins for MAME's
 *    framework (tools/core/system32/reference/sound: emu.h, logmacro.h, machine/z80daisy.h), so that its names
 *    keep away from the 6809 core's stand-ins in the same program. ymfm builds as it is.
 *    MAME's ym2203_device (sound/ymopn.cpp, sound/ymfm_mame.h) sets ymfm's medium fidelity and gives the SSG's
 *    three outputs first and the FM's last: gng.cpp's routes 0-2 (0.40) are SSG A, B, C, route 3 (0.20) the FM.
 *    The YM2203's IRQ is not connected on this board, and nothing reads the chips: their timers and busy flag
 *    change nothing the Z80 sees (CSM, the timer A's key-on, is not used: to check if a sound needs it).
 */
#include "gsound.h"

#include "ymfm_opn.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <memory>
#include <ostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace gs
{
#include "emu.h"
#include "logmacro.h"
#include "z80.cpp"
#include "z80dasm.cpp"
}

namespace
{
constexpr uint64_t kIrqPeriod = 12500;      // Z80 cycles: 3 MHz / 240 (set_periodic_int(4 * 60))
constexpr uint64_t kSamplePeriod = 24;      // Z80 cycles a YM2203 sample takes: 12 of its 1.5 MHz clocks
constexpr uint32_t kSampleRate = 125000;
// MAME's Z80 starts one cycle after the 6809 releases its reset line (measured: tools/mame_trace.py --sound, every
// Z80 RAM and YM2203 write one Z80 cycle later against the 6809's time than 2 x the release): its scheduler's
// rounding while the Z80 is suspended, a MAME mode effect (docs/accuracy.md #7): none in board mode
static uint64_t g_release_delay = 1;
// MAME's chips' samples are on its own time's grid, sample k at MAME time 24 k Z80 cycles, made up to a write's time
// included: with its time origin (docs/accuracy.md #6: 2110 6809 cycles, 4220 Z80 cycles, 4220 = 24 x 176 - 4), on
// this board's time sample n starts at 24 n + 4, and a write at t sees those at t or before: here, those that start
// before t + 1, 24 n + 3 < t (measured against MAME's -wavwrite: tools/compare_mame_sound.py, docs/accuracy.md #6b)
static uint64_t g_sample_phase = 3;

class sound_cpu : public gs::z80_device
{
public:
	sound_cpu(const gs::machine_config &config) : gs::z80_device(config, "audiocpu", nullptr, 3000000) {}
	uint16_t pc() const { return m_pc.w; }
	int icount() const { return m_icount; }
};

class fm_chip : public ymfm::ymfm_interface
{
public:
	fm_chip() : m_chip(*this) { m_chip.set_fidelity(ymfm::OPN_FIDELITY_MED); }
	void reset() { m_chip.reset(); }
	void write(uint32_t offset, uint8_t data) { m_chip.write(offset, data); }
	ymfm::ym2203::output_data generate()
	{
		ymfm::ym2203::output_data output;
		m_chip.generate(&output, 1);
		return output;
	}

private:
	ymfm::ym2203 m_chip;
};

gs::machine_config g_config;
std::unique_ptr<sound_cpu> g_z80;
fm_chip g_fm[2];
const uint8_t *g_program;
uint8_t g_ram[0x800];
uint8_t g_latch;
bool g_in_reset = true;
bool g_irq_line;
uint64_t g_time;              // the Z80's own time: where it has run to
uint64_t g_next_irq = kIrqPeriod;
uint64_t g_samples;           // samples made so far (each chip)
uint64_t g_run_start;         // the time the running slice began at, and its length (for the time of an access)
int g_run_length;
bool g_running;
FILE *g_log;
FILE *g_wav;
uint32_t g_wav_samples;
uint8_t g_executed[0x10000 / 8];

uint64_t now()
{
	return g_running ? g_run_start + uint64_t(g_run_length - g_z80->icount()) : g_time;
}

// the chips' samples that start before 'time'
void make_samples(uint64_t time)
{
	while (g_samples * kSamplePeriod + g_sample_phase < time)
	{
		const ymfm::ym2203::output_data a = g_fm[0].generate();
		const ymfm::ym2203::output_data b = g_fm[1].generate();
		g_samples++;
		if (g_wav)
		{
			// ymfm's outputs: 0 the FM, 1-3 the SSG's A, B, C; MAME's routes: the SSG at 0.40, the FM at 0.20
			double mix = 0;
			for (const auto *chip : { &a, &b })
				mix += 0.20 * chip->data[0] + 0.40 * (chip->data[1] + chip->data[2] + chip->data[3]);
			const int value = std::clamp(int(std::lround(mix)), -32768, 32767);
			const uint8_t bytes[2] = { uint8_t(value), uint8_t(value >> 8) };
			std::fwrite(bytes, 1, 2, g_wav);
			g_wav_samples++;
		}
	}
}

uint8_t read(gs::offs_t address)
{
	address &= 0xFFFF;
	if (address < 0x8000)
		return g_program[address];
	if (address >= 0xC000 && address < 0xC800)
		return g_ram[address - 0xC000];
	if (address == 0xC800)
		return g_latch;
	return 0;    // (unmapped)
}

FILE *g_ram_log;
int g_ram_logged;
FILE *g_marked_out;
uint64_t g_writes;

void write(gs::offs_t address, uint8_t data)
{
	address &= 0xFFFF;
	if (address >= 0xC000 && address < 0xC800)
	{
		g_ram[address - 0xC000] = data;
		if (g_ram_log && g_ram_logged < 200)
		{
			std::fprintf(g_ram_log, "%llu %04x %02x\n", (unsigned long long)now(), unsigned(address), data);
			g_ram_logged++;
		}
	}
	else if (address >= 0xE000 && address <= 0xE003)
	{
		const uint64_t time = now();
		make_samples(time);
		const int chip = (address >> 1) & 1;
		if (g_marked_out)
		{
			// the RAM at every write: MAME's marks (its first write after each frame) are looked up by number
			uint32_t hash = 2166136261u;
			for (uint8_t byte : g_ram)
				hash = (hash ^ byte) * 16777619u;
			std::fprintf(g_marked_out, "%llu %08x %04x\n", (unsigned long long)g_writes, hash, unsigned(g_z80->pc()));
		}
		g_writes++;
		if (g_log)
			std::fprintf(g_log, "%llu %d %u %02x\n", (unsigned long long)time, chip, unsigned(address & 1), data);
		g_fm[chip].write(address & 1, data);
	}
}

void set_irq(bool asserted)
{
	g_irq_line = asserted;
	g_z80->set_input_line(gs::INPUT_LINE_IRQ0, asserted ? gs::ASSERT_LINE : gs::CLEAR_LINE);
}

// the Z80 up to 'until' (MAME's scheduler: it runs whole cycles, and may end a little past the time)
void run_z80(uint64_t until)
{
	if (g_in_reset)
	{
		g_time = std::max(g_time, until);
		return;
	}
	while (g_time < until)
	{
		g_run_start = g_time;
		g_run_length = int(until - g_time);
		g_running = true;
		const int ran = g_z80->run(g_run_length);
		g_running = false;
		g_time += uint64_t(ran);
	}
}
}

extern "C" {

void gs_power_on(const uint8_t *program, int mame_mode)
{
	g_program = program;
	g_release_delay = mame_mode ? 1 : 0;
	g_sample_phase = mame_mode ? 3 : 0;
	std::memset(g_ram, 0, sizeof(g_ram));
	std::memset(g_executed, 0, sizeof(g_executed));
	g_latch = 0;
	g_time = 0;
	g_next_irq = kIrqPeriod;
	g_samples = 0;
	g_in_reset = true;
	g_irq_line = false;
	g_z80 = std::make_unique<sound_cpu>(g_config);
	for (int space : { gs::AS_PROGRAM, gs::AS_IO })
	{
		g_z80->space(space).read = space == gs::AS_PROGRAM ? read : [](gs::offs_t) -> uint8_t { return 0; };
		g_z80->space(space).write = space == gs::AS_PROGRAM ? write : [](gs::offs_t, uint8_t) {};
	}
	g_z80->irq_vector = []() {
		// HOLD_LINE: cleared as the CPU takes it
		if (g_irq_line)
			set_irq(false);
		return 0xFF;
	};
	g_z80->instruction_hook = [](gs::offs_t pc) { g_executed[(pc & 0xFFFF) >> 3] |= uint8_t(1u << (pc & 7)); };
	if (const char *path = std::getenv("GSOUND_RAM_LOG"))
		g_ram_log = std::fopen(path, "w");
	g_z80->start();
	g_z80->reset();
	for (auto &chip : g_fm)
		chip.reset();
}

void gs_run_until(uint64_t time)
{
	// the IRQ timer's ticks on the way
	while (g_next_irq <= time)
	{
		run_z80(g_next_irq);
		make_samples(g_next_irq);
		if (!g_in_reset)
			set_irq(true);
		g_next_irq += kIrqPeriod;
	}
	run_z80(time);
	make_samples(time);
}

void gs_latch(uint64_t time, uint8_t value)
{
	gs_run_until(time);
	g_latch = value;
}

void gs_q1(uint64_t time, int value)
{
	gs_run_until(time);
	const bool reset = value == 0;
	if (reset)
	{
		// the YM2203s reset (ym_reset_w), the Z80 held
		for (auto &chip : g_fm)
			chip.reset();
		g_in_reset = true;    // (a held IRQ stays held: MAME's suspend keeps the line)
	}
	else if (g_in_reset)
	{
		// the reset line released: the Z80 reset (diexec.cpp), which restarts its periodic IRQ's timer
		// (interface_post_reset), then on from here
		g_z80->reset();
		g_in_reset = false;
		g_time = std::max(g_time, time + g_release_delay);
		g_next_irq = time + g_release_delay + kIrqPeriod;
	}
}

const uint8_t *gs_ram(void) { return g_ram; }

uint32_t gs_ram_hash(void)
{
	uint32_t hash = 2166136261u;
	for (uint8_t byte : g_ram)
		hash = (hash ^ byte) * 16777619u;
	return hash;
}

uint16_t gs_pc(void) { return g_z80->pc(); }
uint64_t gs_time(void) { return g_time; }
void gs_log_writes(FILE *out) { g_log = out; }

void gs_ram_at_writes(FILE *out) { g_marked_out = out; }

void gs_wav(FILE *out)
{
	g_wav = out;
	g_wav_samples = 0;
	uint8_t header[44] = {};
	std::fwrite(header, 1, sizeof(header), out);
}

void gs_close_wav(void)
{
	if (!g_wav)
		return;
	auto put32 = [](uint8_t *p, uint32_t v) { p[0] = uint8_t(v); p[1] = uint8_t(v >> 8); p[2] = uint8_t(v >> 16); p[3] = uint8_t(v >> 24); };
	uint8_t header[44];
	std::memcpy(header, "RIFF", 4);
	put32(header + 4, 36 + g_wav_samples * 2);
	std::memcpy(header + 8, "WAVEfmt ", 8);
	put32(header + 16, 16);
	header[20] = 1; header[21] = 0;    // PCM
	header[22] = 1; header[23] = 0;    // mono
	put32(header + 24, kSampleRate);
	put32(header + 28, kSampleRate * 2);
	header[32] = 2; header[33] = 0;
	header[34] = 16; header[35] = 0;
	std::memcpy(header + 36, "data", 4);
	put32(header + 40, g_wav_samples * 2);
	std::fseek(g_wav, 0, SEEK_SET);
	std::fwrite(header, 1, sizeof(header), g_wav);
	std::fclose(g_wav);
	g_wav = nullptr;
}

uint32_t gs_sample_rate(void) { return kSampleRate; }
const uint8_t *gs_executed(void) { return g_executed; }

}
