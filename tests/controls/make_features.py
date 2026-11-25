"""Writes the scripts that reach what the others do not (tools/coverage.py shows what is left):

service_mode.txt: the board's test mode (DSW1 bit 6 on: the game state 3, irq_states[3]) and its screens, the
    controls pressed one after another.
weapons.txt: stage 1 with each weapon in turn (<$73: 0 the torch, 2 the axe, 3 the cross, 4 the dagger; the lance,
    1, is everyone's), thrown in bursts, the lives kept (a test's cheat).
cocktail.txt: a cocktail cabinet with the picture flipped (DSW2 bit 2, DSW1 bit 7), 7 lives, two players taking
    turns (the picture turned for the second), a game over each and a continue (a coin, then a button, 76D8).
dips.txt: free play (no coin), very difficult, bonus lives at 30k and 80k only, the demo's sound off, the second
    coin slot's coinage; a game with the moves of long_play.
service_pages.txt: the test mode entered with coin 1 held (service_start, frame 494: the player's step 1, the
    hidden pages), its pages turned with both start buttons (service_pages): the cross hatch and scroll test (the
    background moved by the stick), the object RAM test; the controls pressed one after another on each.
ranking.txt: three games whose score is written (a test's cheat) into the ranking (first, then second and third
    places' scores), their last life lost (<$60 = 0) and the initials entered (play_step3: the cursor moved about
    the letters by a pseudo-random sequence, letters taken, deleted, the end).
faulty_sprite_ram.txt, faulty_text_ram.txt, bad_rom.txt, bad_bank.txt: a faulty board at power-on, for the boot's
    tests' failures (bank 3's ram_rom_test, rom_test): "0 faulty ADDR" a RAM cell (1E00-2FFF) storing its bit 0
    inverted (gng_board.faulty_cell: 1F00, the sprite RAM, in the first RAM loop; 2100, the text layer, in the
    second), "0 rOFFSET VALUE" the program ROM region's byte changed (a bad ROM: F13A, fill in 6000-FFFF, breaks
    its sum; 17E3D, fill in bank 3, breaks the sum the RAM copy at 1A00 makes). gref and gng_headless only.

The lines are "FRAME PORT VALUE" (the byte as the board reads it, active low; dsw1 and dsw2 from the power on) and
"FRAME mADDR VALUE" (the work RAM written: a test's cheat), for gref, gng_headless and makaimura --inputs. The moves
come from a fixed pseudo-random sequence: the files are the same every time."""
import os

RIGHT, LEFT, DOWN, UP, FIRE, JUMP = 0x01, 0x02, 0x04, 0x08, 0x10, 0x20
START1, START2, SERVICE, COIN1, COIN2 = 0x01, 0x02, 0x20, 0x40, 0x80
DSW1, DSW2 = 0xDF, 0xFB       # the factory settings (gng/board.h)
# clears.txt: the frames each stage is cleared at (while it is played: tools/coverage.py's runs show it), and which
# of them has the cross given first
CLEARS = [2100 + 1500 * i for i in range(15)]
CROSS_AT = 6


class Script:
    def __init__(self, title, seed):
        self.lines = [f"# {title} (tests/controls/make_features.py)"]
        self.changes = {}
        self.state = seed

    def rand(self, n):
        self.state = (self.state * 1103515245 + 12345) & 0x7FFFFFFF
        return (self.state >> 8) % n

    def press(self, frame, port, value):
        self.changes.setdefault(frame, {})[port] = value

    def tap(self, frame, port, bits, length=6):
        self.press(frame, port, 0xFF & ~bits)
        self.press(frame + length, port, 0xFF)

    def moves(self, start, end, ports=("p1",)):
        """long_play's: a move every 8-40 frames, throws and jumps as presses"""
        moves = [RIGHT, RIGHT, RIGHT, LEFT, RIGHT | JUMP, JUMP, FIRE, RIGHT | FIRE, DOWN | FIRE, UP, DOWN,
                 LEFT | JUMP, 0]
        frame = start
        while frame < end:
            move = moves[self.rand(len(moves))]
            length = 8 + self.rand(33)
            for f in range(frame, min(frame + length, end), 2):
                value = 0xFF & ~move
                if move & FIRE and (f - frame) % 6 >= 3:
                    value |= FIRE
                if move & JUMP and f - frame >= 4:
                    value |= JUMP
                for port in ports:
                    self.press(f, port, value)
            frame += length
        for port in ports:
            self.press(frame, port, 0xFF)

    def text(self, dsw1=DSW1, dsw2=DSW2):
        lines = self.lines + [f"0 dsw1 {dsw1:02x}", f"0 dsw2 {dsw2:02x}"]
        for frame in sorted(self.changes):
            for port, value in self.changes[frame].items():
                lines.append(f"{frame} {port} {value:02x}")
        return "\n".join(lines) + "\n"


