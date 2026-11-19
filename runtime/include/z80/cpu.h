/**
 *    Name:        cpu.h
 *    Description: the Z80 the translated sound program (tools/z80cpu/translate.py) runs on: its registers, its time,
 *                 the bus as the program's C reaches it, the IRQ, and the trampoline that runs it up to a time
 *    Version:     0.1.0
 *    Created:     19-11-2026
 *
 *    license:BSD-3-Clause
 *    After MAME 0.289's src/devices/cpu/z80 (BSD-3-Clause, Juergen Buchmueller and others), z80.lst's "ffff" step:
 *    between two instructions the core first stops if its run is over, then (on its way on) takes an IRQ the line
 *    holds if IFF1 is set and the instruction before was not EI. z80.cycles counts Z80 cycles from power-on, set
 *    by the C before each data access to the start of its machine cycle (where MAME's core makes it).
 */
#ifndef Z80_CPU_H
#define Z80_CPU_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct z80_cpu
{
	uint32_t bc, de, hl, ix, iy, sp, a;          /* carried as 32 bits, never above their width */
	uint32_t bc2, de2, hl2, a2;                  /* the other bank */
	uint32_t f_s, f_z, f_h, f_ov, f_n, f_cy;     /* F: S Z H P/V N C (bits 3 and 5 are not kept: lift.py) */
	uint32_t f_s_alt, f_z_alt, f_h_alt, f_ov_alt, f_n_alt, f_cy_alt;
	uint32_t iff1, iff2, im;
	int after_ei;                                /* the instruction just run was EI */
	int halted;
	int stopping;                                /* the run is over: the regions give back their next address */
	uint16_t pc;                                 /* where the next run goes on */
	uint64_t cycles;
} z80_cpu;

extern z80_cpu z80;
extern uint64_t z80_event_at;    /* the time Z80_CHECK calls z80_event from */

typedef uint16_t (*z80_region)(uint16_t pc);
typedef struct z80_entry { uint16_t address; z80_region region; } z80_entry;
extern const z80_entry z80_entries[];
extern const unsigned z80_entry_count;
uint16_t z80_irq_entry(uint16_t pc);    /* (generated: the PC pushed, IFF1 and IFF2 cleared, 13 cycles; gives 0038) */

/* the board's (gng/sound.h): the Z80's bus at its time z80.cycles */
uint32_t z80_read8(uint32_t address);
void z80_write8(uint32_t address, uint8_t value);

/* the core's printer's helpers (tools/core/v60/emit_c.py) */
#ifndef ARCADE_DECOMP_SHIFT_HELPERS
#define ARCADE_DECOMP_SHIFT_HELPERS
static inline uint32_t shl32(uint32_t value, int count) { return value << (count & 31); }
static inline uint32_t shr32(uint32_t value, int count) { return value >> (count & 31); }
static inline int32_t sar32(int32_t value, int count) { return value >> (count & 31); }
#endif

uint16_t z80_event(uint16_t pc);
#define Z80_CHECK(pc) do { if (z80.cycles >= z80_event_at) { const uint16_t z80_next_ = z80_event((uint16_t)(pc)); \
	if (z80_next_ != (uint16_t)(pc) || z80.stopping) return z80_next_; } } while (0)

void z80_di(void);
void z80_ei(void);
uint16_t z80_halt(uint16_t next);
uint16_t z80_not_code(uint16_t pc);
void z80_fault(const char *what, uint16_t pc);

/* The IRQ line (the board's 240 Hz timer: HOLD_LINE, cleared as the CPU takes it) */
void z80_set_irq(int asserted);
int z80_irq_line(void);

/* Reset (the core's device_reset: PC 0, IFF1 and IFF2 off, IM 0), at the board's time 'time' */
void z80_reset(uint64_t time);

/* Runs the program from z80.pc until z80.cycles reaches 'until' (it stops at the first instruction boundary at or
   after it); the next run goes on from there. 'next_irq' is when the board's timer next asserts the line (the
   board's callback z80_timer runs then): the trampoline takes it in time. -1 on a fault */
typedef uint64_t (*z80_timer_callback)(uint64_t time);    /* returns the timer's next time */
void z80_set_timer(uint64_t next, z80_timer_callback callback);
int z80_run(uint64_t until);
void z80_init(void (*fault)(const char *what, uint16_t pc));

#ifdef __cplusplus
}
#endif

#endif
