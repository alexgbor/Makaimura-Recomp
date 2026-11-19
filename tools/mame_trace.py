"""
Name:        mame_trace.py
Description: MAME's own run of makaimurg, frame by frame (tools/mame/trace.lua), compared with the reference's
             (build/bin/gref --bus-hashes): the board model and its timing checked against all of MAME
Version:     0.1.0
Created:     19-11-2026

    python tools/mame_trace.py [--frames N] [--inputs FILE] [--mame PATH] [--dumps] [--pictures N]
                               [--dsw1 HEX] [--dsw2 HEX]

MAME is not part of the repository: the default is D:/repositories/mame0289/mame.exe (MAME 0.289, the version
tools/reference/mame comes from), or the MAME environment variable. MAME runs without a window, sound or
throttle. Both runs' hashes are of the 6809's bus 0000-2FFF at each frame's VBLANK; with --dumps, the first
frame that differs is compared byte by byte.
"""
import argparse
import os
import shutil
import subprocess
import sys

TOOLS = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(TOOLS)
EXE = ".exe" if os.name == "nt" else ""
DEFAULT_MAME = os.environ.get("MAME", "D:/repositories/mame0289/mame" + EXE)
BUS = 0x3000


def run_mame(mame, frames, inputs, out, dumps, pictures=None, every=0, dsw=(None, None)):
    env = dict(os.environ, MK_OUT=out, MK_FRAMES=str(frames))
    for name, value in zip(("MK_DSW1", "MK_DSW2"), dsw):
        if value:
            env[name] = value
    if pictures:
        env["MK_PICTURES"], env["MK_PICTURES_EVERY"] = pictures, str(every)
    if inputs:
        env["MK_INPUTS"] = os.path.abspath(inputs)
    if dumps:
        env["MK_DUMPS"] = dumps
    command = [mame, "makaimurg", "-rompath", os.path.join(ROOT, "rom"), "-autoboot_script",
               os.path.join(TOOLS, "mame", "trace.lua"), "-video", "none", "-sound", "none", "-nothrottle", "-seconds_to_run", str(frames // 50 + 30),
               "-skip_gameinfo", "-nonvram_save", "-noreadconfig", "-cfg_directory",
               os.path.join(ROOT, "build", "mame_cfg"), "-nvram_directory", os.path.join(ROOT, "build", "mame_nvram")]
    # MAME writes the game's settings (the DIP switches among them) to its cfg directory when it exits and reads
    # them at the next start, -noreadconfig or not: every run starts from an empty one, MAME's defaults
    shutil.rmtree(os.path.join(ROOT, "build", "mame_cfg"), ignore_errors=True)
    subprocess.run(command, env=env, check=True, cwd=os.path.join(ROOT, "build"))


def run_gref(frames, inputs, out, dumps, pictures=None, every=0, dsw=(None, None)):
    command = [os.path.join(ROOT, "build", "bin", "gref" + EXE), "--frames", str(frames), "--bus-hashes", out]
    for name, value in zip(("--dsw1", "--dsw2"), dsw):
        if value:
            command += [name, value]
    if pictures:
        command += ["--pictures", str(every), pictures]
    if inputs:
        command += ["--inputs", inputs]
    if dumps:
        command += ["--bus-dumps", dumps]
    subprocess.run(command, check=True, cwd=ROOT, stdout=subprocess.DEVNULL)


def read(path):
    rows = []
    with open(path) as handle:
        for line in handle:
            parts = line.split()
            if len(parts) >= 2:
                rows.append((int(parts[0]), parts[1], parts[2] if len(parts) > 2 else ""))
    return rows


WIDTH, HEIGHT = 256, 224


def compare_pictures(mame_path, gref_path, every):
    """MAME's pictures (32-bit ARGB, little endian: B G R A) against gref's (R G B), frame by frame."""
    with open(mame_path, "rb") as handle:
        mame = handle.read()
    with open(gref_path, "rb") as handle:
        gref = handle.read()
    size_mame, size_gref = WIDTH * HEIGHT * 4, WIDTH * HEIGHT * 3
    count = min(len(mame) // size_mame, len(gref) // size_gref)
    same = 0
    first = None
    for index in range(count):
        a = mame[index * size_mame:(index + 1) * size_mame]
        b = gref[index * size_gref:(index + 1) * size_gref]
        converted = bytearray(size_gref)
        converted[0::3], converted[1::3], converted[2::3] = a[2::4], a[1::4], a[0::4]
        if converted == b:
            same += 1
        elif first is None:
            differ = [i // 3 for i in range(0, size_gref, 3) if converted[i:i + 3] != b[i:i + 3]]
            first = (index * every, len(differ), differ[:8], converted, b)
    print(f"pictures: {count} compared (every {every} frames), {same} the same as MAME's")
    if first:
        frame, number, pixels, converted, b = first
        print(f"  first difference at frame {frame}: {number} pixels, e.g. " +
              ", ".join(f"({p % WIDTH},{p // WIDTH}) MAME {converted[3 * p:3 * p + 3].hex()} gref {b[3 * p:3 * p + 3].hex()}"
                        for p in pixels[:4]))
    return first is None


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[2])
    parser.add_argument("--frames", type=int, default=1200)
    parser.add_argument("--inputs")
    parser.add_argument("--mame", default=DEFAULT_MAME)
    parser.add_argument("--dumps", action="store_true", help="compare the first differing frame byte by byte")
    parser.add_argument("--pictures", type=int, default=0, metavar="N",
                        help="compare every Nth frame's picture with MAME's screen, pixel by pixel")
    parser.add_argument("--dsw1", help="DIP switches 1 from power-on, hex as the board reads them (default DF)")
    parser.add_argument("--dsw2", help="DIP switches 2 (default FB)")
    args = parser.parse_args()
    dsw = (args.dsw1, args.dsw2)

    build = os.path.join(ROOT, "build")
    mame_out, gref_out = os.path.join(build, "mame_frames.txt"), os.path.join(build, "gref_frames.txt")
    mame_dumps = os.path.join(build, "mame_bus.bin") if args.dumps else None
    gref_dumps = os.path.join(build, "gref_bus.bin") if args.dumps else None
    mame_pictures = os.path.join(build, "mame_pictures.bin") if args.pictures else None
    gref_pictures = os.path.join(build, "gref_pictures.bin") if args.pictures else None
    run_mame(args.mame, args.frames, args.inputs, mame_out, mame_dumps, mame_pictures, args.pictures, dsw)
    run_gref(args.frames, args.inputs, gref_out, gref_dumps, gref_pictures, args.pictures, dsw)
    pictures_ok = True
    if args.pictures:
        pictures_ok = compare_pictures(mame_pictures, gref_pictures, args.pictures)

    mame, gref = read(mame_out), read(gref_out)
    first = None
    same = 0
    for (frame, mame_hash, pc), (_, gref_hash, gref_pc) in zip(mame, gref):
        if mame_hash == gref_hash:
            same += 1
        elif first is None:
            first = frame
    print(f"{len(mame)} frames from MAME, {len(gref)} from gref: {same} the same")
    if first is None:
        print("every frame the same as MAME's")
        return 0 if pictures_ok else 1
    print(f"first difference at frame {first}: MAME {mame[first][1]} (PC {mame[first][2]}), "
          f"gref {gref[first][1]} (PC {gref[first][2]})")
    if args.dumps:
        with open(mame_dumps, "rb") as a, open(gref_dumps, "rb") as b:
            a.seek(first * BUS)
            b.seek(first * BUS)
            x, y = a.read(BUS), b.read(BUS)
        differ = [i for i in range(BUS) if x[i] != y[i]]
        print(f"{len(differ)} bytes differ; the first ones:")
        for i in differ[:24]:
            print(f"  {i:04X}  MAME {x[i]:02X}  gref {y[i]:02X}")
    return 1


if __name__ == "__main__":
    sys.exit(main())
