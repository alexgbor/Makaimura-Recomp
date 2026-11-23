# Changelog

What changes from one version to the next, for players. The version is set in `CMakeLists.txt` (the menu shows it);
`tools/make_release.ps1` takes it from there and wants its section here.

## Unreleased

Nothing yet.

## v0.2.2 (2026-11-23)

- **Fixed: stage 3 after its midway point.** Starting there again (after losing a life past the midway point), a
  small block of the background was drawn blank for about a second where the arcade draws some odd tiles: the
  original program reads them from a part of its memory the C version did not fill the same way. Now it does, and
  that scene is the same as the arcade's again. Nothing else changes; save states, scores and settings are kept.

## v0.2.1 (2026-11-22)

- **PRACTICE, set up:** choose the area (now 1-7: the last room too), the loop, Arthur's weapon (lance, dagger,
  torch, axe, cross) and his lives, then START. The first time an area is chosen it is prepared unseen, with how
  far it is on the screen (no more the whole opening flashing past at full speed); from then on it starts at once.
  The areas' starts kept by earlier versions (`states/practice-N-...`) are no longer used and can be deleted.
- **The cross is called the cross** (in the side panel too), as in this Japanese game: the western releases made it
  a shield.

## v0.2.0 (2026-11-22)

- **The game is now its program decompiled into readable C:** the same 6809 program, now as C functions with
  their own variables rather than an instruction at a time. It plays exactly as before: checked against the
  reference at every frame of every test (both modes, both loops, every stage, the ending, the test mode),
  picture for picture and sample for sample.
- **Save states from v0.1.x can't be loaded** (the game says "A STATE OF THE TRANSLATED PROGRAM"): save again.
  The practice areas' starts are made again by themselves the first time each one is chosen. High scores and the
  settings are kept.

## v0.1.1 (2026-11-20)

- **A CRT look:** PICTURE AND SOUND > CRT. SCANLINES draws the picture as a tube's beam does, its lines wider where
  the picture is bright; ARCADE and PVM add a phosphor mask (a slot mask, an aperture grille). Best full screen or
  in a large window.

## v0.1.0 (2026-11-20)

The first release.

- **The arcade game, translated:** Makaimura (Japan revision G, MAME's `makaimurg`), its 6809 program and its Z80 sound
  program translated into C, on a model of the board: the same as MAME 0.289 at every frame checked (both loops,
  every stage, the ending, the test mode), picture for picture and sample for sample.
- **Two accuracy modes:** "MAME 1:1", or "BOARD": what the real board also does (the sprite copy halting the CPU,
  the palette's flashes in busy scenes).
- **The ROM set's window:** on the first run, drop `makaimurg.zip` and `gng.zip` onto it; they are kept in `rom\`.
- **Save states** (F5, F7, F6 the slot; 8 slots), **practice** (any area of the first loop), **replays** (each game
  kept if you want it at its game over, played again from the menu) and **high scores** kept between sessions.
- **Side panels** on a wide window: the player, the stage, the rank, the controls, the slowdown, the menu's help.
- **The menu:** the board's DIP switches, the picture (4:3 or square pixels, scaling, sharp or smooth), full screen,
  the volume, every control (keyboard and two gamepads), reset.
- **Recordings:** F12 records the board's picture and sound into an AVI.
- **Updates:** the newest release looked up as the game starts and offered on the menu (UPDATE TO V...): downloaded,
  checked and installed only if you choose it, your files kept. The version is in the window's title and on the menu.
