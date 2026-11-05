# Tracks one kind of particle through a stepped reference run (scripts/pb*.txt in E:\mc-reference): the frames are one tick apart,
# the kinds are spread along x, upper row only. For each frame prints the particle's pixel count, bounding box, centroid and mean
# colour, found as the difference from the median frame (the background). Usage: particle_track.py <batch> <ntypes> <type index> [row y px]
import sys
import numpy as np
from PIL import Image

batch, n, ti = sys.argv[1], int(sys.argv[2]), int(sys.argv[3])
rowy = int(sys.argv[4]) if len(sys.argv) > 4 else 216
frames = [np.array(Image.open(f'E:/mc-reference/run/screenshots/ref_{batch}_{i:02d}.png').convert('RGB')).astype(int) for i in range(40)]
median = np.median(np.stack(frames), axis=0)
px = int(640 + (ti - (n - 1) / 2) * 176)
x0, x1, y0, y1 = px - 85, px + 85, max(rowy - 170, 0), min(rowy + 170, 430)
for i, f in enumerate(frames):
    d = np.abs(f - median).sum(axis=2)[y0:y1, x0:x1]
    m = d > 60
    ys, xs = np.nonzero(m)
    if len(xs) < 3:
        print(f'{i:2d} -')
        continue
    col = f[y0:y1, x0:x1][m].mean(axis=0).astype(int)
    print(f'{i:2d} n={len(xs):4d} x[{xs.min()+x0:4d},{xs.max()+x0:4d}] y[{ys.min()+y0:4d},{ys.max()+y0:4d}] c=({xs.mean()+x0:6.1f},{ys.mean()+y0:6.1f}) rgb={tuple(col)}')
