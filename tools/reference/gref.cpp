/**
 *    Name:        gref.cpp
 *    Description: the reference: Makaimura's original 6809 program on MAME's MC6809 core (mame/m6809,
 *                 unchanged), against the board model (runtime/), a frame at a time, as MAME times it
 *    Version:     0.1.0
 *    Created:     19-11-2026
 *
 *    license:BSD-3-Clause
 *    The timing is MAME's for gng.cpp: the MC6809 at 1.5 MHz, the screen 384x262 at 6 MHz (25152 CPU cycles a
 *    frame), the IRQ held from each VBLANK's start (line 240: 23040 cycles into the frame) until the CPU takes it
 *    (irq0_line_hold). The 6809's program reads nothing the sound board writes, so it runs alone here; the sound
 *    latch's writes are logged with their cycle for the sound side.
 *
 *    gref [options]
 *      --accuracy MODE   mame (the default: MAME 0.289's board) or board (the real board's: docs/accuracy.md)
 *      --regions FILE    the ROM regions (python tools/makaimura_rom.py --extract FILE); default build/makaimurg.regions
 *      --frames N        how many frames (VBLANKs) to run (default 600)
 *      --hashes FILE     each frame's state hash (at its VBLANK, before the IRQ): "frame hash"
 *      --bus-hashes FILE each frame's hash of the bus 0000-2FFF and the PC, as tools/mame/trace.lua writes MAME's
 *      --bus-dumps FILE  the bus bytes 0000-2FFF of every frame
 *      --irq-hashes FILE as the CPU takes each IRQ (its state pushed): "n hash pc cycle", the bus 0000-2FFF's
 *                        FNV-1a, the PC it comes back to and the board's time (what the translation is checked by)
 *      --executed FILE   every instruction address the program ran, with its bank where 4000-5FFF: "bank:addr"
 *      --jumps FILE      every transfer of control the program made (from the instruction before), "from to",
 *                        each as the --executed addresses: the jumps through registers and tables, checked
 *      --registers-at ADDR [N]   the registers each time the instruction at ADDR (hex) starts, N times (10)
 *      --inputs FILE     an input script: lines "FRAME PORT VALUE" (PORT system, p1, p2, dsw1, dsw2; VALUE hex,
 *                        active low as the board reads it) applied at that frame's VBLANK, for the frames after
 *      --dsw1 HEX, --dsw2 HEX   the DIP switches from power-on (as the board reads them, active low; the
 *                        defaults DF and FB: gng/board.h)
 *      --sound FILE      the sound board too (tools/reference/sound: MAME's Z80, ymfm's YM2203s), and at each
 *                        VBLANK its state: "frame hash pc time", the Z80's RAM's FNV-1a, its PC, its time (Z80 cycles)
 *      --ym-writes FILE  with --sound: every YM2203 write, "time chip offset value" (Z80 cycles)
 *      --sound-marks FILE  with --sound: the Z80's RAM as it makes each YM2203 write, "write hash pc" (MAME's
 *                        run hashes it at the first write after each frame: tools/mame/trace.lua's MK_MARKS)
 *      --wav FILE        with --sound: the mix as MAME routes it, at the chips' 125 kHz
 *      --sound-executed FILE  with --sound: every address the Z80 started an instruction at, one a line (hex)
 *      --latch-writes FILE  every write of the sound latch: "time value" (6809 cycles)
 *      --pictures N FILE every Nth frame's picture (from frame 0), 256x224 RGB bytes each, as MAME's (trace.lua)
 *      --picture-hashes FILE   each frame's picture sampled as --pictures 1 samples it, its FNV-1a: "frame hash"
 *      --ppm FRAME FILE  that frame's picture as a PPM image
 *      --text FRAME      the text layer's codes at that frame, as hex
 *      --dump FRAME FILE the board's memories at that frame (RAM, sprites, layers, palette), raw
 */
#include "emu.h"
#include "m6809.h"

#include "6x09dasm.h"
#include "gng/board.h"
#include "gng/video.h"
#include "gsound.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

// The core's class is protected where the host needs it: a subclass opens it
class reference_cpu : public mc6809_device
{
public:
	reference_cpu() : mc6809_device(machine_config(), "maincpu", nullptr, 6000000) {}
	u16 pc() const { return m_pc.w; }
	u8 cc() const { return m_cc; }
	int icount() const { return m_icount; }
	void print_registers(FILE *out) const
	{
		std::fprintf(out, "PC=%04X A=%02X B=%02X X=%04X Y=%04X U=%04X S=%04X DP=%02X CC=%02X bank=%u\n", m_pc.w,
			m_q.r.a, m_q.r.b, m_x.w, m_y.w, m_u.w, m_s.w, m_dp, m_cc, gng.bank);
	}
};

