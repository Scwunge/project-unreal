# Writes farmland.txt: which water hydrates farmland (provenance: farmland reach). One cell per (dy, dx, dz): farmland[moisture=3] with wheat on top
# at the cell centre, a water source in a stone ring dx/dz/dy away; random ticks fast; moisture after 60 ticks (7 wet, 0 dry). Phase 2: rain, no water.
DY = [-1, 0, 1, 2]
OFF = [(1, 0), (2, 0), (3, 0), (4, 0), (5, 0), (4, 4), (5, 4), (4, 5), (5, 5), (0, 4), (0, 5), (-4, 0), (-5, 0), (0, 1)]
CELLS = [(dy, dx, dz) for dy in DY for dx, dz in OFF] + [(None, 0, 0)]
COLS = 8
S = 16
L = ["cmd gamerule send_command_feedback false", "cmd gamemode spectator", "cmd execute in minecraft:overworld run tp @s 60 -50 60", "wait 200",
     "cmd tick freeze", "wait 4", "cmd gamerule do_mob_spawning false", "cmd gamerule random_tick_speed 0", "cmd weather clear 1000000", "cmd time set 6000"]
for x0 in range(-4, COLS * S + 8, 40):
    for z0 in range(-4, 9 * S, 16):
        L.append(f"cmd fill {x0} -62 {z0} {x0+39} -52 {z0+15} air")
for z0 in range(-4, 151, 40):
    L.append(f"cmd fill -4 -64 {z0} 135 -63 {min(z0+39, 150)} stone")
for x0 in range(-4, COLS * S + 8, 40):
    for z0 in range(-4, 9 * S, 16):
        L.append(f"cmd fill {x0} -62 {z0} {x0+39} -61 {z0+15} stone")
pos = []
for i, (dy, dx, dz) in enumerate(CELLS):
    cx = S * (i % COLS) + 6; cz = S * (i // COLS) + 6
    pos.append((cx, cz))
    if dy is not None:
        wx, wy, wz = cx + dx, -61 + dy, cz + dz
        for sx, sz in ((1, 0), (-1, 0), (0, 1), (0, -1)):
            L.append(f"cmd setblock {wx+sx} {wy} {wz+sz} stone")
        L.append(f"cmd setblock {wx} {wy-1} {wz} stone")
        L.append(f"cmd setblock {wx} {wy} {wz} water")
for cx, cz in pos:
    L.append(f"cmd setblock {cx} -61 {cz} farmland[moisture=3]")
    L.append(f"cmd setblock {cx} -60 {cz} wheat[age=0]")
L.append("wait 20")
L.append("cmd gamerule random_tick_speed 1000")
L += ["cmd tick step 60", "wait 100"]
for cx, cz in pos:
    L.append(f"log states {cx} -61 {cz} {cx} -61 {cz}")
L.append("cmd gamerule random_tick_speed 0")
# phase 2: rain, same cells all reset to moisture 3
for cx, cz in pos:
    L.append(f"cmd setblock {cx} -61 {cz} farmland[moisture=3]")
L += ["cmd weather rain 1000000", "wait 20", "cmd tick unfreeze", "wait 40", "cmd tick freeze", "wait 4", "cmd gamerule random_tick_speed 1000", "cmd tick step 60", "wait 100"]
for cx, cz in pos:
    L.append(f"log states {cx} -61 {cz} {cx} -61 {cz}")
L += ["cmd tick unfreeze", "cmd gamerule random_tick_speed 3"]
open("E:/mc-reference/scripts/farmland.txt", "w").write("\n".join(L) + "\n")
print(len(CELLS), len(L))
