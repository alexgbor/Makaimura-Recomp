# Makaimura Recomp

A native PC port of Capcom's 1985 arcade game **Makaimura** (*Ghosts'n Goblins* in the West), Japan
revision G. It is being made by translating the arcade board's MC6809 program into C ahead of time and running
it on a model of the board, aiming at exactly what the arcade does, frame for frame. It uses the approach and
tools of [DonPachi Recomp](https://github.com/CapsLock4284/DonPachi-Recomp) and
[arcade-decomp](https://github.com/cheviman/arcade-decomp).

**Status: the main program runs translated, with no window yet.** The 6809 program is translated to C an
instruction at a time (`tools/m6809/translate.py`). It runs on a model of the board (`runtime/`), and it is the
same as MAME 0.289 at every frame checked: the CPU's bus at every VBLANK and every interrupt, and the picture,
pixel for pixel. The sound board exists in the reference (MAME's Z80 and ymfm's YM2203s, the same as MAME at every YM2203 write); the
game's own sound board and a window are next. The plan is in [TODO.md](TODO.md), the
board is described in [docs/hardware.md](docs/hardware.md), and [docs/accuracy.md](docs/accuracy.md) explains
the two accuracy modes: MAME's, and the real board's, which is the goal.

**This repository contains no game data.** You need your own copy of MAME's ROM sets: `makaimurg.zip` and its
parent `gng.zip`, in `rom/`.

```sh
git clone --recursive <this repository>
python tools/makaimura_rom.py --extract build/makaimurg.regions   # the set's files found in rom/, extracted
python tools/disassemble.py --listing     # the static analysis, and build/makaimurg.lst
python tools/m6809/translate.py           # the program as C: build/generated/program (never committed)
cmake -S . -B build/msvc && cmake --build build/msvc --config Release
python tools/check_translation.py         # the translation against the reference (gref), frame by frame
python tools/mame_trace.py --frames 7001 --inputs tests/controls/walk_right.txt --pictures 1 --against gng_headless
```

`tools/mame_trace.py` needs MAME 0.289 (`MAME` environment variable, or `D:/repositories/mame0289/mame.exe`).

## License

BSD 3-Clause (`LICENSE`). The game is not included and not covered by it.

## Credits

- **Makaimura** is © 1985 Capcom. This is an unofficial fan project, not connected with or endorsed by Capcom.
- The hardware model follows [MAME](https://github.com/mamedev/mame)'s `capcom/gng.cpp` (Pierpaolo Prazzoli and
  the MAME team, BSD-3-Clause), and Jose Tejada's (jotego) research on the board for jt_gng.
- The verification tools run MAME 0.289's MC6809 and Z80 cores and Aaron Giles's
  [ymfm](https://github.com/aaronsgiles/ymfm) (BSD-3-Clause), in `tools/reference/mame`: never in the game.
- The decompiler core is [arcade-decomp](https://github.com/cheviman/arcade-decomp) (in `tools/core`), from
  [CapsLock4284's fork](https://github.com/CapsLock4284/arcade-decomp/tree/timing-model).
