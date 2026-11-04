# Writes sapling.txt: bone meal from a dispenser on 180 oak saplings (stage 0), two rows of 90, 3 apart; before/after counts.
L = ["cmd gamerule send_command_feedback false", "cmd gamemode spectator", "cmd execute in minecraft:overworld run tp @s 130 -50 10", "wait 200",
     "cmd tick freeze", "wait 4", "cmd gamerule do_mob_spawning false", "cmd gamerule random_tick_speed 0", "cmd weather clear 1000000", "cmd time set 6000"]
for x0 in range(-2, 290, 80):
    L.append(f"cmd fill {x0} -60 -2 {x0+79} -50 24 air")
L.append("cmd fill -2 -62 -2 140 -62 24 stone"); L.append("cmd fill 141 -62 -2 290 -62 24 stone")
L.append("cmd fill -2 -61 -2 140 -61 24 grass_block"); L.append("cmd fill 141 -61 -2 290 -61 24 grass_block")
for row, z in enumerate((4, 14)):
    for c in range(90):
        x = 3 * c + 1
        L += [f"cmd setblock {x} -60 {z} oak_sapling[stage=0]", f"cmd setblock {x+1} -60 {z} dispenser[facing=west]",
              f"cmd item replace block {x+1} -60 {z} container.0 with minecraft:bone_meal 1"]
L.append("wait 20")
for z in (4, 14): L.append(f"log box 0 -60 {z} 275 -60 {z}")
for z in (4, 14):
    for c in range(90): L.append(f"cmd setblock {3*c+2} -59 {z} redstone_block")
L += ["cmd tick step 10", "wait 100"]
for z in (4, 14): L.append(f"log box 0 -60 {z} 275 -60 {z}")
L += ["cmd tick unfreeze", "cmd gamerule random_tick_speed 3"]
open("E:/mc-reference/scripts/sapling.txt", "w").write("\n".join(L) + "\n")
