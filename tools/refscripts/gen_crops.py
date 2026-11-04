# Writes E:/mc-reference/scripts/crops1.txt: crop growth fields for the vanilla reference. Each variant is a row of 14 patches
# (7 wide, 8 apart) at z = 12*row; the wheat or melon-stem ages are counted over each row every 80 ticks ("log box").
# The same layout is built by McClient.McSim.CropField, which logs "mccrop ..." lines for cropcmp.py.
import sys
Z0 = 0
VARIANTS = [
    # name, crop, farmland moisture, crop cells (x,z), water cells (x,z)
    ('dry_single', 'wheat', 0, [(3, 3)], [], [(2, 2), (4, 4)]),
    ('wet_single', 'wheat', 7, [(3, 3)], [(5, 3)], [(2, 2), (4, 4)]),
    ('wet_row', 'wheat', 7, [(x, 3) for x in range(1, 6)], [(3, 5)], [(0, 2), (6, 4)]),
    ('wet_checker', 'wheat', 7, [(x, z) for x in (1, 3, 5) for z in (1, 3, 5)], [(0, 3), (6, 3)], [(0, 0), (6, 6)]),
    ('wet_full', 'wheat', 7, [(x, z) for x in range(1, 6) for z in range(1, 6)], [(0, 3), (6, 3)], [(0, 0), (6, 6)]),
    ('wet_stem', 'melon_stem', 7, [(3, 3)], [(5, 3)], [(2, 2), (4, 4)]),
]
COPIES = 14
SPACING = 8
def patch(var):
    name, crop, moist, crops, waters, farm = var
    # farmland over the whole footprint: the single ones 3x3, the rest 7x7
    cells = [(x, z) for x in range(2, 5) for z in range(2, 5)] if len(crops) == 1 else [(x, z) for x in range(7) for z in range(7)] if len(crops) > 5 or name == 'wet_row' else []
    if name == 'wet_row':
        cells = [(x, z) for x in range(7) for z in range(2, 5)]
    return cells
def build():
    out = []
    for row, var in enumerate(VARIANTS):
        name, crop, moist, crops, waters, _ = var
        cells = patch(var)
        for c in range(COPIES):
            ox, oz = c * SPACING, Z0 + 12 * row
            for (x, z) in cells:
                if (x, z) in waters or (x, z) in crops:
                    pass
                out.append(f'cmd setblock {ox+x} -61 {oz+z} farmland[moisture={moist}]')
            for (x, z) in waters:
                out.append(f'cmd setblock {ox+x} -61 {oz+z} water')
            for (x, z) in crops:
                out.append(f'cmd setblock {ox+x} -60 {oz+z} {crop}[age=0]')
    return out
if __name__ == '__main__':
    lines = [
        'cmd gamerule send_command_feedback false', 'cmd gamemode creative',
        'cmd gamerule random_tick_speed 100', 'cmd tp @s 56 -50 36', 'wait 60', 'cmd weather clear 1000000', 'cmd time set 6000',
        'cmd tick freeze',
    ]
    # fill is limited to 32768 blocks: clear in bands (air from -50 down so old builds go too, then the stone ground)
    for z in range(-2, 81, 12):
        lines.append(f'cmd fill -2 -62 {z} 130 -48 {min(z+11, 80)} air')
    for z in range(-2, 81, 12):
        lines.append(f'cmd fill -2 -62 {z} 130 -61 {min(z+11, 80)} stone')
    # the reference world's sea floods open ground: wall the field in (before clearing is too late, so wall, then clear again)
    for a in ('-3 -62 -3 131 -48 -3', '-3 -62 81 131 -48 81', '-3 -62 -3 -3 -48 81', '131 -62 -3 131 -48 81'):
        lines.append(f'cmd fill {a} stone')
    lines.append('wait 40')
    lines += build()
    lines += ['cmd tick step 20', 'wait 10']
    for t in range(10):
        lines += ['cmd tick step 80', 'wait 400']
        for row, var in enumerate(VARIANTS):
            z = Z0 + 12 * row
            lines.append(f'log box 0 -60 {z} {(COPIES-1)*SPACING+6} -60 {z+6}')
    open(sys.argv[1] if len(sys.argv) > 1 else 'E:/mc-reference/scripts/crops1.txt', 'w').write('\n'.join(lines) + '\n')
