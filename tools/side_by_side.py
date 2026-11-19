"""Two recordings of the game (makaimura --record, or F12) side by side in one video, for comparisons: MAME mode on
the left and board mode on the right, say. Each picture is enlarged with sharp pixels (5 times, nearest) and then
to the arcade monitor's 4:3 (1280 x 960); the sound in stereo, the left recording's in the left channel and the
right one's in the right (--sound left or right: only that one's, in both channels). Encoded with ffmpeg (x264, at
quality 14, and AAC), at the board's own 59.637 frames a second; as long as the shorter one.

    python tools/side_by_side.py LEFT.avi RIGHT.avi OUT.mp4 [--sound both|left|right] [--ffmpeg PATH]

To make the same game in both modes, record its controls as you play and play them again in the other mode:

    makaimura --accuracy mame --record mame.avi --record-inputs game.txt
    makaimura --accuracy board --inputs game.txt --record board.avi --frames N

(the same inputs at the same frames: where board mode slows down, the game can part ways from there on)."""
import argparse
import os
import shutil
import subprocess
import sys


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("left")
    parser.add_argument("right")
    parser.add_argument("out")
    parser.add_argument("--sound", choices=("both", "left", "right"), default="both")
    parser.add_argument("--ffmpeg", default=os.environ.get("FFMPEG") or shutil.which("ffmpeg"))
    args = parser.parse_args()
    if not args.ffmpeg:
        sys.exit("side_by_side: ffmpeg not found (--ffmpeg PATH, or the FFMPEG environment variable)")
    picture = "scale=1280:1120:flags=neighbor,scale=1280:960:flags=bilinear,setsar=1"
    if args.sound == "both":
        sound = "[0:a][1:a]amerge=inputs=2[s]"
    else:
        index = 0 if args.sound == "left" else 1
        sound = f"[{index}:a]pan=stereo|c0=c0|c1=c0[s]"
    graph = f"[0:v]{picture}[l];[1:v]{picture}[r];[l][r]hstack=inputs=2[v];{sound}"
    command = [args.ffmpeg, "-y", "-i", args.left, "-i", args.right, "-filter_complex", graph,
               "-map", "[v]", "-map", "[s]", "-c:v", "libx264", "-crf", "14", "-preset", "medium",
               "-pix_fmt", "yuv420p", "-c:a", "aac", "-b:a", "192k", "-shortest", args.out]
    sys.exit(subprocess.call(command))


if __name__ == "__main__":
    main()
