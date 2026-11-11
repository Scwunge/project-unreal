# Writes bonemeal2.txt: bone meal from a dispenser on a sapling (stage 0), a cocoa pod (age 0), short grass, and a grass block with air above.
# Rows z = 14*i; copies 10 apart (14 for the grass block, whose spread is counted over the whole row).
KINDS = [("sapling", "oak_sapling[stage=0]", "grass_block", 30, 10), ("cocoa", "cocoa[age=0,facing=north]", None, 30, 10),
         ("short_grass", "short_grass", "grass_block", 30, 10), ("grass_block", None, "grass_block", 20, 14)]
L = ["cmd gamerule send_command_feedback false", "cmd gamemode spectator", "cmd execute in minecraft:overworld run tp @s 60 -50 25", "wait 200",
     "cmd tick freeze", "wait 4", "cmd gamerule do_mob_spawning false", "cmd gamerule random_tick_speed 0", "cmd weather clear 1000000", "cmd time set 6000"]
for y in range(-60, 20, 15):
    for z in range(-2, 60, 16):
        for x0 in range(-2, 301, 80):
            L.append(f"cmd fill {x0} {y} {z} {x0+79} {y+14} {z+15} air")
L.append("cmd fill -2 -62 -2 100 -62 60 stone")
L.append("cmd fill 101 -62 -2 300 -62 60 stone")
L.append("cmd fill -2 -61 -2 100 -61 60 grass_block")
L.append("cmd fill 101 -61 -2 300 -61 60 grass_block")
for i, (name, plant, ground, copies, step) in enumerate(KINDS):
    z = 14 * i + 5
    for c in range(copies):
        x = step * c + 6
        if name == "cocoa":
            L.append(f"cmd setblock {x} -60 {z+1} jungle_log")
        if plant:
            L.append(f"cmd setblock {x} -60 {z} {plant}")
        L.append(f"cmd setblock {x+1} {-61 if name == 'grass_block' else -60} {z} dispenser[facing=west]")
        L.append(f"cmd item replace block {x+1} {-61 if name == 'grass_block' else -60} {z} container.0 with minecraft:bone_meal 1")
L.append("wait 20")
def snap():
    for i, (name, plant, ground, copies, step) in enumerate(KINDS):
        z = 14 * i + 5
        L.append(f"log states -2 -60 {z-5} {step*copies+8} -58 {z+5}")
snap()
for i, (name, plant, ground, copies, step) in enumerate(KINDS):
    z = 14 * i + 5
    for c in range(copies):
        x = step * c + 6
        L.append(f"cmd setblock {x+2} {-61 if name == 'grass_block' else -60} {z} redstone_block")
L += ["cmd tick step 10", "wait 100"]
snap()
L += ["cmd tick unfreeze", "cmd gamerule random_tick_speed 3"]
open("E:/mc-reference/scripts/bonemeal2.txt", "w").write("\n".join(L) + "\n")
