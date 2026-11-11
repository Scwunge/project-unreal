"""Compares the blocks of two saved worlds, or two -McChunkDump files, chunk by chunk.

    python -I tools/vanilla_worlds_cmp.py <saveA> <saveB>            two saves (their overworld region files)
    python -I tools/vanilla_worlds_cmp.py <a.bin> <b.bin> [blocks.json]  two dumps, named through the block report

For saves, only chunks finished in both are compared. Prints how many chunks and blocks differ, what the differing blocks
are, how they fall by height, and how far they are from a chunk's edge (the report prints what an even spread would give).
"""
import collections
import json
import pathlib
import struct
import sys
import zlib


def read_tag(data, at, kind):
    if kind == 1:
        return struct.unpack_from(">b", data, at)[0], at + 1
    if kind == 2:
        return struct.unpack_from(">h", data, at)[0], at + 2
    if kind == 3:
        return struct.unpack_from(">i", data, at)[0], at + 4
    if kind == 4:
        return struct.unpack_from(">q", data, at)[0], at + 8
    if kind == 5:
        return None, at + 4
    if kind == 6:
        return None, at + 8
    if kind == 7:
        size = struct.unpack_from(">i", data, at)[0]
        return None, at + 4 + size
    if kind == 8:
        size = struct.unpack_from(">H", data, at)[0]
        return data[at + 2:at + 2 + size].decode("utf-8", "replace"), at + 2 + size
    if kind == 9:
        inner = data[at]
        size = struct.unpack_from(">i", data, at + 1)[0]
        at += 5
        out = []
        for _ in range(size):
            value, at = read_tag(data, at, inner)
            out.append(value)
        return out, at
    if kind == 10:
        out = {}
        while True:
            inner = data[at]
            at += 1
            if inner == 0:
                return out, at
            size = struct.unpack_from(">H", data, at)[0]
            name = data[at + 2:at + 2 + size].decode("utf-8", "replace")
            at += 2 + size
            out[name], at = read_tag(data, at, inner)
    if kind == 11:
        size = struct.unpack_from(">i", data, at)[0]
        return None, at + 4 + size * 4
    if kind == 12:
        size = struct.unpack_from(">i", data, at)[0]
        return data[at + 4:at + 4 + size * 8], at + 4 + size * 8
    raise ValueError(kind)


def read_save(save):
    chunks = {}
    for path in sorted((pathlib.Path(save) / "dimensions/minecraft/overworld/region").glob("r.*.mca")):
        raw = path.read_bytes()
        for index in range(1024):
            where = struct.unpack_from(">I", raw, index * 4)[0]
            if not where:
                continue
            start = (where >> 8) * 4096
            size, method = struct.unpack_from(">IB", raw, start)
            if method != 2:
                continue
            data = zlib.decompress(raw[start + 5:start + 4 + size])
            root, _ = read_tag(data, 3, 10)
            if root.get("Status") != "minecraft:full":
                continue
            chunks[(root["xPos"], root["zPos"])] = root["sections"]
    return chunks


def name_of(entry):
    # A plain name, a compound holding it under an empty key, or id and properties.
    if isinstance(entry, str):
        return entry.replace("minecraft:", "")
    props = entry.get("properties")
    name = (entry["id"] if "id" in entry else entry[""]).replace("minecraft:", "")
    return name + ("[" + ",".join(f"{k}={v}" for k, v in sorted(props.items())) + "]" if props else "")


def cells(section):
    states = section.get("block_states")
    if states is None:
        return ["air"] * 4096
    palette = [name_of(entry) for entry in states["palette"]]
    packed = states.get("data")
    if packed is None:
        return [palette[0]] * 4096
    bits = max(4, len(bin(len(palette) - 1)) - 2)
    per_long = 64 // bits
    mask = (1 << bits) - 1
    out = []
    for at in range(0, len(packed), 8):
        word = int.from_bytes(packed[at:at + 8], "big")
        for _ in range(per_long):
            if len(out) == 4096:
                break
            out.append(palette[word & mask])
            word >>= bits
    return out


