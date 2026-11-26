#!/bin/sh
# Name:        tools/linux/make_release.sh
# Description: the player's package for Linux: release/Makaimura-<version>-linux-x86_64.tar.gz, for a GitHub Release
# Version:     0.1.0
# Created:     26-11-2026
#
# license:BSD-3-Clause
# Run in tools/linux/Dockerfile's machine with the repository at /work, by tools/make_release.ps1 -Linux (which
# gives the version and the program: readable or translated, as for Windows). The readable C (src/) or the
# translation (build/generated/) must be there already: they are written from the ROM set on the host.
#
# It builds the game in build/linux (its programs in build/bin-linux), then puts in the package only the files
# named below: makaimura (its library path $ORIGIN: the SDL2 beside it first), libSDL2-2.0.so.0, README.txt,
# CHANGES.txt and the licenses. Then the checks, as make_release.ps1's: nothing but those files, no ROM set or
# saves, a 64-bit x86-64 program that needs nothing outside the package but the system's own libraries, the
# program asked for, none of the ROM's files' starts in it; and the package run from an empty folder with the ROM
# set (no display, no sound device): the attract mode recorded into an MP4 through ffmpeg, which must hold its
# frames. Any problem stops it before the .tar.gz is made.
set -eu

VERSION="$1"
PROGRAM="${2:-readable}"
cd /work

NAME="Makaimura-$VERSION-linux-x86_64"
OUT="release/$NAME"
TARBALL="release/$NAME.tar.gz"
BIN="build/bin-linux"

# ---- the build ----
cmake -S . -B build/linux -DCMAKE_BUILD_TYPE=Release -DSDL2_DIR=/opt/sdl2/lib/cmake/SDL2 \
    -DCMAKE_BUILD_WITH_INSTALL_RPATH=ON '-DCMAKE_INSTALL_RPATH=$ORIGIN' > build/linux-configure.log
if [ "$PROGRAM" = "translated" ]; then target=makaimura_translated; else target=makaimura; fi
cmake --build build/linux --target "$target" -j"$(nproc)"

# ---- the folder, made fresh ----
rm -rf "$OUT" "$TARBALL"
mkdir -p "$OUT/licenses"
cp "$BIN/$target" "$OUT/makaimura"
strip "$OUT/makaimura"
cp -L /opt/sdl2/lib/libSDL2-2.0.so.0 "$OUT/libSDL2-2.0.so.0"
cp ports/sdl2/release/README.txt "$OUT/README.txt"
cp CHANGELOG.md "$OUT/CHANGES.txt"
cp LICENSE "$OUT/licenses/LICENSE.txt"
cp ports/sdl2/release/NOTICES.txt "$OUT/licenses/NOTICES.txt"
cp /opt/sdl2/LICENSE.txt "$OUT/licenses/SDL2.txt"
# (the text files with Unix line ends)
for text in README.txt CHANGES.txt licenses/LICENSE.txt licenses/NOTICES.txt licenses/SDL2.txt; do
    sed -i 's/\r$//' "$OUT/$text"