static reference_cpu *cpu;
static bool irq_held;
static std::set<u32> executed;
static bool trace_executed;
static u64 cycle_base;    // the board's time at the current run's start
static int run_length;
static std::vector<std::pair<u64, u8>> latch_writes;

static u8 host_read(u16 address) { return gng_read(address); }
static void host_write(u16 address, u8 data) { gng_write(address, data); }

static FILE *irq_hashes;
static FILE *picture_hashes;
static u64 irqs_taken;

static void host_acknowledge(int line)
{
	if (line == M6809_IRQ_LINE && irq_hashes)
	{
		// the bus as the CPU takes the IRQ (its state pushed), the PC it will come back to, the board's time
		u32 hash = 2166136261u;
		for (unsigned address = 0; address < 0x3000; address++)
			hash = (hash ^ gng_read(u16(address))) * 16777619u;
		std::fprintf(irq_hashes, "%llu %08x %04x %llu\n", (unsigned long long)irqs_taken++, hash, cpu->pc(),
			(unsigned long long)(cycle_base + u64(run_length - cpu->icount())));
	}
	if (line == M6809_IRQ_LINE && irq_held)
	{
		irq_held = false;    // HOLD_LINE: cleared as the CPU takes it
		cpu->set_input_line(M6809_IRQ_LINE, CLEAR_LINE);
	}
}

static bool trace_jumps;
static u32 previous_key = 0xFFFFFFFFu;
static u16 previous_end;
static std::set<std::pair<u32, u32>> jumps;

static u32 place_key(u16 pc)
{
	return pc >= 0x4000 && pc < 0x6000 ? (u32(gng.bank) << 16) | pc : 0xFF0000u | pc;
}

// The length of the instruction at 'pc' (MAME's disassembler on the bus as the CPU sees it now: its 16 bytes
// copied into an image, so that the disassembler's reads have no effect on the board)
static u16 instruction_length(u16 pc)
{
	static std::vector<u8> image(0x10000, 0x12);
	static util::disasm_interface::data_buffer buffer(image.data(), 0x10000);
	static m6809_disassembler disassembler;
	for (unsigned i = 0; i < 16; i++)
		image[u16(pc + i)] = gng_read(u16(pc + i));
	std::ostringstream text;
	return u16(disassembler.disassemble(text, pc, buffer, buffer) & util::disasm_interface::LENGTHMASK);
}

static int registers_at = -1, registers_left = 0;

static void host_instruction(u16 pc)
{
	const u32 here = place_key(pc);
	if (pc == registers_at && registers_left > 0)
	{
		registers_left--;
		cpu->print_registers(stdout);
	}
	if (trace_executed)
		executed.insert(here);
	if (trace_jumps)
	{
		// a transfer of control: the instruction before did not fall through to this one
		if (previous_key != 0xFFFFFFFFu && pc != previous_end)
			jumps.insert({previous_key, here});
		previous_key = here;
		previous_end = u16(pc + instruction_length(pc));
	}
}

static bool sound;
static FILE *latch_log;

static u64 board_time()
{
	// the board's time of an access: the run's start and the cycles it has used so far
	return cycle_base + u64(run_length - cpu->icount());
}

static void on_latch(gng_board *, uint8_t value)
{
	const u64 time = board_time();
	latch_writes.emplace_back(time, value);
	if (latch_log)
		std::fprintf(latch_log, "%llu %02x\n", (unsigned long long)time, value);
	if (sound)
		gs_latch(2 * time, value);    // (Z80 cycles: two a 6809 cycle)
}

static void on_ls259(gng_board *, unsigned output, unsigned value)
{
	if (sound && output == 1)
		gs_q1(2 * board_time(), int(value));
}

