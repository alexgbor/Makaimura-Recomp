/**
 *    Name:        m6809.c
 *    Description: the MC6809 the translated program runs on (m6809/cpu.h): the trampoline, the dispatch of an
 *                 address to its region, the board's VBLANK and the IRQ, SYNC and CWAI
 *    Version:     0.1.0
 *    Created:     19-11-2026
 *
 *    license:BSD-3-Clause
 *    After MAME's src/devices/cpu/m6809 (BSD-3-Clause, Nathan Woods and others): the reset (m6809.cpp: the
 *    registers 0, then DP 0 and CC's I and F, the vector fetched in four cycles), the IRQ held until taken
 *    (HOLD_LINE), taken in MAIN only, SYNC (the line awaited, masked or not, then a cycle) and CWAI (the state
 *    pushed, an interrupt the mask lets through awaited, then the vector: four cycles).
 */
#include "m6809/cpu.h"

#include <setjmp.h>
#include <stdio.h>
#include <string.h>

m6809_cpu cpu;
uint16_t cpu_pc;
uint64_t m6809_event_at;
uint64_t m6809_board_at;

static m6809_host host;
static int irq_line;
static uint64_t frame;
static uint64_t frames_to_run;
static jmp_buf stop;
static int faulted;

/* the dispatch: every instruction's region, by address (and the bank in the window) */
static m6809_region fixed[0x10000];
static m6809_region banked[5][0x2000];

/* the interrupts the CPU is in, innermost last: where each one's RTI comes back */
#define DEPTH 16
static int depth;
static uint16_t return_pc[DEPTH];
static uint16_t return_s[DEPTH];
static int returned[DEPTH];

static void update_event_at(void)
{
	m6809_event_at = irq_line && !cpu.fi ? 0 : m6809_board_at;
}

void m6809_mask_changed(void)
{
	update_event_at();
}

void m6809_fault(const char *what, uint16_t pc)
{
	faulted = 1;
	if (host.fault)
		host.fault(what, pc);
	else
		fprintf(stderr, "m6809: %s at %04X (bank %u, cycle %llu)\n", what, pc, gng.bank, (unsigned long long)cpu.cycles);
	longjmp(stop, 1);
}

uint16_t m6809_not_code(uint16_t pc)
{
	m6809_fault("not an instruction the translation has", pc);
	return pc;
}

/* The board's time catches up with the CPU's: the VBLANKs it went past */
void m6809_board_catch_up(void)
{
	while (cpu.cycles >= m6809_board_at)
	{
		if (host.vblank)
			host.vblank(frame);
		frame++;
		irq_line = 1;
		m6809_board_at += GNG_CPU_CYCLES_PER_FRAME;
		if (frame >= frames_to_run)
		{
			update_event_at();
			longjmp(stop, 1);
		}
	}
	update_event_at();
}

static int ram_code_intact(void)
{
	for (unsigned i = 0; i < m6809_ram_code_count; i++)
		if (gng_read(m6809_ram_code[i].address) != m6809_ram_code[i].value)
			return 0;
	return 1;
}

static uint16_t dispatch(uint16_t pc)
{
	m6809_region region;
	if (pc >= 0x4000 && pc < 0x6000)
		region = gng.bank < 5 ? banked[gng.bank][pc - 0x4000] : 0;
	else
	{
		region = fixed[pc];
		if (region && pc < 0x4000 && !ram_code_intact())
			m6809_fault("code in RAM that is not what the translation was made from", pc);
	}
	if (!region)
		m6809_fault("an address the static analysis did not find", pc);
	return region(pc);
}

/* The handler of the interrupt just taken (its state pushed, returning to 'pc' with S back at 's'), run until its
   RTI comes back */
static void run_handler(uint16_t handler, uint16_t pc, uint16_t s)
{
	if (depth + 1 >= DEPTH)
		m6809_fault("interrupts nested too deep", pc);
	depth++;
	return_pc[depth] = pc;
	return_s[depth] = s;
	returned[depth] = 0;
	uint16_t next = handler;
	while (!returned[depth])
		next = dispatch(next);
	depth--;
}

uint16_t m6809_rti(uint16_t pc)
{
	if (depth > 0 && pc == return_pc[depth] && (uint16_t)cpu.s == return_s[depth])
		returned[depth] = 1;
	return pc;
}

void m6809_event(uint16_t pc)
{
	if (cpu.cycles >= m6809_board_at)
		m6809_board_catch_up();
	if (irq_line && !cpu.fi)
	{
		/* HOLD_LINE: cleared as the CPU takes it */
		irq_line = 0;
		const uint16_t s = (uint16_t)cpu.s;
		const uint64_t start = cpu.cycles;
		const uint16_t handler = m6809_irq_entry(pc);
		update_event_at();
		if (host.irq_taken)
			host.irq_taken(pc, start + 15);    /* (MAME acknowledges after the pushes: 15 cycles in) */
		run_handler(handler, pc, s);
	}
	update_event_at();
}

void m6809_sync(void)
{
	/* the line awaited (it comes with the next VBLANK), masked or not; then a cycle */
	if (!irq_line)
	{
		if (cpu.cycles < m6809_board_at)
			cpu.cycles = m6809_board_at;
		m6809_board_catch_up();
	}
	cpu.cycles += 1;
}

void m6809_cwai(uint16_t next)
{
	/* the state is pushed (the lift's); an interrupt the mask lets through awaited */
	while (!(irq_line && !cpu.fi))
	{
		if (cpu.fi)
			m6809_fault("CWAI with the IRQ masked", next);
		if (cpu.cycles < m6809_board_at)
			cpu.cycles = m6809_board_at;
		m6809_board_catch_up();
	}
	irq_line = 0;
	cpu.fi = 1;
	if (host.irq_taken)
		host.irq_taken(next, cpu.cycles);
	/* INTERRUPT_VECTOR: a VMA cycle, the vector's two bytes, a VMA cycle */
	cpu.cycles += 2;
	const uint32_t high = mem_read8(0xFFF8u);
	cpu.cycles += 1;
	const uint32_t low = mem_read8(0xFFF9u);
	cpu.cycles += 1;
	update_event_at();
	run_handler((uint16_t)(high << 8 | low), next, (uint16_t)(cpu.s + 12));
}

void m6809_power_on(const m6809_host *h)
{
	memset(&host, 0, sizeof(host));
	if (h)
		host = *h;
	memset(&cpu, 0, sizeof(cpu));
	cpu.fi = 1;
	cpu.ff = 1;
	cpu.cycles = 4;    /* the reset's vector fetch */
	irq_line = 0;
	frame = 0;
	depth = 0;
	faulted = 0;
	m6809_board_at = GNG_VBLANK_CYCLE;
	update_event_at();
	memset(fixed, 0, sizeof(fixed));
	memset(banked, 0, sizeof(banked));
	for (unsigned i = 0; i < m6809_entry_count; i++)
	{
		const m6809_entry *entry = &m6809_entries[i];
		if (entry->bank < 0)
			fixed[entry->address] = entry->region;
		else
			banked[entry->bank][entry->address - 0x4000] = entry->region;
	}
}

int m6809_run(uint64_t frames)
{
	frames_to_run = frames;
	if (setjmp(stop))
		return faulted ? -1 : 0;
	uint16_t pc = (uint16_t)(gng_read(0xFFFE) << 8 | gng_read(0xFFFF));
	for (;;)
		pc = dispatch(pc);
}
