/**
 *    Name:        fcheck.cpp
 *    Description: each function of the readable C (src/) against the original program on MAME's MC6809 core
 *                 (mame/m6809, unchanged), alone, from the same states: the board's memories and latches, the
 *                 registers it hands back, its cycles
 *    Version:     0.1.0
 *    Created:     23-11-2026
 *
 *    license:BSD-3-Clause
 *    fcheck [--accuracy mame|board] [--inputs FILE] [--frames N] [--samples N] [--variants N] [--seed N] [--cap CYCLES]
 *           [--function NAME [--trace]] [--regions FILE]
 *
 *    First the original program runs on MAME's core as gref runs it (tools/reference/gref.cpp: the IRQ at each
 *    VBLANK, the input script), and as an instruction starts at a function's entry the state is kept: the
 *    registers and the board (gng: its memories, latches). Up to --samples states a function (spread over the run:
 *    a reservoir's sample). Then each function of the C (tools/m6809/check_functions.py's calls.c) is run from
 *    states of four kinds: its own as kept (real), those changed with registers from its other calls and work RAM
 *    bytes it read set to values they held in the run (plausible), made up (wild), and for a function the run never
 *    calls the others' states made up (unowned); a made-up register keeps what every caller agrees it holds
 *    (fc_entry_kind). Each run goes on MAME's core until the function returns (S 2 or 12 above where it started, the
 *    PC the address pulled) and in the C (fc_run), each with the IRQ's line clear, and is compared: how it ends
 *    (returned, past --cap cycles, at a SYNC), its cycles, the registers the C hands back (src/functions.txt), the
 *    board's memories and latches but the stack below S and <$E2-<$E3, the sound latch's writes and their times.
 *    Not compared: a run where the original goes to code the analysis never found or reads past a table it jumps
 *    through (build/fcheck/code.txt), reads <$E2-<$E3 holding the round robin's S or (board mode) 3C00, or where the C
 *    reaches past the work RAM (M6809_RAM_GUARD). Board mode: the runs go on from the state's time, and each time the
 *    board asks for it (the beam: the picture's writes, the palette's, the DMA and its halt) is compared too.
 *
 *    One line a function: "F name" then, for each kind, the runs compared, how many differ and how many were not
 *    compared; then each kind's first difference after a tab. --trace prints to stderr the original's instructions
 *    in the first run that differs. Exit status 1 if a real, plausible or unowned run differs.
 */
#include "emu.h"
#include "m6809.h"

#include "fcheck.h"
#include "gng/board.h"
#include "gng/video.h"

#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

class reference_cpu : public mc6809_device
{
public:
	reference_cpu() : mc6809_device(machine_config(), "maincpu", nullptr, 6000000) {}
	int icount() const { return m_icount; }
	void eat(int cycles) { m_icount -= cycles; }
	u16 pc() const { return m_pc.w; }
	u16 s() const { return m_s.w; }
	void get(fc_registers &r) const
	{
		r.a = m_q.r.a;
		r.b = m_q.r.b;
		r.dp = m_dp;
		r.x = m_x.w;
		r.y = m_y.w;
		r.u = m_u.w;
		r.s = m_s.w;
		r.cc = m_cc;
	}
	void set(u16 pc, const fc_registers &r)
	{
		m_pc.w = pc;
		m_q.r.a = u8(r.a);
		m_q.r.b = u8(r.b);
		m_dp = u8(r.dp);
		m_x.w = u16(r.x);
		m_y.w = u16(r.y);
		m_u.w = u16(r.u);
		m_s.w = u16(r.s);
		m_cc = u8(r.cc);
	}
};

static reference_cpu *core;

// the board's memories and latches: gng up to its ROMs' pointers (the inputs among them)
static const size_t BOARD_BYTES = offsetof(gng_board, roms);

struct state
{
	u32 key;
	u64 time;       // the board's, as the function was called: board mode's beam (the DMA's halt, the raster)
	bool booted;    // kept after the boot (the main loop reached): the work RAM holds the game's values, not the test's
	fc_registers registers;
	std::vector<u8> board;
};

// ---- the original program's run, as gref's ----

enum phase { CAPTURING, CHECKING, IDLE };
static bool booted;
static phase now_phase = CAPTURING;
static bool irq_held;
static u64 cycle_base, now;
static int run_length;

