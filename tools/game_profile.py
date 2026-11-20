"""
Name:        game_profile.py
Description: what the shared core (tools/core: arcade-decomp) needs to know about Makaimura where its modules ask
             for the game's profile: the repository, the ROM set's program areas, the names its generated files use
Version:     0.1.0
Created:     20-11-2026

license:BSD-3-Clause
The core's own frontends (V60, 68000, Z80) read much more from a profile; the 6809's is this repository's
(tools/m6809/), which sets the core's modules itself (tools/m6809/decompile.py, emit.py). This is only what the
core's modules read as they are imported: ROOT, rom.PROGRAM_AREAS and rom.canonical_address (core disassemble.py),
GAME, CPU and GUARD for the files' headers.
"""
from pathlib import Path
from types import SimpleNamespace

ROOT = Path(__file__).resolve().parent.parent
GAME = "Makaimura"
ROM_SET_TITLE = "Makaimura (Japan revision G, makaimurg)"
CPU = "MC6809"
GUARD = "MAKAIMURA"
# the 6809's program: the fixed ROM, then the banked window
rom = SimpleNamespace(PROGRAM_AREAS=[(0x6000, 0x10000), (0x4000, 0x6000)], canonical_address=lambda address: address)
