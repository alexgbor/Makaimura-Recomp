/**
 *    Name:        state.h
 *    Description: save states: the whole board (the 6809's registers, its memories and latches, the video's raster,
 *                 the sound board: the Z80, its RAM, the latch's history, the two YM2203s) at an instruction
 *                 boundary of the 6809, to go on from later exactly as the board went on from there
 *    Version:     0.1.0
 *    Created:     20-11-2026
 *
 *    license:BSD-3-Clause
 *    A state is taken where the program is between two instructions (m6809_at_boundary: the hook runs before the
 *    boundary's VBLANK and IRQ are looked at) and the Z80 between two of its runs (it runs only as the 6809's
 *    writes and VBLANKs make it). Loading it puts every part back and the program on at the PC it had
 *    (m6809_resume), the boundary's event first: the run is the same as the one that saved it, frame for frame and
 *    sample for sample (tools/check_states.py checks it against an uninterrupted run).
 *
 *    The file is this build's memory layout (the same executable, the same ROM set): a header with the format's
 *    version, the parts' sizes, the accuracy mode and a hash of the program's ROMs, so that a state from anything
 *    else is refused. It holds what the ROM put in RAM, so it is the user's own, never part of the repository.
 */
#ifndef GNG_STATE_H
#define GNG_STATE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The bytes a state takes */
size_t gng_state_size(void);

/* The board's state now, the 6809 at 'pc' (an instruction boundary: from the m6809_at_boundary hook), into 'out'
   (gng_state_size() bytes) */
void gng_state_save(uint8_t *out, uint16_t pc);

/* NULL if 'in' is a state this build can load into the board as it is (its ROMs, its mode); else why not */
const char *gng_state_check(const uint8_t *in, size_t size);

/* The frame (the VBLANKs counted from power-on) a state was saved in */
uint64_t gng_state_frame(const uint8_t *in);

/* Puts the state back (checked first) and the program on from it: from the host's vblank during a run, the run
   leaves the instruction it was in (m6809_resume) and this does not return; before a run, the run starts there */
void gng_state_load(const uint8_t *in);

#ifdef __cplusplus
}
#endif

#endif