class Tally:
    def __init__(self):
        self.chunks = 0
        self.blocks = 0
        self.total = 0
        self.names = collections.Counter()
        self.pairs = collections.Counter()
        self.heights = collections.Counter()
        self.edges = collections.Counter()
        self.per_chunk = []

    def add(self, x, y, z, first, second):
        self.blocks += 1
        base = (lambda text: text.split("[")[0])
        self.names[base(first)] += 1
        self.names[base(second)] += 1
        self.pairs[tuple(sorted((base(first), base(second))))] += 1
        self.heights[y // 32 * 32] += 1
        self.edges[min(x, 15 - x, z, 15 - z)] += 1

    def report(self, number):
        self.per_chunk.sort()
        count = len(self.per_chunk)
        print(f"{number} chunks compared, {self.total} blocks")
        print(f"{self.chunks} chunks differ ({100.0 * self.chunks / max(number, 1):.1f}%), {self.blocks} blocks ({100.0 * self.blocks / max(self.total, 1):.4f}%)")
        if count:
            print("in a differing chunk: median", self.per_chunk[count // 2], "p90", self.per_chunk[count * 9 // 10], "max", self.per_chunk[-1])
            print("blocks:", self.names.most_common(14))
            print("pairs:", self.pairs.most_common(10))
            print("by height:", sorted(self.heights.items()))
            print("by distance from a chunk edge:", " ".join(f"{k}:{100.0 * v / self.blocks:.0f}%" for k, v in sorted(self.edges.items())))
            print("  (even spread would be 0:23% 1:20% 2:17% 3:14% 4:11% 5:8% 6:5% 7:2%)")


def compare_saves(a, b):
    first, second = read_save(a), read_save(b)
    shared = first.keys() & second.keys()
    print(f"{a}: {len(first)} finished chunks; {b}: {len(second)}")
    tally = Tally()
    for pos in sorted(shared):
        here = 0
        by_y = {s["Y"]: s for s in second[pos]}
        for section in first[pos]:
            other = by_y.get(section["Y"])
            if other is None or "block_states" not in section:
                continue
            tally.total += 4096
            if section["block_states"] == other.get("block_states"):
                continue
            one, two = cells(section), cells(other)
            if one == two:
                continue
            for index in range(4096):
                if one[index] != two[index]:
                    here += 1
                    tally.add(index & 15, section["Y"] * 16 + (index >> 8), (index >> 4) & 15, one[index], two[index])
        if here:
            tally.chunks += 1
            tally.per_chunk.append(here)
    tally.report(len(shared))


def read_dump(path):
    out = {}
    with open(path, "rb") as dump:
        while header := dump.read(16):
            x, z, count, size = struct.unpack("<iiii", header)
            out[(x, z)] = (count, size, dump.read(count * size))
    return out


def compare_dumps(a, b, report):
    names = {}
    if report:
        for name, block in json.load(open(report, encoding="utf-8")).items():
            for state in block.get("states", []):
                names[state["id"]] = name.replace("minecraft:", "")
    first, second = read_dump(a), read_dump(b)
    tally = Tally()
    shared = first.keys() & second.keys()
    for pos in sorted(shared):
        count, size, one = first[pos]
        two = second[pos][2]
        tally.total += count
        if one == two:
            continue
        here = 0
        for index in range(count):
            p, q = one[index * size:(index + 1) * size], two[index * size:(index + 1) * size]
            if p != q:
                here += 1
                u, v = int.from_bytes(p, "little"), int.from_bytes(q, "little")
                tally.add(index & 15, index // 256 - 64, (index >> 4) & 15, str(names.get(u, u)), str(names.get(v, v)))
        tally.chunks += 1
        tally.per_chunk.append(here)
    tally.report(len(shared))


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        return 2
    if pathlib.Path(sys.argv[1]).is_dir():
        compare_saves(sys.argv[1], sys.argv[2])
    else:
        compare_dumps(sys.argv[1], sys.argv[2], sys.argv[3] if len(sys.argv) > 3 else None)
    return 0


if __name__ == "__main__":
    sys.exit(main())
