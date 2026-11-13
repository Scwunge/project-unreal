"""Makes new, ungenerated reference saves with a chosen seed, from an existing save's settings.

    python -I tools/vanilla_seed_saves.py <saves folder> <template save> <seed> <new save>...

Copies level.dat and the world generation settings, with the seed changed, and nothing else: no chunks, no player, no
other saved state. Refuses to touch a save that already exists.
"""
import gzip
import pathlib
import shutil
import struct
import sys


def main():
    if len(sys.argv) < 5:
        print(__doc__)
        return 2
    saves = pathlib.Path(sys.argv[1])
    template = saves / sys.argv[2]
    seed = int(sys.argv[3])
    data = gzip.decompress((template / "data/minecraft/world_gen_settings.dat").read_bytes())
    marker = b"\x04\x00\x04seed"
    at = data.find(marker)
    if at < 0 or data.find(marker, at + 1) >= 0:
        print("no single seed tag in the template's settings")
        return 1
    at += len(marker)
    print("template seed", struct.unpack(">q", data[at:at + 8])[0])
    data = data[:at] + struct.pack(">q", seed) + data[at + 8:]
    for name in sys.argv[4:]:
        save = saves / name
        if save.exists():
            print("already there:", save)
            return 1
        (save / "data/minecraft").mkdir(parents=True)
        shutil.copy(template / "level.dat", save / "level.dat")
        (save / "data/minecraft/world_gen_settings.dat").write_bytes(gzip.compress(data))
        print("made", save)
    return 0


if __name__ == "__main__":
    sys.exit(main())
