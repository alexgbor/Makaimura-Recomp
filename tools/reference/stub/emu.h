/**
 *    Name:        emu.h
 *    Description: a minimal stand-in for MAME's emu.h, enough to build MAME's MC6809 core and its disassembler
 *                 (tools/reference/mame/m6809) on their own, unchanged
 *    Version:     0.1.0
 *    Created:     19-11-2026
 *
 *    license:BSD-3-Clause
 *    In the way of arcade-decomp's konami/reference/stub/emu.h (Sebastian Toyos), for the 6809: an 8-bit bus
 *    whose reads and writes go to functions the host program sets (bus_read, bus_write), the device framework
 *    reduced to what m6809.cpp touches (no scheduler: the host runs the core with run(cycles) and sets its
 *    lines), save states and the debugger left out.
 */
#pragma once

#include <array>
#include <cassert>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <ostream>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

using u8 = uint8_t;
using u16 = uint16_t;
using u32 = uint32_t;
using u64 = uint64_t;
using s8 = int8_t;
using s16 = int16_t;
using s32 = int32_t;
using s64 = int64_t;
using offs_t = uint32_t;

#define LSB_FIRST 1
#define ATTR_COLD
#define ATTR_FORCE_INLINE
#define UNEXPECTED(x) (x)
#define NAME(x) x, #x
#define FUNC(x) &x, #x
#define DECLARE_DEVICE_TYPE(Type, Class) extern const device_type Type;
#define DEFINE_DEVICE_TYPE(Type, Class, ShortName, FullName) const device_type Type = ShortName;
#define BIT(x, n) (((x) >> (n)) & 1)

enum { CLEAR_LINE = 0, ASSERT_LINE = 1, HOLD_LINE = 2 };
enum { INPUT_LINE_NMI = 32 };
enum { AS_PROGRAM = 0, AS_DATA = 1, AS_IO = 2, AS_OPCODES = 3 };
enum { STATE_GENPC = -1, STATE_GENPCBASE = -2, STATE_GENFLAGS = -3 };
enum endianness_t { ENDIANNESS_LITTLE, ENDIANNESS_BIG };

[[noreturn]] inline void fatalerror(const char *format, ...)
{
	char message[512];
	va_list arguments;
	va_start(arguments, format);
	std::vsnprintf(message, sizeof(message), format, arguments);
	va_end(arguments);
	throw std::runtime_error(message);
}

// MAME's PAIR16 (little-endian host)
union PAIR16
{
	struct { u8 l, h; } b;
	struct { s8 l, h; } sb;
	u16 w;
	s16 sw;
};

namespace util {

template <typename T> T format_argument(T value) { return value; }
inline const char *format_argument(const std::string &value) { return value.c_str(); }

template <typename... Args>
std::string string_format(const char *format, Args... args)
{
	char buffer[512];
	std::snprintf(buffer, sizeof(buffer), format, format_argument(args)...);
	return buffer;
}

template <typename... Args>
void stream_format(std::ostream &stream, const char *format, Args... args)
{
	stream << string_format(format, args...);
}

class disasm_interface
{
public:
	enum : u32
	{
		LENGTHMASK = 0x0000ffff,
		STEP_COND = 0x10000000,
		STEP_OVER = 0x20000000,
		STEP_OUT = 0x40000000,
		SUPPORTED = 0x80000000
	};

	// Big-endian reads from a flat 64 KB image
	class data_buffer
	{
	public:
		data_buffer(const u8 *data, u32 size) : m_data(data), m_size(size) {}
		u8 r8(offs_t address) const { address &= 0xffff; return address < m_size ? m_data[address] : 0xff; }
		u16 r16(offs_t address) const { return u16((r8(address) << 8) | r8(address + 1)); }
		u32 r32(offs_t address) const { return (u32(r16(address)) << 16) | r16(address + 2); }

	private:
		const u8 *m_data;
		u32 m_size;
	};

	virtual ~disasm_interface() = default;
	virtual u32 opcode_alignment() const = 0;
	virtual offs_t disassemble(std::ostream &stream, offs_t pc, const data_buffer &opcodes, const data_buffer &params) = 0;
};

} // namespace util

using util::string_format;

// ---- the device framework, as far as m6809.cpp uses it ----

class machine_config {};
class device_t {};
using device_type = const char *;

struct running_machine
{
	std::string describe_context() const { return std::string(); }
};

class address_space_config
{
public:
	address_space_config(const char *name, endianness_t endian, int data_width, int address_width, int shift = 0)
	{
		(void)name; (void)endian; (void)data_width; (void)address_width; (void)shift;
	}
};

