MAKAIMURA RECOMP
================

A native PC port of Capcom's 1985 arcade game Makaimura (Ghosts'n Goblins in the West), Japan revision G. The
arcade board's own program, translated into C ahead of time and run on a model of the board: the same game as the
arcade, frame for frame, its slowdown, picture and sound included. Not an emulator, not a remake.

This package contains no game data. You need your own copy of the arcade ROM set, as MAME has it:
makaimurg.zip and its parent set gng.zip.

https://github.com/alexgbor/Makaimura-Recomp


FIRST RUN
---------

Windows: unzip the package somewhere you can write to (not inside Program Files) and run makaimura.exe. Windows
may say "Windows protected your PC" because the program is not signed: More info, then Run anyway.

Linux (x86-64, glibc 2.35 or newer: Ubuntu 22.04, Debian 12, Fedora 36, SteamOS 3 and later): unpack the .tar.gz
somewhere you can write to and run ./makaimura from its folder. It brings its own SDL2 (libSDL2-2.0.so.0 beside
it) and works on X11 or Wayland, with PulseAudio, PipeWire or ALSA. Recording videos needs ffmpeg (see below).

The first time, a window asks for the ROM set: drag makaimurg.zip and gng.zip onto it (both at once, or one after
the other). The game copies them into a "rom" folder beside it and finds them there from then on. Merged and
split MAME sets both work, and a folder of the set's loose files too; the files are known by their contents, not
their names.


CONTROLS
--------

                    Keyboard                        Gamepad
  Move              arrows                          left stick or D-pad
  Attack (throw)    Z or left Ctrl                  B or X
  Jump              X or left Alt                   A
  Coin              5 (6: the second coin slot)     Back
  Start             1 (one player), 2 (two)         Start
  Player 2          R F D G, A (attack), S (jump)   the second gamepad
                    (a cocktail cabinet; the continue's buttons)

  Menu              Esc or F1                       right stick pressed, or guide
  Pause             P or Pause
  Save state        F5          Load state   F7     Next slot   F6
  Reset the board   F3
  Full screen       F11 or Alt+Enter
  Record a video    F12

The joystick is 4-way, like the board's. Every control can be changed in the menu (CONTROLS) or in makaimura.ini.


THE MENU
--------

Esc pauses the game and opens the menu:

  SAVE STATE, LOAD STATE   the whole board in one of 8 slots (left, right: the slot)
  PRACTICE                 a game from the start of any area (1-7), in either loop, with the weapon and the
                           lives you choose
  REPLAYS                  the games you kept, played again frame for frame (Delete, then Enter: delete one)
  SPEEDRUN                 the timer in the left panel (on or off) and where a run ends: Astaroth beaten in the
                           1st loop, in the 2nd (the ending), or never
  GAME OPTIONS             the board's DIP switches: lives, difficulty, bonus life, coinage, free play...
  PICTURE AND SOUND        4:3 or square pixels, scaling, sharp or smooth, a CRT look (SCANLINES, ARCADE, PVM),
                           full screen, the side panels, what F12 records, volume
  CONTROLS                 every key and button
  ACCURACY                 "MAME 1:1" (the board as MAME 0.289 has it) or "BOARD" (what the real board also does:
                           its sprite copy halting the CPU, so a little more slowdown, and the palette's flashes)

On a wide window or screen, side panels beside the picture show the score, lives, weapon, area, loop, rank, the
controls and the slowdown; the right one explains the menu's options while it is open.

The speedrun timer (left panel) starts with the START that begins a game and stops where the SPEEDRUN page says
(green, CLEAR), or at the game over (OVER). It counts the board's frames, 59.637 a second: the slowdown is in it as
on the arcade, a pause is not, and a replay gives the same time. A reset or a loaded state ends the run.

At each game over the game asks whether to keep its replay (KEEP REPLAY, LET IT GO).


UPDATES
-------

As it starts, the game looks up its newest version on GitHub (only the request is sent). If there is one, the menu
offers it first (UPDATE TO V...): it asks, downloads it, checks it, puts it in place and starts again, keeping your
settings, scores, states, replays and ROM set. GAME OPTIONS > CHECK FOR UPDATES turns the look-up off (or
check_updates in makaimura.ini). The version you have is in the window's title and on the menu.


FILES THE GAME KEEPS (beside the program)
-----------------------------------------

  makaimura.ini   the settings, each with what it does (deleting it brings the defaults back)
  makaimura.hi    the high scores (the game's own ranking, kept between sessions)
  rom/            the ROM set, as you gave it
  states/         the save states and the practice's area starts
  replays/        the replays you kept
  *.mp4, *.avi    the videos recorded with F12


VIDEOS (F12)
------------

F12 starts and stops a recording, named by the date and time, beside the program. By default it is a video ready
to share or upload to YouTube: an MP4 (H.264 and AAC) of what the window shows, the CRT look included, with the side
panels when they are on (16:9, 2560 x 1440; the picture alone at 4:3 when they are off), at the board's own 59.637
frames a second, in the board's time (a pause or the menu leave nothing in it). About 3 MB a second. 1440 lines
because YouTube names a video's quality by its height and gives a 1440p video many more bits: the pixels stay
sharp even watched at 1080p.

PICTURE AND SOUND > RECORDING chooses: VIDEO 1440P, VIDEO 1080P (1920 x 1080: smaller files) or BOARD AVI (the
board's own 256 x 224 picture and its sound, nothing lost or added: uncompressed, about 10 MB a second).

Windows encodes the video with its own encoders (Media Foundation; the graphics card's when it has one). Linux
uses ffmpeg: install it with your system's packages (apt install ffmpeg, dnf install ffmpeg, pacman -S ffmpeg...).
If the game cannot encode, it says so and records nothing.


CREDITS
-------

Makaimura is (c) 1985 Capcom. This is an unofficial fan project, not connected with or endorsed by Capcom.

The board's model follows MAME's capcom/gng.cpp (Pierpaolo Prazzoli and the MAME team) and Jose Tejada's
(jotego) research on the real board; the YM2203 sound chips follow Aaron Giles's ymfm. SDL2 makes the window, the
sound and the controls. Their licenses are in the licenses folder.