def service_mode():
    s = Script("the test mode: DSW1's service switch on from the power on, its screens' controls", 0x5E51)
    # every control in turn, now and then, for 6000 frames
    controls = [("p1", UP), ("p1", DOWN), ("p1", LEFT), ("p1", RIGHT), ("p1", FIRE), ("p1", JUMP), ("p2", UP),
                ("p2", FIRE), ("system", START1), ("system", START2), ("system", COIN1), ("system", COIN2),
                ("system", SERVICE)]
    frame = 400
    while frame < 6000:
        port, bits = controls[s.rand(len(controls))]
        s.tap(frame, port, bits, 4 + s.rand(20))
        frame += 30 + s.rand(90)
    return s.text(dsw1=DSW1 & ~0x40)


def service_pages():
    s = Script("the test mode entered with coin 1 held (its hidden pages), the pages turned with both starts", 0x5E52)
    s.press(0, "system", 0xFF & ~COIN1)
    s.press(520, "system", 0xFF)
    controls = [("p1", UP), ("p1", DOWN), ("p1", LEFT), ("p1", RIGHT), ("p1", FIRE), ("p1", JUMP), ("p2", UP),
                ("p2", FIRE), ("system", SERVICE)]
    frame = 600
    for page in range(7):
        s.tap(frame, "system", START1 | START2, 8)
        frame += 60
        end = frame + 900
        while frame < end:
            port, bits = controls[s.rand(len(controls))]
            s.tap(frame, port, bits, 4 + s.rand(60))
            frame += 30 + s.rand(90)
    return s.text(dsw1=DSW1 & ~0x40)


def service_dips():
    s = Script("the test mode in a cocktail cabinet: the settings page through DSW1's changes, the sound test", 0x5E53)
    # DSW1, read every frame (irq: dip1_settings), the page drawn every 16 frames (service_settings_draw): coin A's
    # coinage (bits 0-3, all set: free play), coin B's slot (bit 4), the demo sounds (bit 5), each for 60 frames;
    # DSW2 (read at the start only) bit 2 set: a cocktail cabinet
    frame = 400
    for dsw1 in (DSW1, DSW1 & ~0x0F, DSW1 & ~0x0E, DSW1 & ~0x0D, DSW1 & ~0x0B, DSW1 & ~0x07, DSW1 & ~0x10,
                 DSW1 & ~0x1C, DSW1 & ~0x20, DSW1 ^ 0x2F):
        s.press(frame, "dsw1", dsw1 & ~0x40)
        frame += 60
    s.press(frame, "dsw1", DSW1 & ~0x40)
    # the sound test (the same page): its code up past 09 and 15 (0A-0B and 16-17 skipped), down past them again
    for move in [RIGHT] * 24 + [LEFT] * 24:
        s.tap(frame, "p1", move, 4)
        frame += 12
    # the next page (both starts) and back to the first (two pages without coin 1 held at the start)
    s.tap(frame, "system", START1 | START2, 8)
    s.tap(frame + 300, "system", START1 | START2, 8)
    s.press(frame + 600, "p1", 0xFF)
    return s.text(dsw1=DSW1 & ~0x40, dsw2=DSW2 | 0x04)


