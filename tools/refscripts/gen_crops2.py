# Writes E:/mc-reference/scripts/crops2.txt: crop growth calibration. Each shape is a wheat plant at (3,3) over a patch of
# farmland (dry: no water about; wet: water 3 blocks east), 21 patches 6 apart in a row, two rows (z = 8*i) per shape; ages
# are summed over each pair of rows every 80 ticks for 1600 ticks. Built identically by McClient.McSim.CropCalib.
import sys
BOX = lambda x0, x1, z0, z1: [(x, z) for x in range(x0, x1 + 1) for z in range(z0, z1 + 1)]
SHAPES = [
    ('dry1', 0, [(3, 3)]),
    ('dry9', 0, BOX(2, 4, 2, 4)),
    ('wet1', 7, [(3, 3)]),
    ('wet3', 7, [(2, 3), (3, 3), (4, 3)]),
    ('wet5', 7, [(3, 3), (2, 3), (4, 3), (3, 2), (3, 4)]),
    ('wet9', 7, BOX(2, 4, 2, 4)),
]
COPIES, SPACING, REPS, ROWH = 21, 6, 2, 8
def rows():
    r = 0
    for s in SHAPES:
        for _ in range(REPS):
            yield r, s
            r += 1
if __name__ == '__main__':
    L = ['cmd gamerule send_command_feedback false', 'cmd gamemode creative', 'cmd gamerule random_tick_speed 100', 'cmd tp @s 56 -50 48',
         'wait 60', 'cmd weather clear 1000000', 'cmd time set 6000', 'cmd tick freeze']
    zmax = ROWH * len(SHAPES) * REPS + 4
    for z in range(-2, zmax, 10):
        L.append(f'cmd fill -2 -62 {z} 130 -48 {min(z+9, zmax)} air')
    for z in range(-2, zmax, 10):
        L.append(f'cmd fill -2 -62 {z} 130 -61 {min(z+9, zmax)} stone')
    for a in (f'-3 -62 -3 131 -48 -3', f'-3 -62 {zmax+1} 131 -48 {zmax+1}', f'-3 -62 -3 -3 -48 {zmax+1}', f'131 -62 -3 131 -48 {zmax+1}'):
        L.append(f'cmd fill {a} stone')
    L.append('wait 40')
    for r, (name, moist, cells) in rows():
        for c in range(COPIES):
            ox, oz = c * SPACING, r * ROWH
            for x, z in cells:
                L.append(f'cmd setblock {ox+x} -61 {oz+z} farmland[moisture={moist}]')
            if moist:
                L.append(f'cmd setblock {ox+6} -61 {oz+3} water')
            L.append(f'cmd setblock {ox+3} -60 {oz+3} wheat[age=0]')
    L += ['cmd tick step 20', 'wait 100']
    for t in range(20):
        L += ['cmd tick step 80', 'wait 400']
        for r, s in rows():
            z = r * ROWH
            L.append(f'log box 0 -60 {z} {(COPIES-1)*SPACING+6} -60 {z+6}')
    open('E:/mc-reference/scripts/crops2.txt', 'w').write('\n'.join(L) + '\n')
