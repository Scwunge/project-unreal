#!/usr/bin/env python3
"""Sums up one harness run: tools/perf_report.py Saved/Perf/<name>. Exit 1 if the run crashed or hung."""
import csv
import os
import re
import sys

d = sys.argv[1]
log = os.path.join(d, "perf.log")
if not os.path.exists(log):
    print("no perf.log in", d)
    for name in ("runner.txt", "end.txt"):
        p = os.path.join(d, name)
        if os.path.exists(p):
            print(name + ":", open(p, encoding="utf-8", errors="replace").read())
    sys.exit(1)
lines = open(log, encoding="utf-8", errors="replace").read().splitlines()


def kv(line):
    # "ents" is both a count and, later in the line, a scope's time.
    out = {}
    for a, b in re.findall(r"(\w+)=([^\s]+)", line):
        out["ents_ms" if a == "ents" and a in out else a] = b
    return out


print("--- phases")
print(f"{'phase':<14}{'secs':>7}{'fps':>7}{'p50':>7}{'p99':>7}{'1%low':>7}{'.1%low':>7}{'max':>8}{'>33':>6}{'>50':>6}{'>100':>6}{'>250':>6}")
for l in lines:
    if l.startswith("P "):
        k = kv(l)
        print(f"{k['phase']:<14}{k['secs']:>7}{k['fps']:>7}{k['p50']:>7}{k['p99']:>7}{k['low1']:>7}{k['low01']:>7}{k['max']:>8}{k['over33']:>6}{k['over50']:>6}{k['over100']:>6}{k['over250']:>6}")

print("--- mean per-frame ms by phase")
sec = [kv(l) for l in lines if l.startswith("S ")]
fields = ["gt", "rt", "rhi", "gpu", "stream", "sadd", "sdeco", "sunload", "sim", "ents_ms", "launch", "upload", "resort", "texanim", "hud", "pawn", "entrender", "sky", "world", "render", "models", "blockents", "particles", "blocklight", "ctrl", "audio", "misc", "mentity", "mplayer", "mhand"]
print(f"{'phase':<14}" + "".join(f"{f[:7]:>8}" for f in fields))
phases = []
for s in sec:
    if s["phase"] not in phases:
        phases.append(s["phase"])
for p in phases:
    rows = [s for s in sec if s["phase"] == p]
    print(f"{p:<14}" + "".join(f"{sum(float(r.get(f, 0)) * int(r['n']) for r in rows) / max(1, sum(int(r['n']) for r in rows)):>8.1f}" for f in fields))

print("--- events")
for l in lines:
    if l.startswith("E ") and " GC " not in l and "CHAT" not in l:
        print(l[2:][:170])

if sec:
    print("--- memory and world")
    print(f"peak rss {max(int(s['peak']) for s in sec)} MB, commit max {max(int(s['commit']) for s in sec)} MB, chunks max {max(int(s['chunks']) for s in sec)}, "
          f"meshes max {max(int(s['meshes']) for s in sec)}, water max {max(int(s['water']) for s in sec)}, ktris uploaded {max(int(s['ktris']) for s in sec)}, ents max {max(int(s['ents']) for s in sec)}")
    # When the view filled: the first second after the player came in with nothing left to mesh.
    for s in sec:
        if int(s["chunks"]) > 0 and int(s["dirty"]) == 0 and int(s["flight"]) == 0:
            print(f"first second with nothing left to mesh: t={s['t']} ({s['chunks']} chunks, {s['meshes']} meshes)")
            break
gc = [float(m.group(1)) for l in lines for m in [re.search(r" GC ([\d.]+) ms", l)] if m]
if gc:
    print(f"GC: {len(gc)} runs, mean {sum(gc) / len(gc):.1f} ms, worst {max(gc):.1f} ms")

hitches = [kv(l) for l in lines if l.startswith("H ")]
if hitches:
    print(f"--- {len(hitches)} frames over 33 ms; the worst 12")
    for h in sorted(hitches, key=lambda h: -float(h["ms"]))[:12]:
        print("  " + " ".join(f"{a}={b}" for a, b in h.items()))
    # What the time over budget went on, added over all those frames.
    totals = {}
    for h in hitches:
        for a, b in h.items():
            if a not in ("t", "ms", "dirty", "flight"):
                totals[a] = totals.get(a, 0.0) + float(b)
    print("  total ms in those frames: " + ", ".join(f"{a} {b:.0f}" for a, b in sorted(totals.items(), key=lambda x: -x[1])))

samples = os.path.join(d, "samples.csv")
if os.path.exists(samples):
    rows = list(csv.DictReader(open(samples, encoding="utf-8-sig")))
    good = [r for r in rows if all((r.get(k) or "").isdigit() for k in ("free_mb", "cpu", "vram_mb", "gpu_pct", "game_ws_mb", "game_private_mb"))]
    if good:
        print("--- outside view")
        print(f"VRAM used on the card: start {good[0]['vram_mb']} MB, peak {max(int(r['vram_mb']) for r in good)} MB (all processes); "
              f"lowest free RAM {min(int(r['free_mb']) for r in good)} MB; peak game ws {max(int(r['game_ws_mb']) for r in good)} MB, private {max(int(r['game_private_mb']) for r in good)} MB; "
              f"mean cpu {sum(int(r['cpu'] or 0) for r in good) / len(good):.0f}%, mean gpu {sum(int(r['gpu_pct']) for r in good) / len(good):.0f}%")
for name in ("machine.txt", "end.txt"):
    p = os.path.join(d, name)
    if os.path.exists(p):
        print(f"--- {name}")
        print(open(p, encoding="utf-8", errors="replace").read().strip()[:1200])

bad = [l for l in lines if l.startswith("X ")]
clean = any("CLEAN_EXIT" in l for l in lines)
if bad:
    print("--- exceptions and stalls (tools/perf_symbols.ps1 names the frames)")
    for l in bad[:80]:
        print(l)
print("RESULT:", "clean exit" if clean else "NO CLEAN EXIT: crashed, hung or was stopped")
sys.exit(0 if clean and not any(" EXCEPTION " in l for l in bad) else 1)
