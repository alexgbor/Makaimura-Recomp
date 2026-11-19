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
