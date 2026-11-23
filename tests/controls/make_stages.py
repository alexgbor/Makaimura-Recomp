"""Writes stage1.txt to stage6.txt: a coin, start, the stage set right after the start (the work RAM's <$72, the
stage: a test's cheat), the lives kept (<$60, written back every 300 frames), then the player walking right,
jumping and throwing (as make_walk.py), a while to the left now and then. 12000 frames each: their busy scenes slow
the game down in board mode (docs/accuracy.md #1).

ending.txt: loop2_stage6's, its stage cleared at frame 2100 (while it is played) as the program clears one (E799: <$08 = 2), so the
ending plays (7D3F, 7E18, 71D7) and the game goes back to the first loop.

midway1.txt, midway3.txt, midway4.txt: stages 1, 3 and 4 from their midway point (<$81 = 1, as ECB0 sets it
when the player passes it: the start point from 6CEA's second table, 6D0A), where the scripts from the start do not
reach: the town's windows, the bridge's flames (and the tiles stage 3 draws from the 6809's stack).

loop2_stage0.txt and loop2_stage6.txt: the second loop (<$7F = 1, as 7298 leaves it after the first loop's last
stage; <$71 is the stage's own state: 0 its start, 1 playing, 7AA0), stage 6 with the cross (<$73 = 3, the weapon
the last stage asks for: 7129).

The lines are "FRAME PORT VALUE" (the byte as the board reads it, active low) and "FRAME mADDR VALUE" (the work
RAM's byte at ADDR written at that frame's VBLANK), for gref and gng_headless (and makaimura --inputs); MAME's
tools/mame/trace.lua does not write the RAM, so tools/mame_trace.py does not take them."""
import os

FRAMES = 12000


def script(stage, loop2=False, weapon=None, cleared_at=None, midway=False):
    what = f"stage {stage}" + (", from its midway point" if midway else "") + (", the second loop" if loop2 else "") + (f", weapon {weapon}" if weapon is not None else "")
    lines = [f"# {what}: coin, start, the RAM written, the lives kept (a test's cheat; tests/controls/make_stages.py)"]
    lines += ["1300 system bf", "1306 system ff", "1400 system fe", "1406 system ff", f"1410 m0072 {stage:02x}"]
    if midway:
        lines += ["1410 m0081 01"]
    if loop2:
        lines += ["1410 m007f 01"]
    if weapon is not None:
        lines += [f"1410 m0073 {weapon:02x}"]
    events = {}
    if cleared_at is not None:
        # the stage cleared as E799-E7A0 clears it (<$08 = 2, <$0B = <$0E = 0: the stage's end, 711F): the last
        # stage of the second loop so, the ending (7D3F, 7E18: <$7F = 2, 71D7); the controls let go from there
        events.setdefault(cleared_at, []).extend(["p1 ff", "m0008 02", "m000b 00", "m000e 00"])
    for frame in range(1500, FRAMES, 300):
        events.setdefault(frame, []).append("m0060 05")
    for frame in range(1600, cleared_at if cleared_at is not None else FRAMES, 4):
        p1 = 0xFE                      # right held
        phase = (frame - 1600) // 4 % 40
        if (frame // 1200) % 5 == 4 and phase < 20:
            p1 = 0xFD                  # a while to the left
        if phase in (0, 1, 2):
            p1 &= ~0x20                # button 2: jump
        if phase in (10, 11, 20, 21, 30, 31):
            p1 &= ~0x10                # button 1: throw
        events.setdefault(frame, []).append(f"p1 {p1:02x}")
    for frame in sorted(events):
        lines += [f"{frame} {event}" for event in events[frame]]
    return "\n".join(lines) + "\n"


if __name__ == "__main__":
    here = os.path.dirname(os.path.abspath(__file__))
    for stage in range(1, 7):
        with open(os.path.join(here, f"stage{stage}.txt"), "w", newline="\n") as out:
            out.write(script(stage))
    for stage in (1, 3, 4):
        with open(os.path.join(here, f"midway{stage}.txt"), "w", newline="\n") as out:
            out.write(script(stage, midway=True))
    for name, text in (("loop2_stage0", script(0, loop2=True)), ("loop2_stage6", script(6, loop2=True, weapon=3)),
                       ("ending", script(6, loop2=True, weapon=3, cleared_at=2100))):
        with open(os.path.join(here, f"{name}.txt"), "w", newline="\n") as out:
            out.write(text)