class device_memory_interface
{
public:
	using space_config_vector = std::vector<std::pair<int, const address_space_config *>>;
};

class device_state_entry
{
public:
	explicit device_state_entry(int index) : m_index(index) {}
	int index() const { return m_index; }
	device_state_entry &callimport() { return *this; }
	device_state_entry &noshow() { return *this; }
	device_state_entry &formatstr(const char *) { return *this; }
	device_state_entry &mask(u64) { return *this; }

private:
	int m_index;
};

// The host's bus: every access the core makes goes through these (gref.cpp sets them)
namespace m6809_host {
inline u8 (*read)(u16 address) = nullptr;            // a data read
inline u8 (*read_opcode)(u16 address) = nullptr;     // an opcode or operand fetch (the same bus on this board)
inline void (*write)(u16 address, u8 data) = nullptr;
inline void (*acknowledge)(int line) = nullptr;      // standard_irq_callback: the CPU takes an interrupt
inline void (*instruction)(u16 pc) = nullptr;        // debugger_instruction_hook: an instruction starts
}

template <int Width, int AddrShift, int Level, endianness_t Endian>
struct memory_access
{
	struct cache
	{
		u8 read_byte(offs_t address) { return m6809_host::read_opcode(u16(address)); }
	};
	struct specific
	{
		u8 read_byte(offs_t address) { return m6809_host::read(u16(address)); }
		void write_byte(offs_t address, u8 data) { m6809_host::write(u16(address), data); }
	};
};

class address_space
{
public:
	template <typename T> void cache(T &) {}
	template <typename T> void specific(T &) {}
};

class devcb_write_line
{
public:
	template <typename T> explicit devcb_write_line(T &) {}
	void operator()(int) {}
	int bind() { return 0; }
};

class devcb_read8
{
public:
	template <typename T> devcb_read8(T &, u8) {}
	bool isunset() const { return true; }
	u8 operator()(offs_t) { return 0; }
	int bind() { return 0; }
};

class cpu_device : public device_t, public device_memory_interface
{
public:
	cpu_device(const machine_config &, device_type, const char *, device_t *, u32) {}
	virtual ~cpu_device() = default;

	// the host's API
	void start() { device_start(); }
	void reset() { device_reset(); }
	// runs 'cycles' cycles (the core may stop inside an instruction and go on at the next run); the cycles used
	int run(int cycles)
	{
		*m_icountptr = cycles;
		execute_run();
		return cycles - *m_icountptr;
	}
	void set_input_line(int line, int state) { execute_set_input(line, state); }

protected:
	virtual void device_start() {}
	virtual void device_reset() {}
	virtual void device_pre_save() {}
	virtual void device_post_load() {}
	virtual u32 execute_min_cycles() const noexcept { return 1; }
	virtual u32 execute_max_cycles() const noexcept { return 1; }
	virtual void execute_run() {}
	virtual void execute_set_input(int, int) {}
	virtual bool execute_input_edge_triggered(int) const noexcept { return false; }
	virtual u64 execute_clocks_to_cycles(u64 clocks) const noexcept { return clocks; }
	virtual u64 execute_cycles_to_clocks(u64 cycles) const noexcept { return cycles; }
	virtual space_config_vector memory_space_config() const { return {}; }
	virtual std::unique_ptr<util::disasm_interface> create_disassembler() { return nullptr; }
	virtual void state_import(const device_state_entry &) {}
	virtual void state_string_export(const device_state_entry &, std::string &) const {}

	address_space &space(int) { return m_space; }
	bool has_space(int) const { return false; }
	bool has_configured_map(int) const { return false; }
	template <typename T> device_state_entry &state_add(int index, const char *, T &)
	{
		m_entries.emplace_back(std::make_unique<device_state_entry>(index));
		return *m_entries.back();
	}
	template <typename T> void save_item(T &&, const char *) {}
	void set_icountptr(int &icount) { m_icountptr = &icount; }
	void standard_irq_callback(int line, offs_t) { if (m6809_host::acknowledge) m6809_host::acknowledge(line); }
	void debugger_instruction_hook(offs_t pc) { if (m6809_host::instruction) m6809_host::instruction(u16(pc)); }
	void debugger_exception_hook(int) {}
	void debugger_wait_hook() {}
	template <typename... Args> void logerror(const char *, Args &&...) {}
	running_machine &machine() { return m_machine; }

private:
	int *m_icountptr = nullptr;
	address_space m_space;
	running_machine m_machine;
	std::vector<std::unique_ptr<device_state_entry>> m_entries;
};
