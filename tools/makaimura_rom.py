"""
Name:        makaimura_rom.py
Description: the ROM set of Makaimura (Capcom, 1985; MAME's makaimurg, a clone of gng): its files, and the
             6809 program's address space and banks built from them
Version:     0.1.0
Created:     19-11-2026

After MAME's src/mame/capcom/gng.cpp (BSD-3-Clause, Pierpaolo Prazzoli): ROM_START( makaimurg ), whose files
not its own come from its parent, ROM_START( gng ). MAME's sets are split: makaimurg.zip holds the five files
of its own, gng.zip the rest. A merged set (gng.zip with the clones' files in it) and a folder of files work as
well: files are known by their CRC32 and checked by their SHA-1.

The main CPU (MC6809, 1.5 MHz):
    0000-1DFF  work RAM          1E00-1FFF  sprite RAM (copied to the video's buffer by the DMA at 3C00)
    2000-27FF  text layer RAM    2800-2FFF  background layer RAM
    3000-3004  inputs, DIP switches       3800-39FF  palette          3A00  the sound latch
    3B08-3B0B  background scroll          3C00  sprite DMA            3D00-3D07  LS259 latch     3E00  bank
    4000-5FFF  banked ROM: banks 0-3 are mj05g's four 8 KB, bank 4 is mj04g's first 8 KB (3E00: 4 -> bank 4,
               anything else -> its low two bits)
    6000-7FFF  mj04g's second 8 KB            8000-FFFF  mj03g
"""
import hashlib
import io
import os
import zipfile
import zlib
from dataclasses import dataclass

SET = "makaimurg"
PARENT = "gng"
TITLE = "Makaimura (Japan revision G)"


@dataclass(frozen=True)
class RomFile:
    name: str
    size: int
    crc: int
    sha1: str
    region: str      # maincpu, audiocpu, chars, tiles, sprites, proms
    offset: int      # where in its region


# ROM_START( makaimurg ): the order is MAME's
FILES = (
    RomFile("mj04g.bin", 0x4000, 0x757C94D3, "07f7cf788810a1425016e016ce3579adb3253ac7", "maincpu", 0x04000),
    RomFile("mj03g.bin", 0x8000, 0x61B043BB, "23a0a17d0abc4b084ffeba90266ef455361771cc", "maincpu", 0x08000),
    RomFile("mj05g.bin", 0x8000, 0xF2FDCCF5, "7694a981a6196d77fd2279fc34042b4cfb40c054", "maincpu", 0x10000),
    RomFile("gg2.bin", 0x8000, 0x615F5B6F, "7ef9ec5c2072e21c787a6bbf700033f50c759c1d", "audiocpu", 0x00000),
    RomFile("gg1.bin", 0x4000, 0xECFCCF07, "0a1518e19a2e0a4cc3dde4b9568202ea911b5ece", "chars", 0x00000),
    RomFile("gg11.bin", 0x4000, 0xDDD56FA9, "f9d77eee5e2738b7e83ba02fcc55dd480391479f", "tiles", 0x00000),
    RomFile("gg10.bin", 0x4000, 0x7302529D, "8434c994cc55d2586641f3b90b6b15fd65dfb67c", "tiles", 0x04000),
    RomFile("gg9.bin", 0x4000, 0x20035BDA, "bbb1fba0eb19471f66d29526fa8423ccb047bd63", "tiles", 0x08000),
    RomFile("gg8.bin", 0x4000, 0xF12BA271, "1c42fa02cb27b35d10c3f7f036005e747f9f6b79", "tiles", 0x0C000),
    RomFile("gg7.bin", 0x4000, 0xE525207D, "1947f159189b3a53f1251d8653b6e7c65c91fc3c", "tiles", 0x10000),
    RomFile("gg6.bin", 0x4000, 0x2D77E9B2, "944da1ce29a18bf0fc8deff78bceacba0bf23a07", "tiles", 0x14000),
    RomFile("gng13.n4", 0x4000, 0x4613AFDC, "13e5a38a134bd7cfa16c63a18fa332c6d66b9345", "sprites", 0x00000),
    RomFile("gg16.bin", 0x4000, 0x06D7E5CA, "9e06012bcd82f98fad43de666ef9a75979d940ab", "sprites", 0x04000),
    RomFile("gg15.bin", 0x4000, 0xBC1FE02D, "e3a1421d465b87148ffa94f5673b2307f0246afe", "sprites", 0x08000),
    RomFile("gng16.l4", 0x4000, 0x608D68D5, "af207f9ee2f93a0cf9cf25cfe72b0fdfe55481b8", "sprites", 0x10000),
    RomFile("gg13.bin", 0x4000, 0xE80C3FCA, "cb641c25bb04b970b2cbeca41adb792bbe142fb5", "sprites", 0x14000),
    RomFile("gg12.bin", 0x4000, 0x7780A925, "3f129ca6d695548b659955fe538584bd9ac2ff17", "sprites", 0x18000),
    RomFile("tbp24s10.14k", 0x0100, 0x0EAF5158, "bafd4108708f66cd7b280e47152b108f3e254fc9", "proms", 0x0000),
    RomFile("63s141.2e", 0x0100, 0x4A1285A4, "5018c3950b675af58db499e2883ecbc55419b491", "proms", 0x0100),
)

