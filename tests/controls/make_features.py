"""Writes the scripts that reach what the others do not (tools/coverage.py shows what is left):

service_mode.txt: the board's test mode (DSW1 bit 6 on: the game state 3, irq_states[3]) and its screens, the
    controls pressed one after another.
weapons.txt: stage 1 with each weapon in turn (<$73: 0 the torch, 2 the axe, 3 the cross, 4 the dagger; the lance,
    1, is everyone's), thrown in bursts, the lives kept (a test's cheat).
cocktail.txt: a cocktail cabinet with the picture flipped (DSW2 bit 2, DSW1 bit 7), 7 lives, two players taking
    turns (the picture turned for the second), a game over each and a continue (a coin, then a button, 76D8).
dips.txt: free play (no coin), very difficult, bonus lives at 30k and 80k only, the demo's sound off, the second
    coin slot's coinage; a game with the moves of long_play.

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
    return s.text(dsw1=(DSW1 & ~0x0F & ~0x10 & ~0x20), dsw2=DSW2 & ~0x60 & ~0x18)


if __name__ == "__main__":
    here = os.path.dirname(os.path.abspath(__file__))
    for name, text in (("service_mode", service_mode()), ("weapons", weapons()), ("cocktail", cocktail()),
                       ("dips", dips()), ("clears", clears(CLEARS))):
        with open(os.path.join(here, f"{name}.txt"), "w", newline="\n") as out:
            out.write(text)
