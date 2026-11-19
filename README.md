# Makaimura Recomp

A native PC port of Capcom's 1985 arcade game **Makaimura** (*Ghosts'n Goblins* in the West), Japan
revision G. It is being made by translating the arcade board's MC6809 program into C ahead of time and running
it on a model of the board, aiming at exactly what the arcade does, frame for frame. It uses the approach and
tools of [DonPachi Recomp](https://github.com/CapsLock4284/DonPachi-Recomp) and
[arcade-decomp](https://github.com/cheviman/arcade-decomp).

**Status: started.** Nothing plays yet. The 6809 program is being read: there is a disassembler and a static
analysis. The plan is in [TODO.md](TODO.md) and the board is described in [docs/hardware.md](docs/hardware.md).

**This repository contains no game data.** You need your own copy of MAME's ROM sets: `makaimurg.zip` and its
parent `gng.zip`, in `rom/`.

```sh
git clone --recursive <this repository>
python tools/makaimura_rom.py        # which of the set's files are found in rom/
python tools/disassemble.py --listing # the static analysis, and build/makaimurg.lst
```

## Credits

- **Makaimura** is © 1985 Capcom. This is an unofficial fan project, not connected with or endorsed by Capcom.
- The hardware model follows [MAME](https://github.com/mamedev/mame)'s `capcom/gng.cpp` (Pierpaolo Prazzoli and
  the MAME team, BSD-3-Clause), and Jose Tejada's (jotego) research on the board for jt_gng.
- The decompiler core is [arcade-decomp](https://github.com/cheviman/arcade-decomp) (in `tools/core`), from
  [CapsLock4284's fork](https://github.com/CapsLock4284/arcade-decomp/tree/timing-model).
