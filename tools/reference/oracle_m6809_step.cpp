/**
 *    Name:        oracle_m6809_step.cpp
 *    Description: MAME's MC6809 core (mame/m6809, unchanged) running one instruction, or the IRQ's entry, from
 *                 given states: what the lift (tools/m6809/lift.py) is checked against
 *    Version:     0.1.0
 *    Created:     19-11-2026
 *
 *    license:BSD-3-Clause
 *    oracle_m6809_step < cases > results
 *
 *    A case is a line: "PC A B DP X Y U S CC IRQ" (hex) then any number of "addr:bytes" (hex: the bytes from that
 *    address on, the instruction's among them). Memory holds those bytes, and elsewhere a pattern of the address
 *    (pattern(), the same as tools/m6809/interpreter.py's). With IRQ 1 the IRQ line is held (HOLD_LINE: cleared as
 *    the CPU takes it), so that a CPU with I clear takes it at once: the step is then the interrupt's entry.
 *
 *    For each case a line: "PC A B DP X Y U S CC CYCLES" after the step (as the next instruction starts), then
 *    every bus cycle of the step, "K:ADDR:VALUE:CYCLE" each: K is O (the opcode space: the opcode, its
 *    operands, the dummy reads, the VMA cycles' reads of FFFF), R (a data read) or W (a write); CYCLE counts from
 *    0, the step's first cycle.
 *
 *    The step ends where the core's instruction hook (debugger_instruction_hook, at MAIN) announces the next
 *    instruction: the core runs a cycle at a time until then. Reset reaches MAIN after the vector's four cycles,
 *    and the state is set there.
 */
#include "emu.h"
#include "m6809.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <sstream>
#include <string>
#include <vector>

class step_cpu : public mc6809_device
{
public:
	step_cpu() : mc6809_device(machine_config(), "maincpu", nullptr, 6000000) {}
	int icount() const { return m_icount; }
	void set_state(u16 pc, u8 a, u8 b, u8 dp, u16 x, u16 y, u16 u, u16 s, u8 cc)
	{
		m_pc.w = pc;
		m_q.r.a = a;
		m_q.r.b = b;
		m_dp = dp;
		m_x.w = x;
		m_y.w = y;
		m_u.w = u;
		m_s.w = s;
		m_cc = cc;
	}
	void print_state(std::ostringstream &out) const
	{
		char text[80];
		std::snprintf(text, sizeof(text), "%04X %02X %02X %02X %04X %04X %04X %04X %02X", m_pc.w, m_q.r.a, m_q.r.b,
			m_dp, m_x.w, m_y.w, m_u.w, m_s.w, m_cc);
		out << text;
	}
};

static step_cpu *cpu;
static std::map<u16, u8> given, written;
static bool recording, done, irq_held;
static int hooks, hooks_to_end, run_length;
static u64 consumed;
static std::ostringstream accesses, final_state;

static u8 pattern(u16 address)
{
	return u8(((u32(address) * 2654435761u) & 0xFFFFFFFFu) >> 13);
}

static u8 byte(u16 address)
{
	auto w = written.find(address);
	if (w != written.end())
		return w->second;
	auto g = given.find(address);
	return g != given.end() ? g->second : pattern(address);
}

static void record(char kind, u16 address, u8 value)
{
	if (!recording || done)
		return;
	char text[32];
	const long long cycle = (long long)consumed + (run_length - cpu->icount()) - 1;
	std::snprintf(text, sizeof(text), " %c:%04X:%02X:%lld", kind, address, value, cycle);
	accesses << text;
}

static u8 host_read(u16 address)
{
	const u8 value = byte(address);
	record('R', address, value);
	return value;
}

static u8 host_read_opcode(u16 address)
{
	const u8 value = byte(address);
	record('O', address, value);
	return value;
}

static void host_write(u16 address, u8 data)
{
	record('W', address, data);
	if (recording && !done)
		written[address] = data;
}

static void host_acknowledge(int line)
{
	if (line == M6809_IRQ_LINE && irq_held)
	{
		irq_held = false;
		cpu->set_input_line(M6809_IRQ_LINE, CLEAR_LINE);
	}
}

static void host_instruction(u16)
{
	if (!recording || done)
		return;
	if (++hooks == hooks_to_end)
	{
		done = true;
		cpu->print_state(final_state);
	}
}

int main()
{
	m6809_host::read = host_read;
	m6809_host::read_opcode = host_read_opcode;
	m6809_host::write = host_write;
	m6809_host::acknowledge = host_acknowledge;
	m6809_host::instruction = host_instruction;
	step_cpu core;
	cpu = &core;
	core.start();

	std::string line;
	char buffer[4096];
	while (std::fgets(buffer, sizeof(buffer), stdin))
	{
		line = buffer;
		std::istringstream in(line);
		unsigned pc, a, b, dp, x, y, u, s, cc, irq;
		in >> std::hex >> pc >> a >> b >> dp >> x >> y >> u >> s >> cc >> irq;
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
			u16 address = u16(std::strtoul(chunk.substr(0, colon).c_str(), nullptr, 16));
			const std::string bytes = chunk.substr(colon + 1);
			for (size_t i = 0; i + 1 < bytes.size(); i += 2)
				given[address++] = u8(std::strtoul(bytes.substr(i, 2).c_str(), nullptr, 16));
		}

		// reset, through its vector's four cycles, to MAIN; then the case's state
		recording = false;
		done = false;
		irq_held = false;
		core.set_input_line(M6809_IRQ_LINE, CLEAR_LINE);
		core.reset();
		int left = 4;
		while (left > 0)
			left -= core.run(left);
		core.set_state(u16(pc), u8(a), u8(b), u8(dp), u16(x), u16(y), u16(u), u16(s), u8(cc));
		if (irq)
		{
			irq_held = true;
			core.set_input_line(M6809_IRQ_LINE, ASSERT_LINE);
		}
		// an IRQ the CPU takes at once comes before the instruction hook: then the step ends at the first hook
		const bool takes_irq = irq && !(cc & 0x10);
		hooks = 0;
		hooks_to_end = takes_irq ? 1 : 2;
		accesses.str("");
		final_state.str("");
		recording = true;
		consumed = 0;
		int guard = 0;
		while (!done && guard++ < 1000)
		{
			run_length = 1;
			const int used = core.run(1);
			if (!done)
				consumed += u64(used);
		}
		std::printf("%s %llu%s\n", final_state.str().c_str(), (unsigned long long)consumed, accesses.str().c_str());
	}
	return 0;
}