REGIONS = {"maincpu": 0x18000, "audiocpu": 0x10000, "chars": 0x4000, "tiles": 0x18000, "sprites": 0x20000,
           "proms": 0x200}
REGION_FILL = {"sprites": 0xFF}    # (ROMREGION_ERASEFF)

PROGRAM_FILES = ("mj04g.bin", "mj03g.bin", "mj05g.bin")
BANKS = 5
BANK_BASE, BANK_SIZE = 0x4000, 0x2000


class MissingFiles(Exception):
    def __init__(self, missing):
        self.missing = missing
        super().__init__("missing from the ROM set: " + ", ".join(f.name for f in missing))


def _candidates(path):
    """Every file found at 'path' (a .zip, or a folder of files and .zips), as (name, data)."""
    if os.path.isdir(path):
        for entry in sorted(os.listdir(path)):
            full = os.path.join(path, entry)
            if entry.lower().endswith(".zip"):
                yield from _candidates(full)
            elif os.path.isfile(full) and os.path.getsize(full) <= 0x100000:
                with open(full, "rb") as handle:
                    yield entry, handle.read()
        return
    with zipfile.ZipFile(path) as archive:
        for info in archive.infolist():
            if not info.is_dir() and info.file_size <= 0x100000:
                yield info.filename, archive.read(info)


def find_files(paths, needed=FILES):
    """The set's files from 'paths' (zips or folders), by CRC32 and size; each checked by its SHA-1."""
    wanted = {(f.crc, f.size): f for f in needed}
    found = {}
    for path in paths:
        if not os.path.exists(path):
            continue
        for name, data in _candidates(path):
            key = (zlib.crc32(data) & 0xFFFFFFFF, len(data))
            rom = wanted.get(key)
            if rom and rom.name not in found and hashlib.sha1(data).hexdigest() == rom.sha1:
                found[rom.name] = data
    missing = [f for f in needed if f.name not in found]
    return found, missing


def default_paths(root):
    """Where the set is looked for: rom/ (makaimurg.zip and gng.zip, or loose files)."""
    rom = os.path.join(root, "rom")
    return [os.path.join(rom, f"{SET}.zip"), os.path.join(rom, f"{PARENT}.zip"), rom]


def load_regions(paths, needed=FILES):
    found, missing = find_files(paths, needed)
    if missing:
        raise MissingFiles(missing)
    regions = {}
    for rom in needed:
        region = regions.setdefault(rom.region, bytearray([REGION_FILL.get(rom.region, 0)]) * REGIONS[rom.region])
        region[rom.offset:rom.offset + rom.size] = found[rom.name]
    return {name: bytes(data) for name, data in regions.items()}


@dataclass
class Program:
    """The 6809's view of its ROM: the fixed 6000-FFFF, and the five banks of 4000-5FFF."""
    region: bytes    # MAME's "maincpu" region, 0x18000 bytes

    def fixed(self, address):
        assert 0x6000 <= address <= 0xFFFF
        return self.region[address]

    def bank(self, number, address):
        assert BANK_BASE <= address < BANK_BASE + BANK_SIZE and 0 <= number < BANKS
        if number == 4:
            return self.region[address]    # (mj04g's first half, loaded at 4000)
        return self.region[0x10000 + number * BANK_SIZE + address - BANK_BASE]

    def reader(self, bank=None):
        """A read function for the disassembler: ROM at 4000-FFFF (4000-5FFF in 'bank', or 0xFF when unknown),
        and 0 elsewhere (RAM, I/O: not code)."""
        def read(address):
            if address >= 0x6000:
                return self.region[address]
            if address >= BANK_BASE and bank is not None:
                return self.bank(bank, address)
            return 0xFF
        return read


def load_program(paths):
    needed = tuple(f for f in FILES if f.name in PROGRAM_FILES)
    return Program(load_regions(paths, needed)["maincpu"])


def write_regions(path, regions):
    """The regions as the reference and the headless tools read them (build/makaimurg.regions, never committed):
    "MKRG", then each region's name (16 bytes, NUL padded), size (4 bytes, little endian) and bytes."""
    with open(path, "wb") as out:
        out.write(b"MKRG")
        for name, data in regions.items():
            out.write(name.encode().ljust(16, bytes(1)))
            out.write(len(data).to_bytes(4, "little"))
            out.write(data)


if __name__ == "__main__":
    import sys
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    if len(sys.argv) > 2 and sys.argv[1] == "--extract":
        paths = sys.argv[3:] or default_paths(root)
        found, missing = find_files(paths)
        have = {f.region for f in FILES} - {f.region for f in missing}
        regions = load_regions(paths, tuple(f for f in FILES if f.region in have))
        write_regions(sys.argv[2], regions)
        print(f"{sys.argv[2]}: " + ", ".join(regions) + (f" (missing: {', '.join(sorted({f.region for f in missing}))})"
                                                        if missing else ""))
        sys.exit(0)
    paths = sys.argv[1:] or default_paths(root)
    found, missing = find_files(paths)
    for rom in FILES:
        print(f"  {'ok     ' if rom.name in found else 'MISSING'}  {rom.name:14s} {rom.region:9s} "
              f"{rom.crc:08x}")
    print(f"{len(found)} of {len(FILES)} files" + (" (the program's are all there)" if all(
        n in found for n in PROGRAM_FILES) else ""))
    sys.exit(1 if missing else 0)
