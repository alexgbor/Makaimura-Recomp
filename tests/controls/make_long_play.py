"""Writes long_play.txt: about eleven minutes of play over several credits, to reach more of the game than
walk_right does: one-player games and a two-player one, the player moved by a fixed pseudo-random sequence (both
ways, ladders, crouching, jumps, bursts of throws), new coins after each game, and the service switch at the end.
The script's lines are "FRAME PORT VALUE" (the byte as the board reads it, active low), for gref --inputs,
gng_headless --inputs and tools/mame/trace.lua alike. The sequence is the script's own (a linear congruential
generator from a fixed seed), so the file is the same every time."""
import os

FRAMES = 40000
RIGHT, LEFT, DOWN, UP, FIRE, JUMP = 0x01, 0x02, 0x04, 0x08, 0x10, 0x20
START1, START2, SERVICE, COIN1, COIN2 = 0x01, 0x02, 0x20, 0x40, 0x80

state = 0x6809


def rand(n):
    global state
    state = (state * 1103515245 + 12345) & 0x7FFFFFFF
    return (state >> 8) % n


changes = {}


def press(frame, port, value):
    changes.setdefault(frame, {})[port] = value


def coin_and_start(frame, players):
    for i in range(players):
        press(frame + 10 * i, "system", 0xFF & ~COIN1)
        press(frame + 10 * i + 6, "system", 0xFF)
    press(frame + 100, "system", 0xFF & ~(START1 if players == 1 else START2))
    press(frame + 106, "system", 0xFF)


# the credits: one player, one player, two players, one player
credits = [(1300, 1), (9000, 1), (17000, 2), (27000, 1)]
for frame, players in credits:
    coin_and_start(frame, players)

# the moves: from each credit's start, a move every 8-40 frames
moves = [RIGHT, RIGHT, RIGHT, LEFT, RIGHT | JUMP, JUMP, FIRE, RIGHT | FIRE, DOWN | FIRE, UP, DOWN, LEFT | JUMP, 0]
frame = 1500
while frame < FRAMES - 1500:
    move = moves[rand(len(moves))]
    length = 8 + rand(33)
    for f in range(frame, frame + length, 2):
        value = 0xFF & ~move
        # fire is pressed and released (a throw a press), every 6 frames while it is held
        if move & FIRE and (f - frame) % 6 >= 3:
            value |= FIRE
        # a jump is a press too
        if move & JUMP and f - frame >= 4:
            value |= JUMP
        press(f, "p1", value)
        press(f, "p2", value)
    frame += length
press(frame, "p1", 0xFF)
press(frame, "p2", 0xFF)

# the service switch, held for a while near the end
press(FRAMES - 1200, "system", 0xFF & ~SERVICE)
press(FRAMES - 900, "system", 0xFF)

lines = ["# about eleven minutes of play over several credits (tests/controls/make_long_play.py)"]
last = {}
for f in sorted(changes):
    for port in ("system", "p1", "p2"):
        if port in changes[f] and last.get(port) != changes[f][port]:
            lines.append(f"{f} {port} {changes[f][port]:02x}")
            last[port] = changes[f][port]
with open(os.path.join(os.path.dirname(os.path.abspath(__file__)), "long_play.txt"), "w", newline="\n") as out:
    out.write("\n".join(lines) + "\n")
