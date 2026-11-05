"""Compares what chunks came to in two or more -McChunkHash runs of a seed.

    python -I tools/perf_chunks.py Saved/Perf/runA Saved/Perf/runB ...

Each run's perf.log has a CHUNK x,z crc entities line per new chunk. Prints, against the first run, how many chunks both
have and how many of those differ, and lists the first few. Exit 0 when none differ.
"""
import pathlib
import re
import sys

LINE = re.compile(r"CHUNK (-?\d+),(-?\d+) ([0-9a-f]{8}) (\d+)")


def read(folder):
    out = {}
    with open(pathlib.Path(folder) / "perf.log", encoding="utf-8", errors="replace") as log:
        for line in log:
            match = LINE.search(line)
            if match:
                out[(int(match[1]), int(match[2]))] = (match[3], int(match[4]))
    return out


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        return 2
    first = read(sys.argv[1])
    print(f"{sys.argv[1]}: {len(first)} chunks")
    bad = 0
    for other_name in sys.argv[2:]:
        other = read(other_name)
        shared = first.keys() & other.keys()
        differ = sorted(pos for pos in shared if first[pos] != other[pos])
        print(f"{other_name}: {len(other)} chunks, {len(shared)} shared, {len(differ)} differ ({100.0 * len(differ) / max(len(shared), 1):.1f}%)")
        if differ:
            print("  first:", differ[:12])
        bad += len(differ)
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