static std::vector<int> index_of_key(6 << 16, -1);
static std::vector<bool> known_code(6 << 16);    // (build/fcheck/code.txt)
// a jump through a table: the table and the targets the analysis found; the run is off the program where the original
// reads past the table (an index the game never gives it) or lands elsewhere
struct table_jump
{
	std::vector<std::pair<u16, unsigned>> tables;    // (the table, its entries): one jump may go through several
	std::set<u32> targets;
	bool reads_inside(u16 address) const
	{
		for (const auto &table : tables)
			if (address >= table.first && address < table.first + 2 * table.second)
				return true;
		return false;
	}
};
static std::map<u32, table_jump> table_jumps;
static const table_jump *in_table_jump;    // the instruction being run is one
static bool off_table;
static u32 previous_key;
static std::vector<std::vector<state>> kept;
static std::vector<u64> seen;
static int samples = 8;
static u64 rng_state = 1;

static u64 next_random()
{
	rng_state ^= rng_state << 13;
	rng_state ^= rng_state >> 7;
	rng_state ^= rng_state << 17;
	return rng_state;
}

static u32 key_of(u16 pc)
{
	return pc >= 0x4000 && pc < 0x6000 ? (u32(gng.bank) + 1) << 16 | pc : pc;
}

// the run being checked on MAME's core
static u16 start_s;
static u64 cap;
static bool stopped;
static bool tracing;    // --trace: each instruction of MAME's run, to stderr
static fc_end core_end;
static u64 core_cycles;
static std::set<u16> reads;

static u64 core_elapsed()
{
	return cycle_base + u64(run_length - core->icount());
}

// board mode: a read of 3C00 starts the sprite DMA (runtime/src/board.c), which the program does only in the reset
// (6017); elsewhere it is a made-up pointer, and the C reads through the pointers the game gives with no time asked
// for (m6809_at: the decompiler's analysis of where they lead), so the run is the program's no more
static bool board_mode;
static bool dma_read;
// the round robin's copies keep S at <$E2-<$E3 (its STS <$E2 at 661D-6720), which the C keeps in a variable of its own
// (docs/decompilation.md: S as a pointer): a read of those bytes after one, but by the round robin's LDS, reads what
// the C never wrote (a made-up pointer: the program reads them as a counter only after writing one itself)
static bool e2_holds_s[2];
static bool unkept_read;
static bool in_round_robin()
{
	const u16 pc = u16(previous_key);
	return pc >= 0x6619 && pc <= 0x6754;
}

static u8 host_read(u16 address)
{
	if (now_phase == CHECKING && address < 0x1E00)
		reads.insert(address);
	if (now_phase == CHECKING && board_mode && address == 0x3C00)
		dma_read = true;
	if (now_phase == CHECKING && (address == 0xE2 || address == 0xE3) && e2_holds_s[address - 0xE2] && !in_round_robin())
		unkept_read = true;
	if (in_table_jump && !in_table_jump->reads_inside(address))
		off_table = true;
	return gng_read(address);
}

static u8 host_read_opcode(u16 address)
{
	return gng_read(address);
}

static void host_write(u16 address, u8 data)
{
	if (now_phase == CHECKING && (address == 0xE2 || address == 0xE3))
		e2_holds_s[address - 0xE2] = in_round_robin();
	gng_write(address, data);
}

static void host_acknowledge(int line)
{
	if (line == M6809_IRQ_LINE && irq_held)
	{
		irq_held = false;
		core->set_input_line(M6809_IRQ_LINE, CLEAR_LINE);
	}
}

static void stop_core(fc_end end)
{
	stopped = true;
	core_end = end;
	core_cycles = core_elapsed();
	core->eat(core->icount());
}

