/**
 *    Name:        z80.c
 *    Description: the Z80 the translated sound program runs on (z80/cpu.h): the trampoline that runs it up to a
 *                 time, the dispatch of an address to its region, the IRQ (interrupt mode 1), EI's delay, HALT
 *    Version:     0.1.0
 *    Created:     19-11-2026
 *
 *    license:BSD-3-Clause
 *    After MAME 0.289's src/devices/cpu/z80 (BSD-3-Clause, Juergen Buchmueller and others): z80.lst's "ffff" step
 *    (the run's end looked at first, then the IRQ, not after EI), take_interrupt (IM 1: 0038, the PC pushed, 13
 *    cycles: the generated z80_irq_entry, checked against the core), HALT (M1 cycles of 4 until an interrupt).
 */
#include "z80/cpu.h"

#include <setjmp.h>
#include <stdio.h>
#include <string.h>

z80_cpu z80;
uint64_t z80_event_at;

static z80_region regions[0x10000];
static uint64_t stop_at;
static int line;
static uint64_t timer_at = UINT64_MAX;
static z80_timer_callback timer;
static void (*fault_hook)(const char *, uint16_t);
static jmp_buf fault_jump;

static void update_event_at(void)
{
	uint64_t at = stop_at < timer_at ? stop_at : timer_at;
	if (z80.after_ei || (line && z80.iff1))
		at = 0;
	z80_event_at = at;
}

void z80_set_irq(int asserted)
{
	line = asserted;
	update_event_at();
}

int z80_irq_line(void) { return line; }

void z80_set_timer(uint64_t next, z80_timer_callback callback)
{
	timer_at = next;
	timer = callback;
	update_event_at();
}

void z80_fault(const char *what, uint16_t pc)
{
	if (fault_hook)
		fault_hook(what, pc);
	else
		fprintf(stderr, "z80: %s at %04X (cycle %llu)\n", what, pc, (unsigned long long)z80.cycles);
	longjmp(fault_jump, 1);
}

uint16_t z80_not_code(uint16_t pc)
{
	z80_fault("not an instruction the sound translation has", pc);
	return pc;
}

void z80_di(void)
{
	z80.iff1 = z80.iff2 = 0;
	update_event_at();
}

void z80_ei(void)
{
	z80.iff1 = z80.iff2 = 1;
	z80.after_ei = 1;
	update_event_at();
}

/* the board's timer, up to the CPU's time */
static void timers(void)
{
	while (timer && z80.cycles >= timer_at)
		timer_at = timer(timer_at);
}

/* An instruction boundary, on the CPU's way on: the IRQ if it may be taken */
static uint16_t take_pending(uint16_t pc)
{
	timers();
	if (z80.after_ei)
		z80.after_ei = 0;
	else if (line && z80.iff1)
	{
		line = 0;    /* HOLD_LINE: cleared as the CPU takes it */
		if (z80.halted)
		{
			z80.halted = 0;
			pc++;
		}
		pc = z80_irq_entry(pc);
	}
	update_event_at();
	return pc;
}

uint16_t z80_event(uint16_t pc)
{
	timers();
	if (z80.cycles >= stop_at)
	{
		/* the run is over: MAME's core stops before it looks at its lines */
		z80.stopping = 1;
		return pc;
	}
	return take_pending(pc);
}

uint16_t z80_halt(uint16_t next)
{
	/* HALT: M1 cycles of 4 at the next instruction until an interrupt is taken (PC kept on the HALT's next byte
	   minus one, as the core's PC-- does: the IRQ pushes the next instruction) */
	z80.halted = 1;
	return (uint16_t)(next - 1);
}

static uint16_t dispatch(uint16_t pc)
{
	const z80_region region = regions[pc];
	if (!region)
		z80_fault("an address the sound program's static analysis did not find", pc);
	return region(pc);
}

void z80_reset(uint64_t time)
{
	z80.pc = 0;
	z80.iff1 = z80.iff2 = 0;    /* (IM kept, as the core's device_reset keeps it) */
	z80.after_ei = 0;
	z80.halted = 0;
	if (z80.cycles < time)
		z80.cycles = time;
	update_event_at();
}

int z80_run(uint64_t until)
{
	if (setjmp(fault_jump))
		return -1;
	stop_at = until;
	z80.stopping = 0;
	update_event_at();
	uint16_t pc = z80.pc;
	while (z80.cycles < until)
	{
		pc = take_pending(pc);
		if (z80.halted)
		{
			/* four cycles a step until the IRQ may be taken or the run ends */
			while (z80.halted && z80.cycles < until)
			{
				z80.cycles += 4;
				pc = take_pending(pc);
			}
			continue;
		}
		z80.stopping = 0;
		pc = dispatch(pc);
		if (z80.stopping)
			break;
	}
	z80.pc = pc;
	z80.stopping = 0;
	stop_at = UINT64_MAX;
	update_event_at();
	return 0;
}

void z80_init(void (*fault)(const char *what, uint16_t pc))
{
	fault_hook = fault;
	memset(&z80, 0, sizeof(z80));
	memset(regions, 0, sizeof(regions));
	for (unsigned i = 0; i < z80_entry_count; i++)
		regions[z80_entries[i].address] = z80_entries[i].region;
	line = 0;
	timer_at = UINT64_MAX;
	timer = 0;
	stop_at = UINT64_MAX;
	update_event_at();
}
