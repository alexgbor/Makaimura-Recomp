"""A recording of the game (makaimura --record, or F12) made ready for YouTube or any site: the board's 256 x 224
picture enlarged with sharp pixels (6 times, nearest: 1536 x 1344) and then to the arcade monitor's 4:3 at 1440
lines (1920 x 1440), encoded with ffmpeg (x264 at quality 16, AAC at 192 kb/s) at the board's own 59.637 frames a
second. The recording itself is uncompressed (about 10 MB a second); this makes it some tens of times smaller.

Why 1440 lines: YouTube names a video's quality by its height and gives each one its bitrate, so the board's 224
lines are shown as 240p, blurred; at 1440p it encodes it with VP9 or AV1 and many more bits, and the pixels stay
sharp even when it is watched at 1080p or less.

    python tools/youtube_export.py IN.avi [OUT.mp4] [--height 1440|1080|2160] [--ffmpeg PATH]

(OUT defaults to IN's name with "_youtube.mp4"). The same, by hand:

    ffmpeg -i IN.avi -vf "scale=1536:1344:flags=neighbor,scale=1920:1440:flags=bilinear,setsar=1"
           -c:v libx264 -crf 16 -preset slow -pix_fmt yuv420p -c:a aac -b:a 192k -movflags +faststart OUT.mp4"""
import argparse
import os
import shutil
import subprocess
import sys

# The height asked for: the whole enlargement (nearest) under it, then to 4:3 at that height.
SIZES = {1080: (5, 1440, 1080), 1440: (6, 1920, 1440), 2160: (9, 2880, 2160)}


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("input")
    parser.add_argument("out", nargs="?")
    parser.add_argument("--height", type=int, choices=sorted(SIZES), default=1440)
    parser.add_argument("--ffmpeg", default=os.environ.get("FFMPEG") or shutil.which("ffmpeg"))
    args = parser.parse_args()
    if not args.ffmpeg:
        sys.exit("youtube_export: ffmpeg not found (--ffmpeg PATH, or the FFMPEG environment variable)")
    out = args.out or os.path.splitext(args.input)[0] + "_youtube.mp4"
    times, width, height = SIZES[args.height]
    picture = (f"scale={256 * times}:{224 * times}:flags=neighbor,"
               f"scale={width}:{height}:flags=bilinear,setsar=1")
    command = [args.ffmpeg, "-y", "-i", args.input, "-vf", picture,
               "-c:v", "libx264", "-crf", "16", "-preset", "slow", "-pix_fmt", "yuv420p",
               "-c:a", "aac", "-b:a", "192k", "-movflags", "+faststart", out]
    sys.exit(subprocess.call(command))


if __name__ == "__main__":
    main()