static void host_instruction(u16 pc)
{
	if (now_phase == CAPTURING)
	{
		if (pc == 0x617F)
			booted = true;    // (the main loop waiting for work: symbols.txt's idle)
		const int index = index_of_key[key_of(pc)];
		if (index < 0)
			return;
		const u64 count = ++seen[index];
		size_t slot = kept[index].size();
		if (slot >= size_t(samples))
		{
			slot = size_t(next_random() % count);
			if (slot >= size_t(samples))
				return;
		}
		state s;
		s.key = key_of(pc);
		s.booted = booted;
		s.time = core_elapsed();
		core->get(s.registers);
		s.board.assign(reinterpret_cast<const u8 *>(&gng), reinterpret_cast<const u8 *>(&gng) + BOARD_BYTES);
		if (slot == kept[index].size())
			kept[index].push_back(std::move(s));
		else
			kept[index][slot] = std::move(s);
		return;
	}
	if (now_phase == IDLE || stopped)
		return;
	if (tracing)
	{
		fc_registers r;
		core->get(r);
		std::fprintf(stderr, "  %04X A=%02X B=%02X X=%04X Y=%04X U=%04X S=%04X CC=%02X  %llu\n", pc, r.a, r.b, r.x, r.y, r.u,
			r.s, r.cc, (unsigned long long)core_elapsed());
	}
	// returned: its RTS (or PULS ...,PC) left S 2 above where it started, its RTI (the IRQ's handler) 12, and the PC
	// is the address just pulled (a task's switch to another stack lands elsewhere)
	const u32 here = key_of(pc);
	const bool off = in_table_jump && (off_table || !in_table_jump->targets.count(here));
	const auto table = table_jumps.find(here);
	in_table_jump = table != table_jumps.end() ? &table->second : nullptr;
	off_table = false;
	previous_key = here;
	if (!known_code[here] || off || dma_read || unkept_read)
	{
		stop_core(FC_OFF_CODE);
		return;
	}
	const u16 s = core->s();
	if ((s == u16(start_s + 2) || s == u16(start_s + 12)) &&
		(u16(gng_read(u16(s - 2)) << 8 | gng_read(u16(s - 1))) == pc))
		stop_core(FC_RETURNED);
	else if (core_elapsed() >= cap)
		stop_core(FC_CAPPED);
	else if (gng_read(pc) == 0x13)
		stop_core(FC_SYNC);
}

// the sound latch's writes, by both runs, with the board's time; and every time the board asks the time (board mode:
// each write that changes the picture, each palette write, each DMA: what the beam makes of them hangs on it alone)
static std::vector<std::pair<u64, u8>> latch_log;
static std::vector<u64> clock_log;
static bool running_c;
static u64 time_base;    // the board's time as the run starts (the state's)
static u64 clock_now()
{
	if (now_phase != CHECKING)
		return core_elapsed();
	const u64 time = time_base + (running_c ? fc_now() : core_elapsed());
	clock_log.push_back(time);
	return time;
}
static void on_dma(gng_board *)
{
	const u32 halt = gng_dma_halt(clock_now());
	if (running_c)
		fc_halt(halt);
	else
		core->eat(int(halt));
}
static void on_latch(gng_board *, uint8_t value)
{
	latch_log.emplace_back(clock_now(), value);
}

static void run_until(u64 until)
{
	while (now < until)
	{
		run_length = int(until - now);
		cycle_base = now;
		now += u64(core->run(run_length));
	}
}

static std::map<std::string, std::vector<u8>> read_regions(const char *path)
{
	std::map<std::string, std::vector<u8>> regions;
	std::ifstream file(path, std::ios::binary);
	char magic[4];
	if (!file.read(magic, 4) || std::string(magic, 4) != "MKRG")
	{
		std::fprintf(stderr, "fcheck: %s is not a regions file (python tools/makaimura_rom.py --extract %s)\n", path, path);
		std::exit(2);
	}
	char name[17] = {};
	while (file.read(name, 16))
	{
		u8 size_bytes[4];
		file.read(reinterpret_cast<char *>(size_bytes), 4);
		const u32 size = size_bytes[0] | (size_bytes[1] << 8) | (size_bytes[2] << 16) | (u32(size_bytes[3]) << 24);
		std::vector<u8> data(size);
		file.read(reinterpret_cast<char *>(data.data()), size);
		regions[name] = std::move(data);
	}
	return regions;
}

struct input_change { int frame; std::string port; u8 value; };

static std::vector<input_change> read_inputs(const char *path)
{
	std::vector<input_change> changes;
	std::ifstream file(path);
	std::string line;
	while (std::getline(file, line))
	{
		if (line.empty() || line[0] == '#')
			continue;
		char port[16];
		int frame;
		unsigned value;
		if (std::sscanf(line.c_str(), "%d %15s %x", &frame, port, &value) == 3)
			changes.push_back({frame, port, u8(value)});
	}
	return changes;
}

