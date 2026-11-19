/**
 *    Name:        cpu.h
 *    Description: the MC6809 the translated program (tools/m6809/translate.py) runs on: its registers, the board's
 *                 time, the bus as the program's C reaches it, the interrupt, and the trampoline that runs the
 *                 program's regions
 *    Version:     0.1.0
 *    Created:     19-11-2026
 *
 *    license:BSD-3-Clause
 *    The time is MAME's core's: cpu.cycles counts the 6809's cycles from power-on (the reset's vector fetch is
 *    cycles 0-3), set by the C before each data access to the end of that bus cycle and after each instruction to
 *    its end. The board's VBLANK comes at 23040 + 25152 n (gng/board.h): before the first access that ends after
 *    it, or at the end of the instruction it falls in, whichever is first, as MAME's scheduler stops the core
 *    between two bus cycles; then the IRQ is held (HOLD_LINE) until the CPU takes it, at the first instruction
 *    boundary with I clear (MAME's core looks at its lines in MAIN only). The handler runs nested, from the
 *    instruction boundary where it is taken, until its RTI comes back there.
 */
#ifndef M6809_CPU_H
#define M6809_CPU_H

#include <stdint.h>

#include "gng/board.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct m6809_cpu
{
	uint32_t a, b, dp, x, y, u, s;               /* carried as 32 bits, never above their width */
	uint32_t fe, ff, fh, fi, fn, fz, fv, fc;     /* CC: E F H I N Z V C */
	uint64_t cycles;                             /* the board's time */
} m6809_cpu;

extern m6809_cpu cpu;
extern uint16_t cpu_pc;               /* the instruction a fault is about */
extern uint64_t m6809_event_at;       /* the time M6809_CHECK calls m6809_event from: the next VBLANK, 0 with an IRQ
                                         the mask lets through */
extern uint64_t m6809_board_at;       /* the next VBLANK */

typedef uint16_t (*m6809_region)(uint16_t pc);
typedef struct m6809_entry { int bank; uint16_t address; m6809_region region; } m6809_entry;
typedef struct m6809_ram_byte { uint16_t address; uint8_t value; } m6809_ram_byte;

/* the translation's (build/generated/program/entries.c) */
extern const m6809_entry m6809_entries[];
extern const unsigned m6809_entry_count;
extern const m6809_ram_byte m6809_ram_code[];
extern const unsigned m6809_ram_code_count;
uint16_t m6809_irq_entry(uint16_t pc);

/* the core's printer's helpers (tools/core/v60/emit_c.py) */
#ifndef ARCADE_DECOMP_SHIFT_HELPERS
#define ARCADE_DECOMP_SHIFT_HELPERS
static inline uint32_t shl32(uint32_t value, int count) { return value << (count & 31); }
static inline uint32_t shr32(uint32_t value, int count) { return value >> (count & 31); }
static inline int32_t sar32(int32_t value, int count) { return value >> (count & 31); }
#endif

void m6809_board_catch_up(void);

/* the bus, at the board's time cpu.cycles (the end of the access's bus cycle) */
static inline uint32_t mem_read8(uint32_t address)
{
	if (cpu.cycles > m6809_board_at)
		m6809_board_catch_up();
	return gng_read((uint16_t)address);
}

static inline void mem_write8(uint32_t address, uint8_t value)
{
	if (cpu.cycles > m6809_board_at)
		m6809_board_catch_up();
	gng_write((uint16_t)address, value);
}

/* after each instruction: the board's time, and the interrupt before the instruction at 'pc' */
#define M6809_CHECK(pc) do { if (cpu.cycles >= m6809_event_at) m6809_event((uint16_t)(pc)); } while (0)

void m6809_event(uint16_t pc);
void m6809_mask_changed(void);
void m6809_sync(void);
void m6809_cwai(uint16_t next);
uint16_t m6809_rti(uint16_t pc);
uint16_t m6809_not_code(uint16_t pc);
void m6809_fault(const char *what, uint16_t pc);

/* The host */
typedef struct m6809_host
{
	void (*vblank)(uint64_t frame);                    /* a VBLANK begins (frame 0 the first), before the IRQ */
	void (*irq_taken)(uint16_t pc, uint64_t cycles);   /* the CPU takes the IRQ before the instruction at pc:
	                                                      its state pushed (at 'cycles', as MAME's acknowledge) */
	void (*fault)(const char *what, uint16_t pc);      /* the program went where the translation has no code */
} m6809_host;

/* Power-on (after gng_power_on): the registers as MAME's core starts them, the reset's vector fetched */
void m6809_power_on(const m6809_host *host);

/* Runs the program until 'frames' VBLANKs went by (each one's host.vblank called); 0 once done, -1 on a fault */
int m6809_run(uint64_t frames);

#ifdef __cplusplus
}
#endif

#endif
