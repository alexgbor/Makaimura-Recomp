"""Writes walk_right.txt: a coin, start, then the player walking right, jumping and throwing for a while.
The script's lines are "FRAME PORT VALUE" (the byte as the board reads it, active low), for gref --inputs and
tools/mame/trace.lua alike."""
import os

lines = ["# coin, start, then walk right with jumps and throws (tests/controls/make_walk.py)"]
lines += ["1300 system bf", "1306 system ff", "1400 system fe", "1406 system ff"]
for frame in range(1600, 7000, 4):
    p1 = 0xFE                      # right held
    phase = (frame - 1600) // 4 % 40
    if phase in (0, 1, 2):
        p1 &= ~0x20                # button 2: jump
    if phase in (10, 11, 20, 21, 30, 31):
        p1 &= ~0x10                # button 1: throw
    lines.append(f"{frame} p1 {p1:02x}")
with open(os.path.join(os.path.dirname(os.path.abspath(__file__)), "walk_right.txt"), "w", newline="\n") as out:
    out.write("\n".join(lines) + "\n")