static void apply_input(const input_change &change)
{
	if (change.port == "system") gng.inputs.system = change.value;
	else if (change.port == "p1") gng.inputs.p1 = change.value;
	else if (change.port == "p2") gng.inputs.p2 = change.value;
	else if (change.port == "dsw1") gng.inputs.dsw1 = change.value;
	else if (change.port == "dsw2") gng.inputs.dsw2 = change.value;
	else if (change.port[0] == 'm' && std::strtoul(change.port.c_str() + 1, nullptr, 16) < 0x1E00u)
		gng.ram[std::strtoul(change.port.c_str() + 1, nullptr, 16)] = change.value;
}

// ---- one function from one state, on both ----

struct outcome
{
	fc_end end;
	u64 cycles;
	fc_registers registers;
	std::vector<u8> board;
	std::vector<std::pair<u64, u8>> latch;
	std::vector<u64> clock;
	std::string fault;
	bool outside = false;
};

static void set_board(const std::vector<u8> &board)
{
	std::memcpy(&gng, board.data(), BOARD_BYTES);
}

static outcome run_core(u16 pc, const fc_registers &registers, const std::vector<u8> &board)
{
	core->set_input_line(M6809_IRQ_LINE, CLEAR_LINE);
	irq_held = false;
	core->reset();
	now_phase = IDLE;    // (the reset's vector fetch, the hook at 6000: nothing kept, nothing checked)
	int left = 4;
	while (left > 0)
	{
		run_length = left;
		left -= core->run(left);
	}
	set_board(board);
	latch_log.clear();
	clock_log.clear();
	running_c = false;
	core->set(pc, registers);
	start_s = u16(registers.s);
	previous_key = 0xFFFFFFFFu;
	in_table_jump = nullptr;
	off_table = false;
	dma_read = false;
	unkept_read = false;
	e2_holds_s[0] = e2_holds_s[1] = false;
	stopped = false;
	reads.clear();
	now_phase = CHECKING;
	u64 elapsed = 0;
	while (!stopped)
	{
		run_length = 1 << 16;
		cycle_base = elapsed;
		elapsed += u64(core->run(run_length));
		if (!stopped && elapsed > cap + 1000)
			stop_core(FC_CAPPED);    // (an instruction that never ends: none on the 6809, but a guard)
	}
	outcome o;
	o.end = core_end;
	o.cycles = core_cycles;
	core->get(o.registers);
	o.board.assign(reinterpret_cast<const u8 *>(&gng), reinterpret_cast<const u8 *>(&gng) + BOARD_BYTES);
	o.latch = latch_log;
	o.clock = clock_log;
	return o;
}

static outcome run_c(unsigned index, const fc_registers &registers, const std::vector<u8> &board)
{
	set_board(board);
	latch_log.clear();
	clock_log.clear();
	running_c = true;
	fc_registers r = registers;
	fc_result result;
	fc_run(index, &r, cap, &result);
	if (result.end == FC_RETURNED && !std::strcmp(fc_functions[index].name, "irq"))
		r.s = (r.s + 12) & 0xFFFF;    // (its RTI pulls the state the entry pushed: the runtime's, m6809_program.c)
	running_c = false;
	outcome o;
	o.end = result.end;
	o.cycles = result.cycles;
	o.registers = r;
	o.board.assign(reinterpret_cast<const u8 *>(&gng), reinterpret_cast<const u8 *>(&gng) + BOARD_BYTES);
	o.latch = latch_log;
	o.clock = clock_log;
	o.fault = result.fault;
	o.outside = result.outside != 0;
	return o;
}

static const char *end_name(fc_end end)
{
	switch (end)
	{
	case FC_RETURNED: return "returned";
	case FC_CAPPED: return "capped";
	case FC_SYNC: return "sync";
	case FC_FAULT: return "fault";
	case FC_OFF_CODE: return "off the code";
	default: return "crashed";
	}
}

