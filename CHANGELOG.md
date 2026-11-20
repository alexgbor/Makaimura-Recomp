# Changelog

What changes from one version to the next, for players. The version is set in `CMakeLists.txt` (the menu shows it);
`tools/make_release.ps1` takes it from there and wants its section here.

## Unreleased

Nothing yet.

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
