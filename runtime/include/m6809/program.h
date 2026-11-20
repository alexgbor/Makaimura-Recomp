/**
 *    Name:        program.h
 *    Description: what the decompiled program (src/, tools/m6809/decompile.py) calls: the work RAM and the program
 *                 ROM by address, the board's bus, and the board's time as the instructions take it
 *    Version:     0.1.0
 *    Created:     20-11-2026
 *
 *    license:BSD-3-Clause
 *    The time is the translation's (m6809/cpu.h), kept by the C instruction by instruction (tools/m6809/function.py):
 *    m6809_block(n) after each instruction (the IRQ taken there, a call of its handler, program_irq), m6809_at(c)
 *    before an access that may reach the board's devices (1E00-3FFF: its time is the end of its bus cycle, c
 *    cycles from the instruction's start), m6809_cycles(n) on a long branch's longer way. The work RAM's accesses
 *    need no time: nothing but the 6809 reads it. The interrupt mask (CC's I) is the runtime's: m6809_mask(i),
 *    m6809_masked(). The runtime is runtime/src/m6809_program.c.
 */
#ifndef M6809_PROGRAM_H
#define M6809_PROGRAM_H

#include <stdbool.h>
#include <stdint.h>

#include "gng/board.h"
#include "m6809/cpu.h"

#ifdef __cplusplus
extern "C" {
#endif

/* the time the instruction being run started at (a DMA's halt in it included) */
extern uint64_t m6809_start;

/* the work RAM (0000-1DFF) by address; the program's data by address, through the board (gng_read: a pointer the
   C reads data through may lead into the fixed ROM, the banked window in the latch's bank, or the work RAM, as its
   callers decide) */
#define RAM8(address) (gng.ram[(uint16_t)(address)])
#define ROM8(address) (gng_read((uint16_t)(address)))
static inline uint8_t *ram_at(uint32_t address) { return &gng.ram[(uint16_t)address]; }
static inline const uint8_t *rom_at(uint32_t address) { return &gng.roms.maincpu[(uint16_t)address]; }

/* an instruction's end: its cycles pass, then the board's VBLANK and the IRQ the mask lets through */
void m6809_boundary(void);
static inline void m6809_block(uint32_t cycles)
{
	m6809_start += cycles;
	cpu.cycles = m6809_start;
	if (cpu.cycles >= m6809_event_at)
		m6809_boundary();
}

/* the extra cycles of a long branch taken (before its block) */
static inline void m6809_cycles(uint32_t cycles)
{
	m6809_start += cycles;
}

/* an access whose time shows: the end of its bus cycle, 'cycle' cycles into the instruction */
static inline void m6809_at(uint32_t cycle)
{
	cpu.cycles = m6809_start + cycle;
}

/* CC's I */
void m6809_mask(uint32_t masked);
static inline uint32_t m6809_masked(void) { return cpu.fi; }

/* the flags of a subtraction or an addition with a borrow or a carry in (the C's carry_sub(a, b, c)...), as MAME's
   set_flags computes them: C the bit past the width of the 32-bit result, V (a ^ b ^ r ^ (r >> 1)) at the top */
#define M6809_FLAG_HELPERS(width) \
	static inline uint32_t m6809_carry_sub##width(uint32_t a, uint32_t b, uint32_t c) \
	{ \
		return ((a - b - c) >> width) & 1u; \
	} \
	static inline uint32_t m6809_overflow_sub##width(uint32_t a, uint32_t b, uint32_t c) \
	{ \
		const uint32_t r = a - b - c; \
		return ((a ^ b ^ r ^ (r >> 1)) >> (width - 1)) & 1u; \
	} \
	static inline uint32_t m6809_carry_add##width(uint32_t a, uint32_t b, uint32_t c) \
	{ \
		return ((a + b + c) >> width) & 1u; \
	} \
	static inline uint32_t m6809_overflow_add##width(uint32_t a, uint32_t b, uint32_t c) \
	{ \
		const uint32_t r = a + b + c; \
		return ((a ^ b ^ r ^ (r >> 1)) >> (width - 1)) & 1u; \
	}
M6809_FLAG_HELPERS(8)
M6809_FLAG_HELPERS(16)

/* where the main loop waits for work (symbols.txt's idle), with what it keeps in U: a state may be taken there */
void m6809_idle(uint32_t u);

/* the decompiled program's (src/vectors.c): the reset's code, the IRQ's handler (up to its RTI), and the main loop
   waiting for work at program_idle_address, from a state */
void program_reset(void);
void program_irq(void);
void program_resume(uint16_t u);
extern const uint16_t program_idle_address;

#ifdef __cplusplus
}
#endif

#endif