static std::string board_place(size_t offset)
{
	char text[48];
	const size_t ram = offsetof(gng_board, ram), sprites = offsetof(gng_board, sprite_ram);
	const size_t fg = offsetof(gng_board, fg_ram), bg = offsetof(gng_board, bg_ram);
	if (offset >= ram && offset < ram + 0x1E00)
		std::snprintf(text, sizeof(text), "%04X", unsigned(offset - ram));
	else if (offset >= sprites && offset < sprites + 0x200)
		std::snprintf(text, sizeof(text), "%04X", unsigned(0x1E00 + offset - sprites));
	else if (offset >= fg && offset < fg + 0x800)
		std::snprintf(text, sizeof(text), "%04X", unsigned(0x2000 + offset - fg));
	else if (offset >= bg && offset < bg + 0x800)
		std::snprintf(text, sizeof(text), "%04X", unsigned(0x2800 + offset - bg));
	else
		std::snprintf(text, sizeof(text), "board+%zX", offset);
	return text;
}

// "" if the C did what the original did; else what differs
static std::string compare(const outcome &a, const outcome &b, u32 compared)
{
	char text[160];
	if (a.end != b.end)
	{
		std::snprintf(text, sizeof(text), "ends %s/%s%s%s", end_name(a.end), end_name(b.end), b.fault.empty() ? "" : ": ",
			b.fault.c_str());
		return text;
	}
	if (a.end == FC_CAPPED)
		return "";    // (both past the cap: the C's time is kept a block at a time, not where MAME's stops; not compared)
	std::string found;
	if (a.cycles != b.cycles)
	{
		std::snprintf(text, sizeof(text), "cycles %llu/%llu ", (unsigned long long)a.cycles, (unsigned long long)b.cycles);
		found += text;
	}
	if (a.end == FC_RETURNED)
	{
		static const struct { u32 bit; const char *name; u32 fc_registers::*field; u32 mask; } registers[] = {
			{ FC_A, "A", &fc_registers::a, 0xFF }, { FC_B, "B", &fc_registers::b, 0xFF },
			{ FC_DP, "DP", &fc_registers::dp, 0xFF }, { FC_X, "X", &fc_registers::x, 0xFFFF },
			{ FC_Y, "Y", &fc_registers::y, 0xFFFF }, { FC_U, "U", &fc_registers::u, 0xFFFF },
			{ FC_S, "S", &fc_registers::s, 0xFFFF } };
		for (const auto &r : registers)
			if ((compared & r.bit) && ((a.registers.*r.field ^ b.registers.*r.field) & r.mask))
			{
				std::snprintf(text, sizeof(text), "%s %X/%X ", r.name, a.registers.*r.field & r.mask,
					b.registers.*r.field & r.mask);
				found += text;
			}
		for (int bit = 0; bit < 8; bit++)
			if ((compared >> (FC_CC_SHIFT + bit) & 1) && ((a.registers.cc ^ b.registers.cc) >> bit & 1))
			{
				std::snprintf(text, sizeof(text), "CC.%c %u/%u ", "CVZNIHFE"[bit], a.registers.cc >> bit & 1,
					b.registers.cc >> bit & 1);
				found += text;
			}
		if ((a.registers.s ^ b.registers.s) & 0xFFFF)
		{
			std::snprintf(text, sizeof(text), "S %X/%X ", a.registers.s, b.registers.s);
			found += text;
		}
	}
	int shown = 0, differing = 0;
	std::string bytes;
	// (what the C cannot have as the board does: the stack below S, where the IRQ's entry pushes and the code's dead
	// pushes are; the stack pointer the round robin keeps at <$E2-<$E3: gng_stack_byte)
	const size_t ram = offsetof(gng_board, ram);
	auto left_out = [&](size_t offset) {
		const size_t address = offset - ram;
		return offset >= ram && (address == 0xE2 || address == 0xE3 || (address >= 0x0280 && address < a.registers.s));
	};
	for (size_t offset = 0; offset < BOARD_BYTES; offset++)
		if (a.board[offset] != b.board[offset] && !left_out(offset))
		{
			if (shown < 6)
			{
				std::snprintf(text, sizeof(text), "%s:%02X/%02X ", board_place(offset).c_str(), a.board[offset], b.board[offset]);
				bytes += text;
				shown++;
			}
			differing++;
		}
	if (differing)
	{
		std::snprintf(text, sizeof(text), "%d bytes: ", differing);
		found += text + bytes;
	}
	if (a.clock != b.clock)
	{
		size_t i = 0;
		while (i < a.clock.size() && i < b.clock.size() && a.clock[i] == b.clock[i])
			i++;
		std::snprintf(text, sizeof(text), "board's time asked %zu/%zu times, the %zuth at %lld/%lld ", a.clock.size(),
			b.clock.size(), i + 1, i < a.clock.size() ? (long long)a.clock[i] : -1LL,
			i < b.clock.size() ? (long long)b.clock[i] : -1LL);
		found += text;
	}
	if (a.latch != b.latch)
	{
		std::snprintf(text, sizeof(text), "latch writes %zu/%zu ", a.latch.size(), b.latch.size());
		found += text;
	}
	return found;
}

