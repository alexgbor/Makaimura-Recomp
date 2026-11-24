"""Writes stage1.txt to stage6.txt: a coin, start, the stage set right after the start (the work RAM's <$72, the
stage: a test's cheat), the lives kept (<$60, written back every 300 frames), then the player walking right,
jumping and throwing (as make_walk.py), a while to the left now and then. 12000 frames each: their busy scenes slow
the game down in board mode (docs/accuracy.md #1).

ending.txt: loop2_stage6's, its stage cleared at frame 2100 (while it is played) as the program clears one (E799: <$08 = 2), so the
ending plays (7D3F, 7E18, 71D7) and the game goes back to the first loop.

midway1.txt, midway3.txt, midway4.txt: stages 1, 3 and 4 from their midway point (<$81 = 1, as ECB0 sets it
when the player passes it: the start point from 6CEA's second table, 6D0A), where the scripts from the start do not
reach: the town's windows, the bridge's flames (and the tiles stage 3 draws from the 6809's stack).

tower5.txt: stage 5 with Arthur and the camera moved up the tower at frame 2050 (the player's x and y, <$9C-<$9F:
the camera 0x47 below him, as at the stage's start), where the dragon flies (object_dragon) and Big Man walks.

satan4.txt: stage 4 with Arthur held near the top of its last climb (his x and y written every other frame from 2050
to 2398, while the camera catches up with him), where Satan comes (object_satan).

loop2_stage0.txt and loop2_stage6.txt: the second loop (<$7F = 1, as 7298 leaves it after the first loop's last
stage; <$71 is the stage's own state: 0 its start, 1 playing, 7AA0), stage 6 with the cross (<$73 = 3, the weapon
the last stage asks for: 7129).

The scenes (scene(): Arthur and the camera placed at a frame, Arthur held there a while, then a pattern of
inputs; the lives kept), each for what the scripts above never ran (tools/coverage.py's routines never run):

bosses5.txt: stage 5's tower. Arthur on Big Man's floor (06F7) at four places in turn: walking about at 1E10 (Big
Man keeps its distance, backs off, comes closer), throwing at 1E00 (it walks, jumps at the shots, charges, throws:
Arthur dies), at 1E30 (it is beaten: hit, dying, removed); then held level with the dragon's head (07D8: the
dragon waits while the player is more than 0x20 below it, D708) until it chases him, fires and is beaten.
red_arremer0.txt: the graveyard's Red Arremer (05C0): Arthur put back at 0560 every 900 frames throwing to the
right, the lance (its swoops, its fire, action 4) then the torch from the fourth time (it is beaten).
satan4_beaten.txt: satan4's place, Arthur held there throwing (Satan beaten at once: its hit, dying, removed;
the key's message and walk), then left alone.
magician0.txt: stage 0's first gravestone (0100) shot crouching: 15 hits (a prop's +1A) arm the spawner at 0760,
whose kind 7 is the magician (object_type_07); Arthur turns to the zombies as it comes and its spell (an enemy shot
of kind 0, enemy_shot_start_0) turns him (player state 4) for a while.
stage0_end.txt: Arthur put at the graveyard's end (camera_x_max 0D00), the stage cleared there (E799's <$08 = 2),
then put on the key (camera_x_max + 0x25): the key taken, the gate, the armour back, the map's marker to stage 1.
no_cross5.txt: stage 5 cleared with the torch: "THIS WEAPON HAS NOT EFFECT..." (7177), then stage 4 again.
astaroth6.txt: the second loop's last room (<$7F = 1), the cross, Arthur held in the air at 0A50 throwing: only the
cross hurts Astaroth (EE91's mask for type 13, 10, is the cross's of EE89), and only within 10 columns and 9 rows of
his point (EF92's box), his head's height (0A60): standing (0A46) the shots pass below; 10 hits (+0x26), then his
death and the ending's start.
illusion6.txt: stage 6 of the first loop cleared with the cross in the last room (<$71 = 2, as 7CF9 sets it):
"THIS ROOM IS AN ILLUSION..." (7272), then the second loop.

The lines are "FRAME PORT VALUE" (the byte as the board reads it, active low) and "FRAME mADDR VALUE" (the work
RAM's byte at ADDR written at that frame's VBLANK), for gref and gng_headless (and makaimura --inputs); MAME's
tools/mame/trace.lua does not write the RAM, so tools/mame_trace.py does not take them."""
import os

