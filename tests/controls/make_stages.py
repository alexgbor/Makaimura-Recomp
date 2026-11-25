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
whose kind 7 is the magician (object_magician); Arthur turns to the zombies as it comes and its spell (an enemy shot
of kind 0, enemy_shot_start_0) turns him (player state 4) for a while.
stage0_end.txt: Arthur put at the graveyard's end (camera_x_max 0D00), the stage cleared there (E799's <$08 = 2),
then put on the key (camera_x_max + 0x25): the key taken, the gate, the armour back, the map's marker to stage 1.
no_cross5.txt: stage 5 cleared with the torch: "THIS WEAPON HAS NOT EFFECT..." (7177), then stage 4 again.
astaroth6.txt: the second loop's last room (<$7F = 1), the cross, Arthur held in the air at 0A50 throwing: only the
cross hurts Astaroth (EE91's mask for type 13, 10, is the cross's of EE89), and only within 10 columns and 9 rows of
his point (EF92's box), his head's height (0A60): standing (0A46) the shots pass below; 10 hits (+0x26), then his
death and the ending's start.
window_giant1.txt: stage 1 from its midway point (<$81 = 1), Arthur put left of the town's lowest window (the
giant at 1500, 0050) throwing at it: 10 hits (+0x26; window_giant_flinch at each), its turn, its death.
pillar2.txt: stage 2's caves, Arthur held left of the pillar at 0260, 0248 throwing at it, unhurt (<$AD, the blink
after a hit, written every 60 frames: player_hit_check lets nothing through while it lasts): its second attack, 4
hits (+0x26), its death.
magician_beaten0.txt: magician0's gravestone, then Arthur put 0x20 left of the magician (0100, 004E) as it comes,
unhurt (<$AD as pillar2's): it can be hit only while it casts (+0 = 1, 16 frames; 2 as it comes and goes, which
object_check_hits passes over) and the gravestone stops the shots thrown standing (003B), so a throw early in a
jump (its row within 13 of the magician's): hit, dying.
crows1.txt: stage 1 from its midway point, Arthur put before the perched crows (variant 3, 1840-19B0 in the stage's
list) and left standing, then walking about, unhurt: they take off at him; one flies in from the side (variant 4).
flying_knights0.txt: stage 0 from x 0720, Arthur unhurt running right (jumping, throwing) then walking about: the
map's event at the water arms spawner 3, the shielded flying knights (object type 3, in no stage's list) come in
waves, fly their three ways (variants 0-2) and leave.
crows_forced1.txt: stage 1 from its midway point, Arthur running to the map's event 17 (spawner 1: the crows in from
the side, variant 4) and staying there, unhurt; four of those crows made variants 0 and 1 as they come in (their
variant written, their state, step and substep cleared): the code of crows the game never makes (only spawner 1's
lists 4-5 hold them, and no map event chooses those), run for the translation's check.
illusion6.txt: stage 6 of the first loop cleared with the cross in the last room (<$71 = 2, as 7CF9 sets it):
"THIS ROOM IS AN ILLUSION..." (7272), then the second loop.

The lines are "FRAME PORT VALUE" (the byte as the board reads it, active low) and "FRAME mADDR VALUE" (the work
RAM's byte at ADDR written as the CPU takes the IRQ after that frame's VBLANK), for gref and gng_headless (and makaimura --inputs); MAME's
tools/mame/trace.lua does not write the RAM, so tools/mame_trace.py does not take them."""
import os

FRAMES = 12000

RIGHT, LEFT, DOWN, UP, FIRE, JUMP = 0x01, 0x02, 0x04, 0x08, 0x10, 0x20


def pattern(name, step):
    """the scenes' inputs (p1, active low) at a step of 4 frames from the pattern's start"""
    held = 0
    if name == "walk":
        held = RIGHT
    elif name == "run":
        held = RIGHT | (JUMP if step % 8 < 2 else 0) | (FIRE if step % 4 == 1 else 0)    # jumping, throwing
    elif name in ("throw_left", "throw_right"):
        if step % 60 < 3:
            held |= LEFT if name == "throw_left" else RIGHT        # turned that way now and then
        if step % 4 < 2:
            held |= FIRE
        if step % 50 in (20, 21, 22):
            held |= JUMP
    elif name == "jump_throw":
        held = (JUMP, JUMP | FIRE, 0)[step] if step < 3 else (FIRE if step % 2 == 0 else 0)
    elif name == "crouch_throw":
        held = DOWN | (FIRE if step % 4 < 2 else 0)
    elif name == "wander":
        held = RIGHT if (step // 40) % 2 == 0 else LEFT
        if step % 4 < 2:
            held |= FIRE
        if step % 30 in (5, 6, 7):
            held |= JUMP
    return 0xFF & ~held


def forced(frame, record, variant):
    """the object at record (0880 + 0x30 n) made that variant, started again (state, step, substep 0)"""
    return [(frame, record + 2, variant)] + [(frame, record + offset, 0) for offset in (3, 4, 5)]


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
    unhurt = [(frame, 0xAD, 0x78) for frame in range(2300, 5000, 60)]
    yield "window_giant1", scene("stage 1 from its midway point: the window giant beaten", 1,
                                 [(2050, 0x14A0, 0x0047, 60, "throw_right", "camera")], 5000, ram=[(1410, 0x81, 1)])
    yield "pillar2", scene("stage 2: a pillar beaten", 2, [(2050, 0x0200, 0x023E, 900, "throw_right", "camera")], 5000,
                           ram=[(frame, 0xAD, 0x78) for frame in range(2050, 5000, 60)])
    yield "magician_beaten0", scene("stage 0: the magician beaten", 0, [
        (1960, None, None, 0, "walk"), (2030, None, None, 0, "crouch_throw"), (2340, 0x00E0, 0x003E, 40, "still", "camera"),
        (2392, None, None, 0, "jump_throw"), (2432, None, None, 0, "still"), (2470, None, None, 0, "wander")], 5000,
        ram=unhurt)
    yield "crows1", scene("stage 1 from its midway point: the perched crows take off", 1, [
        (2050, 0x1800, 0x0047, 60, "still", "camera"), (3000, None, None, 0, "wander")], 6000,
        ram=[(1410, 0x81, 1)] + [(frame, 0xAD, 0x78) for frame in range(2050, 6000, 60)])
    yield "flying_knights0", scene("stage 0 from x 0720: the flying knights over the water", 0, [
        (2050, 0x0720, 0x003E, 30, "run", "camera"), (4000, None, None, 0, "wander")], 6000,
        ram=[(frame, 0xAD, 0x78) for frame in range(2050, 6000, 60)])
    yield "crows_forced1", scene("stage 1: crows of variants 0 and 1, which the game never makes", 1, [
        (2050, 0x1800, 0x0047, 30, "run", "camera"), (2650, None, None, 0, "throw_right")], 7000,
        ram=[(1410, 0x81, 1)] + [(frame, 0xAD, 0x78) for frame in range(2050, 7000, 60)]
        + forced(2700, 0x0CA0, 0) + forced(2780, 0x0D60, 1) + forced(3300, 0x0CA0, 0) + forced(3380, 0x0D00, 0))
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
