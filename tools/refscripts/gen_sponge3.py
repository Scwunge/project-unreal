# Writes sponge3.txt: which cells a sponge takes in a deep column (sponge2 left 2 more water in tall3 than the 6-step rule predicts).
# Three columns, each full of water in stone: a 3x3x12, a 1x1x12 and a 3x1x12; sponge at the top middle; every cell state logged right after.
COL = [("a", 3, 3, 12), ("b", 1, 1, 12), ("c", 3, 1, 12)]
L = ["cmd gamerule send_command_feedback false", "cmd gamemode spectator", "cmd execute in minecraft:overworld run tp @s 30 -40 10", "wait 200",
     "cmd tick freeze", "wait 4", "cmd gamerule do_mob_spawning false", "cmd weather clear 1000000"]
for y in range(-60, 30, 15):
    for z in range(-2, 30, 16):
        L.append(f"cmd fill -2 {y} {z} 60 {y+14} {z+15} air")
L.append("cmd fill -2 -62 -2 60 -61 30 stone")
for i, (n, w, d, h) in enumerate(COL):
    x0 = 3 + 12 * i; z0 = 3
    L.append(f"cmd fill {x0-1} -60 {z0-1} {x0+w} {-60+h} {z0+d} stone")
    L.append(f"cmd fill {x0} -60 {z0} {x0+w-1} {-60+h-1} {z0+d-1} water")
    L.append(f"cmd fill {x0} {-60+h} {z0} {x0+w-1} {-60+h} {z0+d-1} air")
L.append("wait 20")
for i, (n, w, d, h) in enumerate(COL):
    x0 = 3 + 12 * i; z0 = 3
    L.append(f"cmd setblock {x0 + w//2} {-60+h-1} {z0 + d//2} sponge")
L += ["cmd tick step 1", "wait 6"]
for i, (n, w, d, h) in enumerate(COL):
    x0 = 3 + 12 * i; z0 = 3
    L.append(f"log states {x0} -60 {z0} {x0+w-1} {-60+h-1} {z0+d-1}")
L.append("cmd tick unfreeze")
open("E:/mc-reference/scripts/sponge3.txt", "w").write("\n".join(L) + "\n")
