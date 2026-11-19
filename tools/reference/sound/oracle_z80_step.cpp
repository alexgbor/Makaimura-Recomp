/**
 *    Name:        oracle_z80_step.cpp
 *    Description: MAME 0.289's Z80 core (tools/reference/mame/z80, unchanged) running one instruction, or the IRQ's
 *                 entry, from given states: what the sound program's lift (tools/z80cpu/lift.py) is checked against
 *    Version:     0.1.0
 *    Created:     19-11-2026
 *
 *    license:BSD-3-Clause
 *    oracle_z80_step < cases > results
 *
 *    A case is a line: "PC SP AF BC DE HL IX IY AF' BC' DE' HL' WZ I IFF1 IFF2 IM IRQ" (hex) then any number of
 *    "addr:bytes" (hex: the bytes from that address on). Memory holds those bytes, and elsewhere a pattern of the
 *    address (pattern(), the same as tools/z80cpu/interpreter.py's); a port reads the pattern of its address too.
 *    With IRQ 1 the IRQ line is held (cleared as the CPU takes it): with IFF1 set the step is the interrupt's entry.
 *
 *    For each case a line: the registers in the same order after the step (as the next instruction starts), the
 *    cycles, then every bus cycle "K:ADDR:VALUE:CYCLE": K is R (a memory read: opcodes and operands too), W (a
 *    write), I (a port read), O (a port write); CYCLE counts from 0 at the step's start, the time the core
 *    makes the access at (it counts an access's cycles after it: z80make.py's "N !! access").
 */
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
#include <map>
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
class step_cpu : public gs::z80_device
{
public:
	step_cpu(const gs::machine_config &config) : gs::z80_device(config, "audiocpu", nullptr, 3000000) {}
	int icount() const { return m_icount; }
	void set_state(const unsigned *v)
	{
		m_pc.w = uint16_t(v[0]); m_sp.w = uint16_t(v[1]);
		m_af.w = uint16_t(v[2]); set_f(uint8_t(v[2]));
		m_bc.w = uint16_t(v[3]); m_de.w = uint16_t(v[4]); m_hl.w = uint16_t(v[5]);
		m_ix.w = uint16_t(v[6]); m_iy.w = uint16_t(v[7]);
		m_af2.w = uint16_t(v[8]); m_bc2.w = uint16_t(v[9]); m_de2.w = uint16_t(v[10]); m_hl2.w = uint16_t(v[11]);
		m_wz.w = uint16_t(v[12]); m_i = uint8_t(v[13]);
		m_iff1 = v[14] != 0; m_iff2 = v[15] != 0; m_im = uint8_t(v[16]);
	}
	void print_state(std::ostringstream &out)
	{
		char text[160];
		const unsigned af = (unsigned(m_af.b.h) << 8) | get_f();
		std::snprintf(text, sizeof(text), "%04X %04X %04X %04X %04X %04X %04X %04X %04X %04X %04X %04X %04X %02X %d %d %d",
			m_pc.w, m_sp.w, af, m_bc.w, m_de.w, m_hl.w, m_ix.w, m_iy.w, m_af2.w, m_bc2.w, m_de2.w, m_hl2.w,
			m_wz.w, m_i, m_iff1 ? 1 : 0, m_iff2 ? 1 : 0, m_im);
		out << text;
	}
};

gs::machine_config g_config;
std::unique_ptr<step_cpu> cpu;
std::map<uint16_t, uint8_t> given, written;
bool recording, done, irq_held;
int hooks, hooks_to_end, run_length;
uint64_t consumed;
std::ostringstream accesses, final_state;

uint8_t pattern(uint32_t address)
{
	return uint8_t(((address * 2654435761u) & 0xFFFFFFFFu) >> 13);
}

uint8_t byte(uint16_t address)
{
	auto w = written.find(address);
	if (w != written.end())
		return w->second;
	auto g = given.find(address);
	return g != given.end() ? g->second : pattern(address);
}

void record(char kind, uint16_t address, uint8_t value)
{
	if (!recording || done)
		return;
	char text[40];
	// the access comes before the core counts its cycles
	const long long cycle = (long long)consumed + (run_length - cpu->icount());
	std::snprintf(text, sizeof(text), " %c:%04X:%02X:%lld", kind, address, value, cycle);
	accesses << text;
}
}

int main()
{
	cpu = std::make_unique<step_cpu>(g_config);
	cpu->space(gs::AS_PROGRAM).read = [](gs::offs_t a) { const uint8_t v = byte(uint16_t(a)); record('R', uint16_t(a), v); return v; };
	cpu->space(gs::AS_PROGRAM).write = [](gs::offs_t a, uint8_t d) {
		record('W', uint16_t(a), d);
		if (recording && !done)
			written[uint16_t(a)] = d;
	};
	cpu->space(gs::AS_IO).read = [](gs::offs_t a) { const uint8_t v = pattern(a & 0xFFFF); record('I', uint16_t(a), v); return v; };
	cpu->space(gs::AS_IO).write = [](gs::offs_t a, uint8_t d) { record('O', uint16_t(a), d); };
	cpu->irq_vector = []() {
		if (irq_held)
		{
			irq_held = false;
			cpu->set_input_line(gs::INPUT_LINE_IRQ0, gs::CLEAR_LINE);
		}
		return 0xFF;
	};
	cpu->instruction_hook = [](gs::offs_t) {
		if (!recording || done)
			return;
		if (++hooks == hooks_to_end)
		{
			done = true;
			cpu->print_state(final_state);
		}
	};
	cpu->start();

	char buffer[4096];
	while (std::fgets(buffer, sizeof(buffer), stdin))
	{
		std::istringstream in(buffer);
		unsigned v[17], irq;
		for (unsigned &value : v)
			in >> std::hex >> value;
		in >> std::hex >> irq;
		if (!in)
			continue;
		given.clear();
		written.clear();
		std::string chunk;
		while (in >> chunk)
		{
			const size_t colon = chunk.find(':');
			if (colon == std::string::npos)
				continue;
			uint16_t address = uint16_t(std::strtoul(chunk.substr(0, colon).c_str(), nullptr, 16));
			const std::string bytes = chunk.substr(colon + 1);
			for (size_t i = 0; i + 1 < bytes.size(); i += 2)
				given[address++] = uint8_t(std::strtoul(bytes.substr(i, 2).c_str(), nullptr, 16));
		}

		recording = false;
		done = false;
		irq_held = false;
		cpu->set_input_line(gs::INPUT_LINE_IRQ0, gs::CLEAR_LINE);
		cpu->reset();
		cpu->set_state(v);
		if (irq)
		{
			irq_held = true;
			cpu->set_input_line(gs::INPUT_LINE_IRQ0, gs::ASSERT_LINE);
		}
		hooks = 0;
		hooks_to_end = irq && v[14] ? 1 : 2;
		accesses.str("");
		final_state.str("");
		recording = true;
		consumed = 0;
		for (int guard = 0; !done && guard < 2000; guard++)
		{
			run_length = 1;
			const int used = cpu->run(1);
			if (!done)
				consumed += uint64_t(used);
		}
		std::printf("%s %llu%s\n", final_state.str().c_str(), (unsigned long long)consumed, accesses.str().c_str());
	}
	return 0;
}