static std::map<std::string, std::vector<u8>> read_regions(const char *path)
{
	// "MKRG" then, for each region: its name (16 bytes, NUL padded), its size (4 bytes, little endian), its bytes
	std::map<std::string, std::vector<u8>> regions;
	std::ifstream file(path, std::ios::binary);
	char magic[4];
	if (!file.read(magic, 4) || std::string(magic, 4) != "MKRG")
	{
		std::fprintf(stderr, "gref: %s is not a regions file (python tools/makaimura_rom.py --extract %s)\n", path, path);
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
	else std::fprintf(stderr, "gref: unknown port %s\n", change.port.c_str());
}

// Runs the CPU until the board's time reaches 'until' (the core may go a cycle past it, in the middle of an
// instruction: it goes on from there at the next run, as in MAME's scheduler)
static u64 now;
static void run_until(u64 until)
{
	while (now < until)
	{
		run_length = int(until - now);
		cycle_base = now;
		now += u64(cpu->run(run_length));
	}
}

int main(int argc, char **argv)
{
	std::string regions_path = "build/makaimurg.regions";
	int frames = 600;
	const char *jumps_path = nullptr;
	const char *bus_hashes_path = nullptr, *bus_dumps_path = nullptr, *pictures_path = nullptr, *ppm_path = nullptr;
	int pictures_every = 0, ppm_frame = -1;
	const char *hashes_path = nullptr, *executed_path = nullptr, *inputs_path = nullptr, *dump_path = nullptr;
	int text_frame = -1, dump_frame = -1;
	int dsw1 = GNG_DSW1_DEFAULT, dsw2 = GNG_DSW2_DEFAULT;
	const char *sound_path = nullptr, *ym_path = nullptr, *wav_path = nullptr, *marks_path = nullptr;
	const char *sound_executed_path = nullptr;
	for (int i = 1; i < argc; i++)
	{
		std::string a = argv[i];
		auto next = [&]() -> const char * { if (i + 1 >= argc) { std::fprintf(stderr, "gref: %s needs a value\n", a.c_str()); std::exit(2); } return argv[++i]; };
		if (a == "--regions") regions_path = next();
		else if (a == "--accuracy")
		{
			const std::string mode = next();
			if (mode != "mame" && mode != "board") { std::fprintf(stderr, "gref: --accuracy mame or board\n"); return 2; }
			gng.accuracy = mode == "board" ? GNG_ACCURACY_BOARD : GNG_ACCURACY_MAME;
		}
		else if (a == "--frames") frames = std::atoi(next());
		else if (a == "--hashes") hashes_path = next();
		else if (a == "--bus-hashes") bus_hashes_path = next();
		else if (a == "--bus-dumps") bus_dumps_path = next();
		else if (a == "--irq-hashes") irq_hashes = std::fopen(next(), "w");
		else if (a == "--sound") { sound = true; sound_path = next(); }
		else if (a == "--ym-writes") ym_path = next();
		else if (a == "--sound-marks") marks_path = next();
		else if (a == "--sound-executed") sound_executed_path = next();
		else if (a == "--wav") wav_path = next();
		else if (a == "--latch-writes") latch_log = std::fopen(next(), "w");
		else if (a == "--pictures") { pictures_every = std::atoi(next()); pictures_path = next(); }
		else if (a == "--picture-hashes") { picture_hashes = std::fopen(next(), "w"); pictures_every = 1; }
		else if (a == "--ppm") { ppm_frame = std::atoi(next()); ppm_path = next(); }
		else if (a == "--executed") executed_path = next();
		else if (a == "--jumps") jumps_path = next();
		else if (a == "--registers-at")
		{
			registers_at = int(std::strtoul(next(), nullptr, 16));
			registers_left = (i + 1 < argc && argv[i + 1][0] != '-') ? std::atoi(argv[++i]) : 10;
		}
		else if (a == "--inputs") inputs_path = next();
		else if (a == "--dsw1") dsw1 = int(std::strtoul(next(), nullptr, 16)) & 0xFF;
		else if (a == "--dsw2") dsw2 = int(std::strtoul(next(), nullptr, 16)) & 0xFF;
		else if (a == "--text") text_frame = std::atoi(next());
		else if (a == "--dump") { dump_frame = std::atoi(next()); dump_path = next(); }
		else { std::fprintf(stderr, "gref: unknown option %s\n", a.c_str()); return 2; }
	}

	auto regions = read_regions(regions_path.c_str());
	if (!regions.count("maincpu"))
	{
		std::fprintf(stderr, "gref: no maincpu region in %s\n", regions_path.c_str());
		return 2;
	}
	gng_roms roms = {};
	roms.maincpu = regions["maincpu"].data();
	if (regions.count("audiocpu")) roms.audiocpu = regions["audiocpu"].data();
	if (regions.count("chars")) roms.chars = regions["chars"].data();
	if (regions.count("tiles")) roms.tiles = regions["tiles"].data();
	if (regions.count("sprites")) roms.sprites = regions["sprites"].data();
	gng_power_on(&roms);
	gng.inputs.dsw1 = u8(dsw1);
	gng.inputs.dsw2 = u8(dsw2);
	gng_video_decode();
	gng.on_sound_latch = on_latch;
	gng.on_ls259 = on_ls259;
	FILE *sound_out = nullptr, *ym_out = nullptr, *wav_out = nullptr, *marks_out = nullptr;
	if (sound)
	{
		if (!regions.count("audiocpu"))
		{
			std::fprintf(stderr, "gref: no audiocpu region in %s\n", regions_path.c_str());
			return 2;
		}
		gs_power_on(regions["audiocpu"].data());
		sound_out = std::fopen(sound_path, "w");
		if (ym_path)
			gs_log_writes(ym_out = std::fopen(ym_path, "w"));
		if (marks_path)
			gs_ram_at_writes(marks_out = std::fopen(marks_path, "w"));
		if (wav_path)
			gs_wav(wav_out = std::fopen(wav_path, "wb"));
	}

	std::vector<input_change> inputs;
	if (inputs_path)
		inputs = read_inputs(inputs_path);
	size_t next_input = 0;

	m6809_host::read = host_read;
	m6809_host::read_opcode = host_read;
	m6809_host::write = host_write;
	m6809_host::acknowledge = host_acknowledge;
	m6809_host::instruction = host_instruction;
	trace_executed = executed_path != nullptr;
	trace_jumps = jumps_path != nullptr;

	reference_cpu core;
	cpu = &core;
	core.start();
	core.reset();

	FILE *hashes = hashes_path ? std::fopen(hashes_path, "w") : nullptr;
	FILE *bus_hashes = bus_hashes_path ? std::fopen(bus_hashes_path, "w") : nullptr;
	FILE *bus_dumps = bus_dumps_path ? std::fopen(bus_dumps_path, "wb") : nullptr;
	FILE *pictures = pictures_path ? std::fopen(pictures_path, "wb") : nullptr;
	const bool sample_pictures = pictures || picture_hashes;
	static uint32_t picture[GNG_SCREEN_WIDTH * GNG_SCREEN_HEIGHT];
	static uint8_t check_indexes[GNG_SCREEN_WIDTH * GNG_SCREEN_HEIGHT];
	auto picture_rgb = [&](std::vector<u8> &rgb) {
		gng_video_render(picture);
		rgb.resize(sizeof(picture) / 4 * 3);
		for (size_t i = 0; i < sizeof(picture) / 4; i++)
		{
			rgb[3 * i] = u8(picture[i] >> 16);
			rgb[3 * i + 1] = u8(picture[i] >> 8);
			rgb[3 * i + 2] = u8(picture[i]);
		}
	};
	for (int frame = 0; frame < frames; frame++)
	{
		const u64 vblank = GNG_VBLANK_CYCLE + u64(frame) * GNG_CPU_CYCLES_PER_FRAME;
		run_until(vblank);
		if (sound)
		{
			// the sound board up to the VBLANK (MAME's timeslice ends at its timer)
			gs_run_until(2 * vblank);
			std::fprintf(sound_out, "%d %08x %04x %llu\n", frame, gs_ram_hash(), gs_pc(), (unsigned long long)gs_time());
		}
		// the VBLANK: the frame is what the board holds now; then the inputs for the next frame; then the IRQ
		if (hashes)
			std::fprintf(hashes, "%d %08x\n", frame, gng_state_hash());
		if (bus_hashes || bus_dumps)
		{
			// the bus 0000-2FFF as tools/mame/trace.lua reads it in MAME: an FNV-1a, and the PC
			u8 bus[0x3000];
			u32 hash = 2166136261u;
			for (unsigned address = 0; address < 0x3000; address++)
			{
				bus[address] = gng_read(u16(address));
				hash = (hash ^ bus[address]) * 16777619u;
			}
			if (bus_hashes)
				std::fprintf(bus_hashes, "%d %08x %04x\n", frame, hash, core.pc());
			if (bus_dumps)
				std::fwrite(bus, 1, sizeof(bus), bus_dumps);
		}
		// --pictures, as tools/mame/trace.lua samples MAME's: MAME's screen bitmap holds palette indexes drawn at
		// one VBLANK, and screen:pixels() at the next frame notifier turns them into colours with the palette of
		// that next VBLANK. So frame N's picture for the check is frame N's indexes in frame N+1's colours,
		// written at frame N+1 (what the board shows is frame N's indexes in frame N's colours: --ppm)
		if (sample_pictures && frame >= 1 && (frame - 1) % pictures_every == 0)
		{
			std::vector<u8> rgb(sizeof(check_indexes) * 3);
			for (size_t i = 0; i < sizeof(check_indexes); i++)
			{
				const u8 index = check_indexes[i];
				const int r = gng.palette_ext[index] >> 4, g = gng.palette_ext[index] & 15, b = gng.palette_base[index] >> 4;
				rgb[3 * i] = u8(r * 17);
				rgb[3 * i + 1] = u8(g * 17);
				rgb[3 * i + 2] = u8(b * 17);
			}
			if (pictures)
				std::fwrite(rgb.data(), 1, rgb.size(), pictures);
			if (picture_hashes)
			{
				u32 hash = 2166136261u;
				for (u8 byte : rgb)
					hash = (hash ^ byte) * 16777619u;
				std::fprintf(picture_hashes, "%d %08x\n", frame - 1, hash);
			}
		}
		if (sample_pictures && frame % pictures_every == 0)
			gng_video_render_indexed(check_indexes);
		if (frame == ppm_frame)
		{
			std::vector<u8> rgb;
			picture_rgb(rgb);
			{
				FILE *ppm = std::fopen(ppm_path, "wb");
				std::fprintf(ppm, "P6\n%d %d\n255\n", GNG_SCREEN_WIDTH, GNG_SCREEN_HEIGHT);
				std::fwrite(rgb.data(), 1, rgb.size(), ppm);
				std::fclose(ppm);
			}
		}
		if (frame == text_frame)
		{
			std::printf("text layer at frame %d (codes; attributes' high bits in brackets where set):\n", frame);
			for (int row = 0; row < 32; row++)
			{
				for (int column = 0; column < 32; column++)
					std::printf("%02X", gng.fg_ram[row * 32 + column]);
				std::printf("\n");
			}
		}
		if (frame == dump_frame && dump_path)
		{
			FILE *dump = std::fopen(dump_path, "wb");
			std::fwrite(gng.ram, 1, sizeof(gng.ram), dump);
			std::fwrite(gng.sprite_ram, 1, sizeof(gng.sprite_ram), dump);
			std::fwrite(gng.fg_ram, 1, sizeof(gng.fg_ram), dump);
			std::fwrite(gng.bg_ram, 1, sizeof(gng.bg_ram), dump);
			std::fwrite(gng.palette_ext, 1, sizeof(gng.palette_ext), dump);
			std::fwrite(gng.palette_base, 1, sizeof(gng.palette_base), dump);
			std::fclose(dump);
		}
		while (next_input < inputs.size() && inputs[next_input].frame <= frame)
			apply_input(inputs[next_input++]);
		irq_held = true;
		core.set_input_line(M6809_IRQ_LINE, ASSERT_LINE);
	}
	if (hashes)
		std::fclose(hashes);
	if (bus_hashes)
		std::fclose(bus_hashes);
	if (bus_dumps)
		std::fclose(bus_dumps);
	if (pictures)
		std::fclose(pictures);
	if (irq_hashes)
		std::fclose(irq_hashes);
	if (picture_hashes)
		std::fclose(picture_hashes);
	if (latch_log)
		std::fclose(latch_log);
	if (sound_out)
		std::fclose(sound_out);
	if (ym_out)
		std::fclose(ym_out);
	if (marks_out)
		std::fclose(marks_out);
	if (sound && sound_executed_path)
	{
		FILE *out = std::fopen(sound_executed_path, "w");
		const uint8_t *bits = gs_executed();
		for (unsigned address = 0; address < 0x10000; address++)
			if (bits[address >> 3] & (1u << (address & 7)))
				std::fprintf(out, "%04X\n", address);
		std::fclose(out);
	}
	if (wav_out)
		gs_close_wav();

	if (executed_path)
	{
		FILE *out = std::fopen(executed_path, "w");
		for (u32 entry : executed)
		{
			const u32 bank = entry >> 16;
			if (bank == 0xFF)
				std::fprintf(out, "%04X\n", entry & 0xFFFF);
			else
				std::fprintf(out, "%u:%04X\n", bank, entry & 0xFFFF);
		}
		std::fclose(out);
	}
	auto key_text = [](u32 key) {
		char text[16];
		if ((key >> 16) == 0xFF)
			std::snprintf(text, sizeof(text), "%04X", key & 0xFFFF);
		else
			std::snprintf(text, sizeof(text), "%u:%04X", key >> 16, key & 0xFFFF);
		return std::string(text);
	};
	if (jumps_path)
	{
		FILE *out = std::fopen(jumps_path, "w");
		for (const auto &jump : jumps)
			std::fprintf(out, "%s %s\n", key_text(jump.first).c_str(), key_text(jump.second).c_str());
		std::fclose(out);
	}
	std::printf("gref: %d frames, %llu cycles, pc %04X, bank %u, %u DMAs, %zu sound commands\n", frames,
		(unsigned long long)now, core.pc(), gng.bank, gng.dma_count, latch_writes.size());
	return 0;
}
