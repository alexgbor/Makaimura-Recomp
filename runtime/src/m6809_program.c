/**
 *    Name:        m6809_program.c
 *    Description: the board's MC6809 as the decompiled program (src/, m6809/program.h) runs on it: the board's
 *                 time, the VBLANK, the IRQ taken as a call of its handler, SYNC, the DMA's halt
 *    Version:     0.1.0
 *    Created:     20-11-2026
 *
 *    license:BSD-3-Clause
 *    The same board as the translation's (runtime/src/m6809.c, m6809/cpu.h), after MAME's src/devices/cpu/m6809
 *    (BSD-3-Clause, Nathan Woods and others): the reset (the vector fetched in four cycles, I and F set), the IRQ
 *    held until taken (HOLD_LINE), taken at an instruction's end with I clear, its entry 19 cycles (the state
 *    pushed, I set, the vector read: lift.IrqEntry) and acknowledged 15 cycles in; SYNC (the line awaited, masked or
 *    not, then a cycle). The C has no registers to push: the handler (program_irq) is called where the IRQ is
 *    taken and comes back there, as the program's always does (TODO.md, M6), and its RTI's end gives back the mask
 *    the code had (clear: the IRQ was taken). The stack's bytes the code pushes are in the work RAM as the board's
 *    (m6809_stack_byte, S in cpu.s); what the C cannot have is the state the IRQ's entry pushes (the registers are
 *    the C's variables: S moves past its 12 bytes, they are not written) and the stack pointer the round robin's
 *    copies keep at <$E2.
 */
#include "m6809/program.h"

#include <setjmp.h>
#include <stdio.h>
#include <string.h>

m6809_cpu cpu;
uint16_t cpu_pc;
const char m6809_state_magic[8] = { 'M', 'K', 'R', 'S', 'T', 'A', 'T', 'C' };
uint64_t m6809_event_at;
uint64_t m6809_board_at;
uint64_t m6809_start;

#define IRQ_ENTRY_CYCLES 19u    /* tools/m6809/lift.py IrqEntry: the pushes, I set, the vector */
#define IRQ_ACKNOWLEDGE  15u    /* MAME acknowledges after the pushes */

static m6809_host host;
static int irq_line;
static uint64_t frame;
static uint64_t frames_to_run;
static jmp_buf stop;
static int faulted;
/* save states: the hook the next idle place calls; where a resumed run goes on */
static void (*boundary_hook)(uint16_t pc);
static jmp_buf resume_jump;
static int running, resume_pending;

static void update_event_at(void)
{
	m6809_event_at = irq_line && !cpu.fi ? 0 : m6809_board_at;
}

void m6809_mask_changed(void)
{
	update_event_at();
}

void m6809_mask(uint32_t masked)
{
	cpu.fi = masked & 1u;
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

void m6809_boundary(void)
{
	for (;;)
	{
		if (cpu.cycles >= m6809_board_at)
			m6809_board_catch_up();
		if (!(irq_line && !cpu.fi))
			break;
		/* HOLD_LINE: cleared as the CPU takes it */
		irq_line = 0;
		const uint64_t start = cpu.cycles;
		cpu.fi = 1;
		m6809_start = cpu.cycles = start + IRQ_ENTRY_CYCLES;
		update_event_at();
		if (host.irq_taken)
			host.irq_taken(0, start + IRQ_ACKNOWLEDGE);
		/* the state the entry pushes, 12 bytes below S (the C has the registers in its variables: their bytes are
		   not written), then the handler's own pushes below them, as on the board; its RTI pulls them */
		cpu.s = (cpu.s - 12u) & 0xFFFFu;
		program_irq();
		cpu.s = (cpu.s + 12u) & 0xFFFFu;
		/* its RTI: the code's own mask back (clear), and the next IRQ, if one is due already, taken at once */
		cpu.fi = 0;
		cpu.cycles = m6809_start;
		update_event_at();
	}
	update_event_at();
}

void m6809_sync(void)
{
	/* the line awaited (it comes with the next VBLANK), masked or not; then a cycle */
	cpu.cycles = m6809_start;
	if (!irq_line)
	{
		if (cpu.cycles < m6809_board_at)
			cpu.cycles = m6809_board_at;
		m6809_board_catch_up();
	}
	cpu.cycles += 1;
	m6809_start = cpu.cycles;
}

uint16_t m6809_cwai(uint16_t next)
{
	m6809_fault("CWAI (the program has none)", next);
	return next;
}

static uint64_t clock_now(void)
{
	return cpu.cycles;
}

/* board mode: the sprite DMA's halt (gng_dma_halt), from the access's time on: the instruction's later cycles too */
static void on_dma(gng_board *board)
{
	(void)board;
	const uint32_t halt = gng_dma_halt(cpu.cycles);
	cpu.cycles += halt;
	m6809_start += halt;
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
	m6809_start = cpu.cycles;
	irq_line = 0;
	frame = 0;
	faulted = 0;
	boundary_hook = NULL;
	resume_pending = 0;
	m6809_board_at = GNG_VBLANK_CYCLE;
	update_event_at();
	gng.on_dma = on_dma;
	gng.clock = clock_now;
}

void m6809_request_stop(void)
{
	frames_to_run = frame + 1;
}

/* Save states: the C has no stack a state could hold, so a state is taken where the program's own is nothing but
   the main loop waiting for work (m6809_idle, symbols.txt's idle: 617F, which the loop passes every few dozen cycles
   while it waits), with what the main loop keeps (U); it goes on by calling the main loop there (program_resume).
   A state asked for (m6809_at_boundary) is taken at the next such place: the boundary there, after its VBLANK and IRQ
   (the translation's are before them: either way the run goes on as the uninterrupted one) */
void m6809_idle(uint32_t u)
{
	cpu.u = u & 0xFFFFu;
	if (boundary_hook)
	{
		void (*hook)(uint16_t) = boundary_hook;
		boundary_hook = NULL;
		hook(program_idle_address);
	}
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
}

void m6809_resume(uint16_t pc)
{
	if (pc != program_idle_address)
		m6809_fault("a state the readable C cannot go on from (not where the main loop waits)", pc);
	/* S there is 0300, the stack empty (gref --registers-at 617F over long_play: always): a state saved before the
	   C kept S (cpu.s) goes on as well */
	cpu.s = 0x0300u;
	resume_pending = 1;
	if (running)
		longjmp(resume_jump, 1);
}

/* Runs the program from its reset (after m6809_power_on), or from a state set (m6809_resume), until 'frames' VBLANKs
   went by, on the host's own stack: the host does its frame's work in its vblank (the window paces itself there); a
   run is left by a longjmp, so a second one starts from the reset again (a reset of the window: m6809_power_on
   first) */
int m6809_run(uint64_t frames)
{
	frames_to_run = frames;
	if (setjmp(stop))
	{
		running = 0;
		return faulted ? -1 : 0;
	}
	running = 1;
	if (setjmp(resume_jump) == 0 && !resume_pending)
	{
		m6809_start = cpu.cycles;
		program_reset();
		m6809_fault("the reset's code returned", 0);
	}
	resume_pending = 0;
	m6809_start = cpu.cycles;
	program_resume((uint16_t)cpu.u);
	m6809_fault("the main loop returned", program_idle_address);
	return -1;
}
