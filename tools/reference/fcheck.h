/**
 *    Name:        fcheck.h
 *    Description: the readable C's functions one at a time (tools/reference/fcheck.cpp): the registers they take
 *                 and give, the table of the functions (the C tools/m6809/check_functions.py writes, calls.c) and
 *                 the runtime they run on there (fcheck_runtime.c)
 *    Version:     0.1.0
 *    Created:     23-11-2026
 *
 *    license:BSD-3-Clause
 */
#ifndef FCHECK_H
#define FCHECK_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* the 6809's registers as a function takes them and leaves them (cc: E F H I N Z V C, bit 7 to bit 0) */
typedef struct fc_registers
{
	uint32_t a, b, dp, x, y, u, s, cc;
} fc_registers;

/* what a function hands back, to compare (fc_function.compared) */
enum
{
	FC_A = 1 << 0, FC_B = 1 << 1, FC_DP = 1 << 2, FC_X = 1 << 3, FC_Y = 1 << 4, FC_U = 1 << 5, FC_S = 1 << 6,
	FC_CC_SHIFT = 8    /* a flag's bit of CC, shifted: FC_CC_SHIFT + 0 C, + 1 V, + 2 Z, + 3 N, + 5 H, + 6 F, + 7 E */
};

/* what every caller agrees a register holds as the function starts, which the C may take as given (src/functions.txt:
   pointers.Program's entry_kinds): 'g' that value (an address: the C reads what it points at as variables), 'o' a
   pointer into an object's record, 'value' bytes into it */
typedef struct fc_entry_kind
{
	char reg;        /* 'x', 'y', 'u', 'd' */
	char kind;
	uint16_t value;
} fc_entry_kind;

typedef struct fc_function
{
	uint32_t key;        /* the decompiler's number: the address, (bank + 1) << 16 | address in 4000-5FFF */
	const char *name;
	uint32_t compared;   /* FC_* of the registers it hands back */
	const fc_entry_kind *kinds;
	unsigned kind_count;
} fc_function;

/* calls.c: the functions, and one called with the registers (what it takes from them, what it gives into them) */
extern const fc_function fc_functions[];
extern const unsigned fc_function_count;
void fc_call(unsigned index, fc_registers *registers);

/* fcheck_runtime.c: the function run from the board as it is (gng) and S, CC's I as given, with no IRQ, until it
   returns or 'cap' cycles went by (at an instruction's end) */
enum fc_end { FC_RETURNED, FC_CAPPED, FC_SYNC, FC_FAULT, FC_CRASHED,
	FC_OFF_CODE    /* MAME's core only: at an instruction the static analysis never found */
};
typedef struct fc_result
{
	enum fc_end end;
	uint64_t cycles;          /* from the function's start to its last instruction's end */
	int outside;              /* the C reached the work RAM past 1DFF (M6809_RAM_GUARD): a state the game never has */
	char fault[96];
} fc_result;
void fc_run(unsigned index, fc_registers *registers, uint64_t cap, fc_result *result);
/* the C's time in the run: the end of the bus cycle being made (cpu.cycles) */
uint64_t fc_now(void);

#ifdef __cplusplus
}
#endif

#endif
