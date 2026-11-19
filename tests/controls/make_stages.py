"""Writes stage2.txt and stage4.txt: a coin, start, the stage set right after the start (the work RAM's <$72, the
stage: a test's cheat), the lives kept (<$60, written back every 300 frames), then the player walking right,
jumping and throwing (as make_walk.py), a while to the left now and then. 12000 frames each: their busy scenes slow
the game down in board mode (docs/accuracy.md #1).

The lines are "FRAME PORT VALUE" (the byte as the board reads it, active low) and "FRAME mADDR VALUE" (the work
RAM's byte at ADDR written at that frame's VBLANK), for gref and gng_headless (and makaimura --inputs); MAME's
tools/mame/trace.lua does not write the RAM, so tools/mame_trace.py does not take them."""
import os

FRAMES = 12000


def script(stage):
    lines = [f"# stage {stage}: coin, start, <$72 = {stage}, the lives kept (a test's cheat; tests/controls/make_stages.py)"]
    lines += ["1300 system bf", "1306 system ff", "1400 system fe", "1406 system ff", f"1410 m0072 {stage:02x}"]
    events = {}
    for frame in range(1500, FRAMES, 300):
        events.setdefault(frame, []).append("m0060 05")
    for frame in range(1600, FRAMES, 4):
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
    for stage in (2, 4):
        with open(os.path.join(here, f"stage{stage}.txt"), "w", newline="\n") as out:
            out.write(script(stage))