FRAMES = 12000

RIGHT, LEFT, DOWN, UP, FIRE, JUMP = 0x01, 0x02, 0x04, 0x08, 0x10, 0x20


def pattern(name, step):
    """the scenes' inputs (p1, active low) at a step of 4 frames from the pattern's start"""
    held = 0
    if name == "walk":
        held = RIGHT
    elif name in ("throw_left", "throw_right"):
        if step % 60 < 3:
            held |= LEFT if name == "throw_left" else RIGHT        # turned that way now and then
        if step % 4 < 2:
            held |= FIRE
        if step % 50 in (20, 21, 22):
            held |= JUMP
    elif name == "crouch_throw":
        held = DOWN | (FIRE if step % 4 < 2 else 0)
    elif name == "wander":
        held = RIGHT if (step // 40) % 2 == 0 else LEFT
        if step % 4 < 2:
            held |= FIRE
        if step % 30 in (5, 6, 7):
            held |= JUMP
    return 0xFF & ~held


def scene(title, stage, places, frames, ram=(), weapon=None):
    """places: (frame, x, y, hold, pattern[, "camera"]): Arthur put at x, y at that frame (x None: left where he is)
    with the camera 0x80 left of him and 0x47 below (or, without "camera", left to follow him), held there for hold
    frames (written every other frame), then the pattern until the next place; ram: (frame, address, value)"""
    events = {}
    lines = [f"# {title} (a test's cheat: the RAM written; tests/controls/make_stages.py)",
             "1300 system bf", "1306 system ff", "1400 system fe", "1406 system ff", f"1410 m0072 {stage:02x}"]
    if weapon is not None:
        lines.append(f"1410 m0073 {weapon:02x}")
    for frame in range(1500, frames, 300):
        events.setdefault(frame, []).append("m0060 05")
    for frame, address, value in ram:
        events.setdefault(frame, []).append(f"m{address:04x} {value:02x}")
    ends = [p[0] for p in places[1:]] + [frames]
    for (start, x, y, hold, name, *options), end in zip(places, ends):
        if x is not None:
            place = [f"m0506 {x >> 8:02x}", f"m0507 {x & 0xFF:02x}", f"m0509 {y >> 8:02x}", f"m050a {y & 0xFF:02x}"]
            camera_x, camera_y = (x - 0x80) & 0xFFFF, (y - 0x47) & 0xFFFF    # (16 bits, as the 6809 has them)
            camera = [f"m009c {camera_x >> 8:02x}", f"m009d {camera_x & 0xFF:02x}", f"m009e {camera_y >> 8:02x}",
                      f"m009f {camera_y & 0xFF:02x}"] if "camera" in options else []
            events.setdefault(start, []).extend(place + camera)
            for frame in range(start + 2, start + hold, 2):
                events.setdefault(frame, []).extend(place)
        for frame in range(start, end, 4):
            events.setdefault(frame, []).append(f"p1 {pattern(name, (frame - start) // 4):02x}")
    for frame in range(1600, places[0][0], 4):
        events.setdefault(frame, []).append("p1 ff")
    for frame in sorted(events):
        lines += [f"{frame} {event}" for event in events[frame]]
    return "\n".join(lines) + "\n"


def cleared(frame):
    """the stage cleared at that frame as E799-E7A0 clears it (<$08 = 2, <$0B = <$0E = 0)"""
    return [(frame, 0x08, 2), (frame, 0x0B, 0), (frame, 0x0E, 0)]


def scenes():
    big_man, dragon = 0x06F7, 0x07D8
    yield "bosses5", scene("stage 5: Big Man at three distances, then the dragon", 5, [
        (2050, 0x1E10, big_man, 0, "wander", "camera"), (3000, 0x1E00, big_man, 0, "throw_left", "camera"),
        (3900, 0x1E30, big_man, 0, "throw_left", "camera"), (4900, 0x1E60, dragon, 600, "throw_left", "camera")],
        7200)
    yield "red_arremer0", scene("stage 0: Red Arremer, the lance then the torch", 0,
                                [(2050 + 900 * i, 0x0560, 0x0040, 0, "throw_right", "camera") for i in range(10)],
                                11050, ram=[(2050 + 900 * 3 - 10, 0x73, 0)])
    yield "satan4_beaten", scene("stage 4: Satan beaten, the key's walk", 4, [
        (2050, 0x1C40, 0x0680, 600, "throw_right"), (2650, None, None, 0, "still")], 4000)
    yield "magician0", scene("stage 0: a gravestone shot 15 times, the magician and its spell", 0, [
        (1960, None, None, 0, "walk"), (2030, None, None, 0, "crouch_throw"), (2340, None, None, 0, "throw_left"),
        (3200, None, None, 0, "wander")], 5000)
    yield "stage0_end", scene("stage 0 cleared at its end: the key, the gate, the map", 0, [
        (2050, 0x0D10, 0x003E, 30, "still", "camera"), (2400, 0x0D25, 0x003E, 20, "still", "camera")], 8000,
        ram=cleared(2150))
    yield "no_cross5", scene("stage 5 cleared with the torch: the weapon has no effect", 5,
                             [(2050, None, None, 0, "still")], 5000, ram=cleared(2100), weapon=0)
    yield "astaroth6", scene("stage 6 of the second loop: Astaroth beaten with the cross, thrown at his head's height", 6,
                             [(2050, 0x1F30, 0x0A50, 2500, "throw_right", "camera")], 5000, ram=[(1410, 0x7F, 1)],
                             weapon=3)
    yield "illusion6", scene("stage 6 of the first loop cleared in the last room with the cross: the illusion", 6,
                             [(2050, None, None, 0, "still")], 6000, ram=[(2400, 0x71, 2)] + cleared(2400), weapon=3)


def script(stage, loop2=False, weapon=None, cleared_at=None, midway=False, moved_to=None, held=False):
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
    if moved_to is not None:
        # Arthur's x and y (0506, 0509: 16 bits each) and the camera's (<$9C, <$9E) written while he plays
        x, y = moved_to
        camera_x, camera_y = x - 0x80, y - 0x47
        place = [f"m0506 {x >> 8:02x}", f"m0507 {x & 0xFF:02x}", f"m0509 {y >> 8:02x}", f"m050a {y & 0xFF:02x}"]
        if held:
            # Arthur kept there while the camera scrolls to him (it follows him a few pixels a frame); the camera
            # not written: moved at once, it leaves the objects it passes behind (object_off_screen removes them)
            for frame in range(2050, 2400, 2):
                events.setdefault(frame, []).extend(place)
        else:
            events.setdefault(2050, []).extend(place + [f"m009c {camera_x >> 8:02x}", f"m009d {camera_x & 0xFF:02x}",
                                                        f"m009e {camera_y >> 8:02x}", f"m009f {camera_y & 0xFF:02x}"])
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
    with open(os.path.join(here, "tower5.txt"), "w", newline="\n") as out:
        out.write(script(5, moved_to=(0x1E60, 0x07DD)))
    with open(os.path.join(here, "satan4.txt"), "w", newline="\n") as out:
        out.write(script(4, moved_to=(0x1C40, 0x0680), held=True))
    for name, text in (("loop2_stage0", script(0, loop2=True)), ("loop2_stage6", script(6, loop2=True, weapon=3)),
                       ("ending", script(6, loop2=True, weapon=3, cleared_at=2100))):
        with open(os.path.join(here, f"{name}.txt"), "w", newline="\n") as out:
            out.write(text)
    for name, text in scenes():
        with open(os.path.join(here, f"{name}.txt"), "w", newline="\n") as out:
            out.write(text)