def coinage():
    s = Script("coin A at 4 coins a credit: the title's coinage, the service switch turned on in the attract mode, "
               "10 lives", 0x5E54)
    # the title's coinage drawn with "COINS" (task_draw_credit_count); DSW1 read every frame (irq: dip1_settings): the
    # service switch on (<$0F) as coins go in, the credits not drawn then (task_draw_credits); off again, a game with
    # its lives written 10 (a test's cheat: the bonus lives could give them), the icons drawn as 7 at the next life
    # (task_draw_lives); then the continue
    s.press(2000, "dsw1", DSW1 & ~0x0D & ~0x40)
    for frame in range(2100, 2260, 40):
        s.tap(frame, "system", COIN1)
    s.press(2600, "dsw1", DSW1 & ~0x0D)
    # one credit: start 2 refused (attract_demo_inputs), start 1
    s.tap(2650, "system", START2)
    s.tap(2700, "system", START1)
    s.press(3000, "m0060", 0x0A)
    # four coins more (a credit), the last life (the lives written 1), the continue taken: fire held and start 1
    for frame in range(4000, 4160, 40):
        s.tap(frame, "system", COIN1)
    s.press(4200, "m0060", 0x01)
    for frame in range(4300, 7000, 30):
        s.tap(frame, "p1", FIRE, 20)
        s.tap(frame + 10, "system", START1, 6)
    s.press(8000, "p1", 0xFF)
    return s.text(dsw1=DSW1 & ~0x0D)


def ranking():
    s = Script("three games into the ranking (the score written: a test's cheat), the initials entered", 12345)
    # the scores (BCD, <$68-<$6B), the ranking's ten being 10000: first place, then second, then third; the lives
    # (<$60) left 1, the game over coming when a death takes them to 0 (0 would wrap to FF; the first game's score
    # gives two more lives, which its 0 + 2 stays as it was written)
    # the initials, from the entry's start (name_entry_0, after game_over_ranking; the cursor on END): a direction
    # moves the cursor as it is pressed, then every 10 frames held (name_entry_3_a, _b), the fire button picks the
    # letter under it (name_entry_3_c); the letters in rows of 13, the last the signs, "@" (a space), RUB (the last
    # letter taken back) and END. The first game: right from END (wrapped to A) and back left, left to "@" and right to RUB, both picked, RUB again (nothing to
    # take back), "@" again (the name a space, taken off as the ranking's name is drawn: name_entry_4_draw), down
    # (the last row's wraps to the first), left to A and from A (to END), picked; the second: up to the first row
    # (from the last row's END to its M), right past its end (onto the next row), left from that row's start
    # (back), up from the first row (wrapped to the last) and down again, three letters; the third: nothing until the entry's time is up (name_entry_3: phase_timer)
    steps = {3800: [RIGHT, LEFT, LEFT, LEFT, FIRE, RIGHT, FIRE, FIRE, LEFT, FIRE, DOWN] + [LEFT] * 11 + [FIRE],
             8550: [UP] * 4 + [RIGHT, LEFT, UP, DOWN, DOWN, FIRE, FIRE, FIRE], 14390: []}
    for start, score, lives in ((1300, (0x00, 0x09, 0x87, 0x60), 0x00), (7000, (0x00, 0x01, 0x05, 0x00), 0x01),
                                (12700, (0x00, 0x01, 0x00, 0x10), 0x01)):
        s.tap(start, "system", COIN1)
        s.tap(start + 100, "system", START1)
        for i, value in enumerate(score):
            s.press(start + 400, f"m{0x68 + i:04x}", value)
        s.press(start + 400, "m0060", lives)
    # standing still until the zombies take the last life; then the entry
    for frame, moves in steps.items():
        for move in moves:
            s.tap(frame, "p1", move, 6)
            frame += 16
    s.press(17900, "p1", 0xFF)      # the third entry's time running out
    return s.text()


