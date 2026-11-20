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
/* save states: the hook the next instruction boundary calls; where a resumed run goes on */
static void (*boundary_hook)(uint16_t pc);
static jmp_buf resume_jump;
static int running, resume_pending;
static uint16_t resume_pc;

/* the dispatch: every instruction's region, by address (and the bank in the window) */
static m6809_region fixed[0x10000];
static m6809_region banked[5][0x2000];

static void update_event_at(void)
{
	m6809_event_at = (irq_line && !cpu.fi) || boundary_hook ? 0 : m6809_board_at;
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

/* RTI: execution goes on at what it pulled (the trampoline's), whatever task that is */
uint16_t m6809_rti(uint16_t pc)
{
	return pc;
}

uint16_t m6809_event(uint16_t pc)
{
	if (boundary_hook)
	{
		void (*hook)(uint16_t) = boundary_hook;
		boundary_hook = NULL;
		update_event_at();
		hook(pc);
	}
	if (cpu.cycles >= m6809_board_at)
		m6809_board_catch_up();
	if (irq_line && !cpu.fi)
	{
		/* HOLD_LINE: cleared as the CPU takes it */
		irq_line = 0;
		const uint64_t start = cpu.cycles;
		const uint16_t handler = m6809_irq_entry(pc);
		update_event_at();
		if (host.irq_taken)
			host.irq_taken(pc, start + 15);    /* (MAME acknowledges after the pushes: 15 cycles in) */
		return handler;
	}
	update_event_at();
	return pc;
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

uint16_t m6809_cwai(uint16_t next)
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
	(void)next;
	return (uint16_t)(high << 8 | low);
}

static uint64_t clock_now(void)
{
	return cpu.cycles;
}

/* board mode: the sprite DMA's halt (gng_dma_halt), from the access's time on */
static void on_dma(gng_board *board)
{
	(void)board;
	cpu.cycles += gng_dma_halt(cpu.cycles);
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
	faulted = 0;
	boundary_hook = NULL;
	resume_pending = 0;
	m6809_board_at = GNG_VBLANK_CYCLE;
	update_event_at();
	gng.on_dma = on_dma;
	gng.clock = clock_now;
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

void m6809_request_stop(void)
{
	frames_to_run = frame + 1;
}

void m6809_state_get(m6809_state *state)
{
	memset(state, 0, sizeof(*state));
	state->cpu = cpu;
	state->irq_line = (uint32_t)irq_line;
	state->frame = frame;
	state->board_at = m6809_board_at;
}

void m6809_state_set(const m6809_state *state)
{
	cpu = state->cpu;
	irq_line = (int)state->irq_line;
	frame = state->frame;
	m6809_board_at = state->board_at;
	boundary_hook = NULL;
	update_event_at();
}

void m6809_at_boundary(void (*hook)(uint16_t pc))
{
	boundary_hook = hook;
	update_event_at();
}

void m6809_resume(uint16_t pc)
{
	resume_pc = pc;
	resume_pending = 1;
	if (running)
		longjmp(resume_jump, 1);
}

int m6809_run(uint64_t frames)
{
	frames_to_run = frames;
	if (setjmp(stop))
	{
		running = 0;
		return faulted ? -1 : 0;
	}
	running = 1;
	uint16_t pc;
	if (setjmp(resume_jump) == 0 && !resume_pending)
		pc = (uint16_t)(gng_read(0xFFFE) << 8 | gng_read(0xFFFF));
	else
	{
		/* a state loaded: its boundary's event, as the saved run made it after the hook */
		resume_pending = 0;
		pc = m6809_event(resume_pc);
	}
	for (;;)
		pc = dispatch(pc);
}
