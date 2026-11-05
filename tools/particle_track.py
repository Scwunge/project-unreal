# Tracks the kinds of particle in a stepped reference run (scripts/qb*.txt in E:\mc-reference): frames are one tick apart, a baseline shot
# ref_<batch>_base.png comes first, the kinds run along x (upper row y 222.6 and lower row 220.9, 1.2 blocks apart; the view looks south
# so the first kind is rightmost). Prints per kind and row, for each frame, pixel count, bounding box and centroid of what differs
# from the baseline, then a one-line summary (ticks visible, start and end width, rise in px). Usage: particle_track.py <batch> <ntypes> [type index]
import sys, glob
import numpy as np
from PIL import Image

batch, n = sys.argv[1], int(sys.argv[2])
only = int(sys.argv[3]) if len(sys.argv) > 3 else None
verbose = len(sys.argv) > 4
base = np.array(Image.open(f'E:/mc-reference/run/screenshots/ref_{batch}_base.png').convert('RGB')).astype(int)
N = len(glob.glob(f'E:/mc-reference/run/screenshots/ref_{batch}_[0-9][0-9].png'))
frames = [np.array(Image.open(f'E:/mc-reference/run/screenshots/ref_{batch}_{i:02d}.png').convert('RGB')).astype(int) for i in range(N)]
PXPERBLOCK = 146.7
for ti in range(n):
    if only is not None and ti != only:
        continue
    px = int(640 - (ti - (n - 1) / 2) * 176)
    for row, (y0, y1) in (('up', (0, 270)), ('low', (275, 500))):
        x0, x1 = px - 80, px + 80
        stats = []
        for i, f in enumerate(frames):
            d = np.abs(f - base).sum(axis=2)[y0:y1, x0:x1]
            m = d > 60
            ys, xs = np.nonzero(m)
            if len(xs) < 3:
                stats.append(None)
                continue
            stats.append((len(xs), xs.min() + x0, xs.max() + x0, ys.min() + y0, ys.max() + y0, xs.mean() + x0, ys.mean() + y0))
        vis = [i for i, s in enumerate(stats) if s]
        if not vis:
            print(f'type {ti} {row}: nothing')
            continue
        a, b = stats[vis[0]], stats[vis[-1]]
        print(f'type {ti} {row}: visible frames {vis[0]}..{vis[-1]} ({len(vis)}), first w={a[2]-a[1]+1} h={a[4]-a[3]+1} n={a[0]} c=({a[5]:.0f},{a[6]:.0f}); last w={b[2]-b[1]+1} h={b[4]-b[3]+1} n={b[0]} c=({b[5]:.0f},{b[6]:.0f})')
        ys_ = [(i, stats[i][6]) for i in vis]
        if len(ys_) >= 4:
            ts = np.array([i for i, _ in ys_], float)
            ys = -np.array([y for _, y in ys_]) / PXPERBLOCK
            best = None
            for v0 in np.arange(-0.2, 0.6, 0.01):
                for fr in np.arange(0.5, 1.001, 0.02):
                    for g in np.arange(-0.01, 0.09, 0.005):
                        v = v0; y = ys[0]; err = 0.0
                        for k in range(1, len(ts)):
                            v = fr * (v - g); y += v
                            err += (y - ys[k]) ** 2
                        if best is None or err < best[0]:
                            best = (err, v0, fr, g)
            print(f'    fit: v0={best[1]:.3f} friction={best[2]:.2f} gravity={best[3]:.4f} rms={np.sqrt(best[0]/len(ts)):.4f} blocks; size(half width)={ (a[2]-a[1]+1)/PXPERBLOCK/2:.3f} -> {(b[2]-b[1]+1)/PXPERBLOCK/2:.3f}')
        if verbose:
            for i, s in enumerate(stats):
                print(f'   {i:2d}', '-' if not s else f'n={s[0]:4d} w={s[2]-s[1]+1:3d} h={s[4]-s[3]+1:3d} c=({s[5]:.1f},{s[6]:.1f})')