def weapons():
    s = Script("stage 1 with each weapon in turn (<$73), thrown in bursts; the lives kept", 0x3EA9)
    s.tap(1300, "system", COIN1)
    s.tap(1400, "system", START1)
    for frame in range(1500, 14000, 300):
        s.press(frame, "m0060", 0x05)
    for i, weapon in enumerate((0, 2, 3, 4, 0, 2, 3, 4)):
        start = 2000 + 1500 * i
        s.press(start, "m0073", weapon)
        s.moves(start, start + 1500)
    return s.text()


def cocktail():
    s = Script("a cocktail cabinet, the picture flipped, 7 lives: two players' turns, their game overs, a continue",
               0xC0C7)
    s.tap(1300, "system", COIN1)
    s.tap(1310, "system", COIN1)
    s.tap(1400, "system", START2)
    # both players' moves (each on their own controls: the cocktail's second player reads 3002)
    s.moves(1500, 9000, ports=("p1", "p2"))
    # coins for the continue, then a button now and then: the continue's (76D8)
    for frame in range(9000, 22000, 2000):
        s.tap(frame, "system", COIN1)
        s.tap(frame + 40, "p1", FIRE)
        s.tap(frame + 80, "p2", FIRE)
        s.moves(frame + 200, frame + 1900, ports=("p1", "p2"))
    return s.text(dsw1=DSW1 & ~0x80, dsw2=(DSW2 | 0x04) & ~0x03)


def clears(times):
    s = Script("every stage cleared in turn as the program clears one (E799: <$08 = 2), the cross given before the "
               "first loop's last stage is cleared the second time; the lives kept", 0xC1EA)
    s.tap(1300, "system", COIN1)
    s.tap(1400, "system", START1)
    for frame in range(1500, times[-1] + 3000, 300):
        s.press(frame, "m0060", 0x05)
    s.moves(1500, times[-1] + 3000)
    for i, frame in enumerate(times):
        if i == CROSS_AT:
            s.press(frame - 200, "m0073", 0x03)
        for address, value in (("m0008", 0x02), ("m000b", 0x00), ("m000e", 0x00)):
            s.press(frame, address, value)
    return s.text()


def dips():
    s = Script("free play, very difficult, bonus lives at 30k and 80k only, no demo sound, coin B's coinage", 0xD195)
    s.tap(1300, "system", START1)
    s.moves(1400, 12000)
    for frame in range(12000, 20000, 2500):
        s.tap(frame, "system", START1)
        s.moves(frame + 100, frame + 2400)
    return s.text(dsw1=(DSW1 & ~0x0F & ~0x10) | 0x20, dsw2=DSW2 & ~0x60 & ~0x18)  # bit 5 set: no demo sound


def faulty(title, line):
    """a board with a fault from power-on (gng_board.faulty_cell, or a ROM byte changed): the boot's message"""
    return f"# {title} (a check's faulty board; tests/controls/make_features.py)\n0 {line}\n"


if __name__ == "__main__":
    here = os.path.dirname(os.path.abspath(__file__))
    for name, text in (("service_mode", service_mode()), ("weapons", weapons()), ("cocktail", cocktail()),
                       ("dips", dips()), ("clears", clears(CLEARS)), ("service_pages", service_pages()),
                       ("service_dips", service_dips()), ("coinage", coinage()),
                       ("ranking", ranking()),
                       ("faulty_sprite_ram", faulty("the sprite RAM's cell 1F00 faulty", "faulty 1f00")),
                       ("faulty_text_ram", faulty("the text layer's cell 2100 faulty", "faulty 2100")),
                       ("bad_rom", faulty("a bad ROM: 6000-FFFF's sum wrong (fill at F13A)", "rf13a 01")),
                       ("bad_bank", faulty("a bad ROM: bank 3's sum wrong (fill at its 5E3D)", "r17e3d 00"))):
        with open(os.path.join(here, f"{name}.txt"), "w", newline="\n") as out:
            out.write(text)