done
chmod 755 "$OUT/makaimura"
chmod 644 "$OUT/libSDL2-2.0.so.0" "$OUT"/*.txt "$OUT"/licenses/*.txt

# ---- the checks ----
problems=""
problem() { problems="$problems
  $1"; }
allowed="makaimura libSDL2-2.0.so.0 README.txt CHANGES.txt licenses/LICENSE.txt licenses/NOTICES.txt licenses/SDL2.txt"
for file in $(cd "$OUT" && find . -type f | sed 's|^\./||'); do
    case " $allowed " in *" $file "*) ;; *) problem "not on the list: $file" ;; esac
    case "$file" in
        *.zip|*.7z|*.dat|*.part|*.hi|*.state|*.inp|*.ini|*.bin|*.rom|*.regions|*.avi|*.mp4)
            problem "a ROM set, game data, save or settings file: $file" ;;
    esac
done
file "$OUT/makaimura" | grep -q "ELF 64-bit LSB.*x86-64" || problem "makaimura is not a 64-bit x86-64 program"
[ "$(stat -c %s "$OUT/makaimura")" -le 16777216 ] || problem "makaimura is more than 16 MB"
# (what it needs: the SDL2 beside it, the rest the system's own)
needed=$(cd "$OUT" && ldd ./makaimura)
echo "$needed" | grep -q "not found" && problem "makaimura needs libraries not found: $(echo "$needed" | grep 'not found')"
echo "$needed" | grep "libSDL2" | grep -q "/release/$NAME/\(\./\)\{0,1\}libSDL2-2.0.so.0" \
    || problem "makaimura does not take the package's libSDL2-2.0.so.0"
for lib in $(echo "$needed" | awk '/=>/ { print $1 }'); do
    case "$lib" in
        libSDL2-2.0.so.0|libc.so.6|libm.so.6|libdl.so.2|libpthread.so.0|librt.so.1|libgcc_s.so.1) ;;
        *) problem "makaimura needs $lib (not every system has it)" ;;
    esac
done
readable=0; translated=0
strings "$OUT/makaimura" | grep -q "the main loop returned" && readable=1
strings "$OUT/makaimura" | grep -q "an address the static analysis did not find" && translated=1
if [ "$PROGRAM" = "readable" ] && { [ $readable = 0 ] || [ $translated = 1 ]; }; then
    problem "makaimura is not the readable C (python tools/m6809/decompile.py, then build again)"
fi
if [ "$PROGRAM" = "translated" ] && { [ $translated = 0 ] || [ $readable = 1 ]; }; then
    problem "makaimura is not the translation (python tools/m6809/translate.py, then build again)"
fi
# (a stretch of the ROM set's graphics or sound program in the program would mean its data had got in: the first
# 256 bytes of each file, looked for)
held=$(python3 -I - "$OUT/makaimura" rom/makaimurg.zip rom/gng.zip <<'PYTHON'
import os, sys, zipfile
program = open(sys.argv[1], "rb").read()
for path in sys.argv[2:]:
    if not os.path.exists(path):
        continue
    with zipfile.ZipFile(path) as archive:
        for entry in archive.infolist():
            if entry.file_size >= 4096 and archive.read(entry)[:256] in program:
                print(f"{entry.filename} from {path}")
PYTHON
)
[ -z "$held" ] || problem "makaimura holds the start of $held"

# the package from an empty folder: the ROM set beside it, no display or sound device; the attract mode recorded
# as the video (the settings' default recording) through ffmpeg
trial=$(mktemp -d)
cp -r "$OUT/." "$trial/"
mkdir "$trial/rom"
cp rom/makaimurg.zip rom/gng.zip "$trial/rom/"
(cd "$trial" && SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy ./makaimura --hidden --frames 600 --record trial.mp4 \
    > trial.log 2>&1) || problem "the package's game did not run: $(tail -3 "$trial/trial.log")"
frames=$(ffprobe -v error -count_frames -select_streams v:0 -show_entries stream=nb_read_frames,width,height \
    -of csv=p=0 "$trial/trial.mp4" 2>/dev/null || true)
case "$frames" in
    2560,1440,59[0-9]|2560,1440,60[0-9]) echo "the package's recording: $frames (width, height, frames)" ;;
    *) problem "the package's recording is not the attract mode's 600 frames at 2560 x 1440: '$frames'" ;;
esac
rm -rf "$trial"

if [ -n "$problems" ]; then
    rm -rf "$OUT"
    echo "No package made:$problems" >&2
    exit 1
fi

tar --owner=0 --group=0 --numeric-owner -czf "$TARBALL" -C release "$NAME"
echo "Made $TARBALL"
(cd "$OUT" && find . -type f -printf "  %-28P %12s bytes\n" | sort)