// registers a function may be called with: a record's, or anything
static u16 made_up_pointer()
{
	switch (next_random() % 6)
	{
	case 0: return u16(0x0880 + 0x30 * (next_random() % 40) + 0x10);    // an object (X = record + 0x10)
	case 1: return u16(0x0510);                                         // the player's
	case 2: return u16(0x0530 + 0x40 * (next_random() % 4) + 0x10);     // a shot's
	case 3: return u16(0x1000 + 0x10 * (next_random() % 0x70));         // the records at 1000-16FF
	case 4: return u16(next_random() & 0x1FFF);
	default: return u16(next_random());
	}
}

static u8 made_up_byte()
{
	static const u8 usual[] = { 0x00, 0x01, 0x02, 0x80, 0xFF, 0x7F, 0x10 };
	return next_random() % 3 ? u8(next_random()) : usual[next_random() % sizeof(usual)];
}

int main(int argc, char **argv)
{
	std::string regions_path = "build/makaimurg.regions";
	const char *inputs_path = nullptr, *only = nullptr;
	int frames = 3000, variants = 24;
	unsigned seed = 1;
	bool trace_wanted = false;
	cap = 2000000;
	for (int i = 1; i < argc; i++)
	{
		std::string a = argv[i];
		auto next = [&]() -> const char * { if (i + 1 >= argc) { std::fprintf(stderr, "fcheck: %s needs a value\n", a.c_str()); std::exit(2); } return argv[++i]; };
		if (a == "--regions") regions_path = next();
		else if (a == "--accuracy")
		{
			const std::string mode = next();
			if (mode != "mame" && mode != "board") { std::fprintf(stderr, "fcheck: --accuracy mame or board\n"); return 2; }
			board_mode = mode == "board";
		}
		else if (a == "--inputs") inputs_path = next();
		else if (a == "--frames") frames = std::atoi(next());
		else if (a == "--samples") samples = std::atoi(next());
		else if (a == "--variants") variants = std::atoi(next());
		else if (a == "--seed") seed = unsigned(std::atoi(next()));
		else if (a == "--cap") cap = u64(std::atoll(next()));
		else if (a == "--function") only = next();
		else if (a == "--trace") trace_wanted = true;
		else { std::fprintf(stderr, "fcheck: unknown option %s\n", a.c_str()); return 2; }
	}
	auto regions = read_regions(regions_path.c_str());
	gng_roms roms = {};
	roms.maincpu = regions["maincpu"].data();
	if (regions.count("audiocpu")) roms.audiocpu = regions["audiocpu"].data();
	if (regions.count("chars")) roms.chars = regions["chars"].data();
	if (regions.count("tiles")) roms.tiles = regions["tiles"].data();
	if (regions.count("sprites")) roms.sprites = regions["sprites"].data();
	gng.accuracy = board_mode ? GNG_ACCURACY_BOARD : GNG_ACCURACY_MAME;
	gng_power_on(&roms);
	gng.inputs.dsw1 = GNG_DSW1_DEFAULT;
	gng.inputs.dsw2 = GNG_DSW2_DEFAULT;
	gng.on_sound_latch = on_latch;
	gng.clock = clock_now;
	gng.on_dma = on_dma;
	gng_video_decode();

	{
		std::ifstream code("build/fcheck/code.txt");
		std::string line;
		while (std::getline(code, line))
			if (line.rfind("T ", 0) == 0)
			{
				std::istringstream words(line.substr(2));
				std::string word;
				words >> word;
				table_jump &jump = table_jumps[u32(std::strtoul(word.c_str(), nullptr, 16))];
				unsigned count = 0;
				words >> word >> count;
				jump.tables.emplace_back(u16(std::strtoul(word.c_str(), nullptr, 16)), count);
				while (words >> word)
					jump.targets.insert(u32(std::strtoul(word.c_str(), nullptr, 16)));
			}
			else if (!line.empty())
				known_code[std::strtoul(line.c_str(), nullptr, 16)] = true;
		if (!known_code[0x6000])
		{
			std::fprintf(stderr, "fcheck: no build/fcheck/code.txt (python tools/m6809/check_functions.py --write)\n");
			return 2;
		}
	}
	kept.resize(fc_function_count);
	seen.resize(fc_function_count);
	for (unsigned i = 0; i < fc_function_count; i++)
		index_of_key[fc_functions[i].key] = int(i);
	rng_state = 0x9E3779B97F4A7C15ull ^ seed;

	m6809_host::read = host_read;
	m6809_host::read_opcode = host_read_opcode;
	m6809_host::write = host_write;
	m6809_host::acknowledge = host_acknowledge;
	m6809_host::instruction = host_instruction;
	reference_cpu reference;
	core = &reference;
	reference.start();
	reference.reset();

	// the original's run: the states kept
	std::vector<input_change> inputs;
	if (inputs_path)
		inputs = read_inputs(inputs_path);
	size_t next_input = 0;
	for (int frame = 0; frame < frames; frame++)
	{
		run_until(GNG_VBLANK_CYCLE + u64(frame) * GNG_CPU_CYCLES_PER_FRAME);
		while (next_input < inputs.size() && inputs[next_input].frame <= frame)
			apply_input(inputs[next_input++]);
		irq_held = true;
		reference.set_input_line(M6809_IRQ_LINE, ASSERT_LINE);
	}
	std::vector<const state *> pool;
	for (const auto &states : kept)
		for (const auto &s : states)
			if (s.booted)
				pool.push_back(&s);    // (the others' states after the boot: before it S is not set yet)

	// the values each byte of the work RAM held in the states kept (what a changed state takes: values the game
	// gives that byte, if not together)
	std::vector<std::vector<u8>> domain(0x1E00);
	{
		std::vector<std::set<u8>> values(0x1E00);
		for (const state *s : pool)
			for (unsigned address = 0; s->booted && address < 0x1E00; address++)
				if (values[address].size() < 64)
					values[address].insert(reinterpret_cast<const gng_board *>(s->board.data())->ram[address]);
		for (unsigned address = 0; address < 0x1E00; address++)
			domain[address].assign(values[address].begin(), values[address].end());
	}

	// each function: from its own states (real), changed with values the game gives (plausible), or anything (wild);
	// one the run never calls from the others' states, its registers made up (unowned)
	enum kind { REAL, PLAUSIBLE, WILD, UNOWNED, KINDS };
	static const char *const kind_names[KINDS] = { "real", "plausible", "wild", "unowned" };
	int differing_functions = 0;
	for (unsigned index = 0; index < fc_function_count; index++)
	{
		const fc_function &function = fc_functions[index];
		if (only && std::strcmp(only, function.name))
			continue;
		rng_state = (0x9E3779B97F4A7C15ull ^ seed) + 0x100000001B3ull * (index + 1);
		next_random();
		const u16 pc = u16(function.key & 0xFFFF);
		const u8 bank = function.key > 0xFFFF ? u8((function.key >> 16) - 1) : 0xFF;
		const std::vector<state> &own = kept[index];
		std::vector<const state *> bases;
		if (!own.empty())
			for (const auto &s : own)
				bases.push_back(&s);
		else
			for (int i = 0; i < samples && !pool.empty(); i++)
				bases.push_back(pool[next_random() % pool.size()]);
		int runs[KINDS] = {}, different[KINDS] = {}, off[KINDS] = {};
		std::string first[KINDS];
		std::set<u16> read_union;
		const int wild = own.empty() ? 0 : (variants + 3) / 4;
		for (const state *base : bases)
		{
			for (int variant = 0; variant <= variants + wild; variant++)
			{
				const kind k = own.empty() ? UNOWNED : variant == 0 ? REAL : variant <= variants ? PLAUSIBLE : WILD;
				fc_registers registers = base->registers;
				std::vector<u8> board = base->board;
				gng_board *b = reinterpret_cast<gng_board *>(board.data());
				if (bank != 0xFF)
					b->bank = bank;    // (another function's state: the window this one is in)
				if (k == PLAUSIBLE)
				{
					// registers from the function's other calls, RAM it read from values the game gives it
					auto other = [&]() -> const fc_registers & { return own[next_random() % own.size()].registers; };
					if (next_random() % 2) registers.a = other().a;
					if (next_random() % 2) registers.b = other().b;
					if (next_random() % 2) registers.x = other().x;
					if (next_random() % 2) registers.y = other().y;
					if (next_random() % 2) registers.u = other().u;
					if (next_random() % 2) registers.cc = (registers.cc & 0x10) | (other().cc & ~0x10u);
					for (u16 address : read_union)
						if (!(address >= 0x0280 && address < 0x0300) && next_random() % 6 == 0 && !domain[address].empty())
							b->ram[address] = domain[address][next_random() % domain[address].size()];
				}
				else if (k == WILD || k == UNOWNED)
				{
					registers.a = u8(next_random());
					registers.b = u8(next_random());
					registers.x = made_up_pointer();
					registers.y = made_up_pointer();
					registers.u = made_up_pointer();
					registers.cc = (registers.cc & 0x50) | (u32(next_random()) & 0xAF);
					for (u16 address : read_union)
						if (!(address >= registers.s && address < registers.s + 2) && next_random() % 6 == 0)
							b->ram[address] = made_up_byte();
					// what every caller agrees a register holds (the C may take it as given): that value, or a
					// pointer into an object's record (the 40 at 0880, the player's at 0500, the 4 shots' at 0530)
					for (unsigned i = 0; i < function.kind_count; i++)
					{
						const fc_entry_kind &given = function.kinds[i];
						u16 value = given.value;
						if (given.kind == 'o')
						{
							const unsigned slot = unsigned(next_random() % 45);
							value = u16((slot < 40 ? 0x0880 + 0x30 * slot : slot == 40 ? 0x0500 : 0x0530 + 0x40 * (slot - 41))
								+ given.value);
						}
						switch (given.reg)
						{
						case 'x': registers.x = value; break;
						case 'y': registers.y = value; break;
						case 'u': registers.u = value; break;
						default: registers.a = value >> 8; registers.b = value & 0xFF; break;
						}
					}
				}
				time_base = base->time;
				const outcome reference_run = run_core(pc, registers, board);
				read_union.insert(reads.begin(), reads.end());
				if (reference_run.end == FC_OFF_CODE)
				{
					off[k]++;    // (the original went where the static analysis never found code: not compared)
					continue;
				}
				const outcome c_run = run_c(index, registers, board);
				if (c_run.outside)
				{
					off[k]++;    // (the C reached past the work RAM: a pointer the game never has; not compared)
					continue;
				}
				const std::string difference = compare(reference_run, c_run, function.compared);
				runs[k]++;
				if (!difference.empty())
				{
					different[k]++;
					if (first[k].empty())
					{
						char text[200];
						std::snprintf(text, sizeof(text), "%s [A=%02X B=%02X X=%04X Y=%04X U=%04X S=%04X CC=%02X bank=%u] ",
							kind_names[k], registers.a, registers.b, registers.x, registers.y, registers.u, registers.s,
							registers.cc, b->bank);
						first[k] = text + difference;
						if (trace_wanted)
						{
							std::fprintf(stderr, "%s: %s\n", function.name, first[k].c_str());
							tracing = true;
							run_core(pc, registers, board);
							tracing = false;
							const outcome again = run_core(pc, registers, board);
							for (size_t offset = 0; offset < BOARD_BYTES; offset++)
								if (again.board[offset] != c_run.board[offset])
									std::fprintf(stderr, "  %s: MAME %02X, C %02X (before %02X)\n", board_place(offset).c_str(),
										again.board[offset], c_run.board[offset], board[offset]);
						}
					}
				}
			}
		}
		if (different[REAL] || different[PLAUSIBLE] || different[UNOWNED])
			differing_functions++;
		// "F name" then for each kind (real, plausible, wild, unowned) the runs compared, how many differ and how many
		// went off the code; then a tab before each kind's first difference
		std::printf("F %s", function.name);
		for (int k = 0; k < KINDS; k++)
			std::printf(" %d %d %d", runs[k], different[k], off[k]);
		for (int k = 0; k < KINDS; k++)
			if (!first[k].empty())
				std::printf("\t%s", first[k].c_str());
		std::printf("\n");
		std::fflush(stdout);
	}
	return differing_functions ? 1 : 0;
}
