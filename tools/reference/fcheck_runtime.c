/**
 *    Name:        fcheck_runtime.c
 *    Description: what the readable C (src/, m6809/program.h) runs on when tools/reference/fcheck.cpp calls one of
 *                 its functions alone: the board's time from 0, no VBLANK and no IRQ, the run left when it went past
 *                 a number of cycles, at a SYNC or at a fault
 *    Version:     0.1.0
 *    Created:     23-11-2026
 *
 *    license:BSD-3-Clause
 *    The game's runtime is runtime/src/m6809_program.c: this one has its time (m6809_block, m6809_at...: the same
 *    inline functions of m6809/program.h) and none of its board's events, which the check leaves out (the function
 *    is compared with MAME's core run with the IRQ's line clear). A function that runs away in the C (a recursion
 *    with no end on garbage registers) is caught by the system's exception (Windows' structured exceptions).
 */
#include <setjmp.h>
#include <stdio.h>
#include <string.h>

#include "fcheck.h"
#include "m6809/program.h"

#ifdef _WIN32
#include <malloc.h>
#include <windows.h>
#endif

m6809_cpu cpu;
uint16_t cpu_pc;
uint64_t m6809_event_at;
uint64_t m6809_board_at;
uint64_t m6809_start;

static jmp_buf stop;
static fc_result *running;

/* the work RAM as the C reaches it (m6809/program.h, M6809_RAM_GUARD): past 1DFF, a place of its own, not the
   board's (the board's memories after the work RAM are other arrays of gng and this program's own data) */
static uint8_t elsewhere[0x10100];
uint8_t *m6809_ram_guard(uint32_t address)
{
	address &= 0xFFFFu;
	if (address < 0x1E00u)
		return &gng.ram[address];
	running->outside = 1;
	return &elsewhere[address];
}

static void leave(enum fc_end end)
{
	running->end = end;
	longjmp(stop, 1);
}

void m6809_boundary(void)
{
	if (cpu.cycles >= m6809_event_at)
		leave(FC_CAPPED);
}

void m6809_board_catch_up(void)
{
	/* (no VBLANK: m6809_board_at is never reached) */
}

void m6809_mask_changed(void)
{
}

void m6809_mask(uint32_t masked)
{
	cpu.fi = masked & 1u;
}

void m6809_sync(void)
{
	/* the line awaited, which never comes here: the run ends where the SYNC starts */
	cpu.cycles = m6809_start;
	leave(FC_SYNC);
}

uint16_t m6809_cwai(uint16_t next)
{
	m6809_fault("CWAI", next);
	return next;
}

void m6809_fault(const char *what, uint16_t pc)
{
	snprintf(running->fault, sizeof(running->fault), "%s at %04X", what, pc);
	leave(FC_FAULT);
}

void m6809_idle(uint32_t u)
{
	cpu.u = u & 0xFFFFu;
}

static void call(unsigned index, fc_registers *registers)
{
#ifdef _WIN32
	__try
	{
		fc_call(index, registers);
	}
	__except (EXCEPTION_EXECUTE_HANDLER)
	{
		if (GetExceptionCode() == EXCEPTION_STACK_OVERFLOW)
			_resetstkoflw();
		snprintf(running->fault, sizeof(running->fault), "exception %08lX", (unsigned long)GetExceptionCode());
		running->end = FC_CRASHED;
	}
#else
	fc_call(index, registers);
#endif
}

uint64_t fc_now(void)
{
	return cpu.cycles;
}

void fc_run(unsigned index, fc_registers *registers, uint64_t cap, fc_result *result)
{
	memset(result, 0, sizeof(*result));
	running = result;
	(void)elsewhere;
	memset(&cpu, 0, sizeof(cpu));
	cpu.a = registers->a;
	cpu.b = registers->b;
	cpu.dp = registers->dp;
	cpu.x = registers->x;
	cpu.y = registers->y;
	cpu.u = registers->u;
	cpu.s = registers->s;
	cpu.fc = registers->cc & 1u;
	cpu.fv = registers->cc >> 1 & 1u;
	cpu.fz = registers->cc >> 2 & 1u;
	cpu.fn = registers->cc >> 3 & 1u;
	cpu.fi = registers->cc >> 4 & 1u;
	cpu.fh = registers->cc >> 5 & 1u;
	cpu.ff = registers->cc >> 6 & 1u;
	cpu.fe = registers->cc >> 7 & 1u;
	m6809_start = cpu.cycles = 0;
	m6809_event_at = cap;
	m6809_board_at = UINT64_MAX;
	result->end = FC_RETURNED;
	if (setjmp(stop) == 0)
		call(index, registers);
	result->cycles = result->end == FC_CAPPED || result->end == FC_RETURNED ? m6809_start : cpu.cycles;
	registers->s = cpu.s;
	registers->cc = (registers->cc & ~0x10u) | (cpu.fi & 1u) << 4;
}
