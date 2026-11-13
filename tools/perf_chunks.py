"""Compares what chunks came to in two or more runs of a seed.

    python -I tools/perf_chunks.py Saved/Perf/runA Saved/Perf/runB ...

Reads each run's chunks.bin (-McChunkDump) if it has one, which also counts the blocks that differ, otherwise the CHUNK
x,z crc entities lines (-McChunkHash) in perf.log, which only says which chunks do. Compares every run against the first.
Exit 0 when nothing differs.
"""
import pathlib
import re
import struct
import sys

LINE = re.compile(r"CHUNK (-?\d+),(-?\d+) ([0-9a-f]{8}) (\d+)")


def read_hashes(folder):
    out = {}
    with open(pathlib.Path(folder) / "perf.log", encoding="utf-8", errors="replace") as log:
        for line in log:
            match = LINE.search(line)
            if match:
                out[(int(match[1]), int(match[2]))] = (match[3], int(match[4]))
    return out


def read_blocks(folder):
    path = pathlib.Path(folder) / "chunks.bin"
    if not path.exists():
        return None
    out = {}
    with open(path, "rb") as dump:
        while header := dump.read(16):
            x, z, count, size = struct.unpack("<iiii", header)
            out[(x, z)] = (size, dump.read(count * size))
    return out


def count_blocks(a, b):
    size, first = a
    second = b[1]
    return sum(first[at:at + size] != second[at:at + size] for at in range(0, len(first), size))


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        return 2
    first_hashes = read_hashes(sys.argv[1])
    first_blocks = read_blocks(sys.argv[1])
    print(f"{sys.argv[1]}: {len(first_hashes)} chunks")
    bad = 0
    for name in sys.argv[2:]:
        hashes = read_hashes(name)
        blocks = read_blocks(name)
        shared = first_hashes.keys() & hashes.keys()
        differ = sorted(pos for pos in shared if first_hashes[pos] != hashes[pos])
        text = f"{name}: {len(hashes)} chunks, {len(shared)} shared, {len(differ)} differ ({100.0 * len(differ) / max(len(shared), 1):.1f}%)"
        if first_blocks is not None and blocks is not None:
            counts = [count_blocks(first_blocks[pos], blocks[pos]) for pos in differ]
            total = sum(len(first_blocks[pos][1]) // first_blocks[pos][0] for pos in shared)
            text += f", {sum(counts)} of {total} blocks differ, most in a chunk {max(counts, default=0)}"
        print(text)
        bad += len(differ)
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
