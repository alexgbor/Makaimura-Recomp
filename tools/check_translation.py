"""
Name:        check_translation.py
Description: the translated program (build/bin/gng_headless) against the reference (build/bin/gref) frame by
             frame: the bus at every IRQ the CPU takes (with its PC and its cycle), at every VBLANK, and the picture
Version:     0.1.0
Created:     19-11-2026

license:BSD-3-Clause
    python tools/check_translation.py [--frames N] [--inputs FILE ...] [--dsw1 HEX] [--dsw2 HEX] [--no-pictures]
                                      [--accuracy mame|board]

With no --inputs, the attract mode and every script of tests/controls/ (*.txt). Both runs are MAME 0.289's board
(MAME mode, the default; gref is checked against MAME itself by tools/mame_trace.py), or both the board's (board
mode: docs/accuracy.md; the reference's MAME core and the translation with the same differences).
"""
import argparse
import glob
import os
import subprocess
import sys

TOOLS = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(TOOLS)
EXE = ".exe" if os.name == "nt" else ""
BIN = os.path.join(ROOT, "build", "bin")


def lines(path, columns=None):
    with open(path) as handle:
        rows = [line.split() for line in handle if line.strip()]
    return [tuple(row[:columns] if columns else row) for row in rows]


def first_difference(a, b):
    for index, (x, y) in enumerate(zip(a, b)):
        if x != y:
            return index, x, y
    if len(a) != len(b):
        return min(len(a), len(b)), None, None
    return None


def run(name, frames, inputs, dsw, pictures, accuracy):
    build = os.path.join(ROOT, "build", "check")
    os.makedirs(build, exist_ok=True)
    outputs = {}
    for program in ("gref", "gng_headless"):
        prefix = os.path.join(build, f"{name}_{program}")
        command = [os.path.join(BIN, program + EXE), "--frames", str(frames), "--accuracy", accuracy,
                   "--irq-hashes", prefix + "_irq.txt", "--bus-hashes", prefix + "_bus.txt"]
        if pictures:
            command += ["--picture-hashes", prefix + "_pictures.txt"]
        # the sound board: every YM2203 write (time, chip, port, value) and the Z80's RAM at each one (the frames'
        # ends are not compared: MAME's core may stop inside an instruction there, the translation between two)
        command += ["--sound", prefix + "_sound.txt", "--ym-writes", prefix + "_ym.txt",
                    "--sound-marks", prefix + "_marks.txt", "--wav", prefix + ".wav"]
        if inputs:
            command += ["--inputs", inputs]
        for option, value in zip(("--dsw1", "--dsw2"), dsw):
            if value:
                command += [option, value]
        result = subprocess.run(command, cwd=ROOT, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, text=True)
        if result.returncode:
            print(f"  {program}: {result.stderr.strip()}")
        outputs[program] = prefix
    ok = True
    reference, translation = outputs["gref"], outputs["gng_headless"]
    irq_a, irq_b = lines(reference + "_irq.txt"), lines(translation + "_irq.txt")
    difference = first_difference(irq_a, irq_b)
    if difference:
        ok = False
        index, x, y = difference
        print(f"  IRQs: {len(irq_a)} in gref, {len(irq_b)} in the translation; the first different, IRQ {index}: "
              f"gref {' '.join(x) if x else '-'}, translation {' '.join(y) if y else '-'}")
    bus_a, bus_b = lines(reference + "_bus.txt", 2), lines(translation + "_bus.txt", 2)
    difference = first_difference(bus_a, bus_b)
    if difference:
        ok = False
        index, x, y = difference
        print(f"  VBLANKs: the first different, frame {index}: gref {x}, translation {y}")
    # the run's end: the last VBLANK (Z80 time: twice the 6809's). MAME's core stops in the middle of an instruction
    # there, the translation at its end: a write that instruction makes after the end is in one run only, so the
    # sound is compared up to the end
    end = 2 * (23040 + (frames - 1) * 25152)
    ym_a = [row for row in lines(reference + "_ym.txt") if int(row[0]) < end]
    ym_b = [row for row in lines(translation + "_ym.txt") if int(row[0]) < end]
    difference = first_difference(ym_a, ym_b)
    if difference:
        ok = False
        index, x, y = difference
        print(f"  YM2203 writes: {len(ym_a)} in gref, {len(ym_b)} in the translation; the first different, {index}: "
              f"gref {x}, translation {y}")
    difference = first_difference(lines(reference + "_marks.txt", 2)[:len(ym_a)],
                                  lines(translation + "_marks.txt", 2)[:len(ym_b)])
    if difference:
        ok = False
        index, x, y = difference
        print(f"  the Z80's RAM at the YM2203 writes: the first different, write {index}: gref {x}, translation {y}")
    # the sound itself: the reference's mix (ymfm's YM2203s) and the game's (runtime/src/ym2203.c), byte for byte
    samples = (end + 23) // 24    # (sample n starts at Z80 time 24 n: those that start before the end)
    wavs = []
    for path in (reference + ".wav", translation + ".wav"):
        with open(path, "rb") as handle:
            wavs.append(handle.read()[44:44 + 2 * samples])
    if len(wavs[0]) != 2 * samples or wavs[0] != wavs[1]:
        ok = False
        print("  the sound (WAV): different")
    for path in (reference + ".wav", translation + ".wav"):
        os.remove(path)
    if pictures:
        difference = first_difference(lines(reference + "_pictures.txt"), lines(translation + "_pictures.txt"))
        if difference:
            ok = False
            index, x, y = difference
            print(f"  pictures: the first different, frame {index}: gref {x}, translation {y}")
    print(f"{name} ({accuracy} mode): {len(bus_a)} frames, {len(irq_a)} IRQs, {len(ym_a)} YM2203 writes" +
          ", the sound" + (", the pictures" if pictures else "") +
          (": the same as gref's" if ok else ": DIFFERENT"))
    return ok


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[2])
    parser.add_argument("--frames", type=int, default=0, help="frames a run (default: 1200, or the script's end + 600)")
    parser.add_argument("--inputs", nargs="*")
    parser.add_argument("--dsw1")
    parser.add_argument("--dsw2")
    parser.add_argument("--no-pictures", action="store_true")
    parser.add_argument("--accuracy", choices=("mame", "board"), default="mame")
    args = parser.parse_args()
    scripts = args.inputs if args.inputs is not None else sorted(glob.glob(os.path.join(ROOT, "tests", "controls", "*.txt")))
    runs = [("attract", None)] if args.inputs is None else []
    runs += [(os.path.splitext(os.path.basename(path))[0], path) for path in scripts]
    ok = True
    for name, path in runs:
        frames = args.frames
        if not frames:
            frames = 1200
            if path:
                last = max((int(row[0]) for row in lines(path) if row and row[0].isdigit()), default=0)
                frames = last + 600
        ok &= run(name, frames, path, (args.dsw1, args.dsw2), not args.no_pictures, args.accuracy)
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
