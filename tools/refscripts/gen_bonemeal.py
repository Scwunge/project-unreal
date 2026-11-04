# Writes bonemeal.txt: bone meal from a dispenser on young crops. Per kind a row (z = 6*i) of 40 copies 3 apart: the plant at (3c,-60,z),
# a dispenser beside it facing it with one bone meal, lit by a redstone block on top. Counts of the plant's states before and after.
KINDS = [("wheat", "wheat[age=0]", "farmland[moisture=7]"), ("wheat4", "wheat[age=4]", "farmland[moisture=7]"), ("carrots", "carrots[age=0]", "farmland[moisture=7]"),
         ("potatoes", "potatoes[age=0]", "farmland[moisture=7]"), ("beetroots", "beetroots[age=0]", "farmland[moisture=7]"),
         ("melon_stem", "melon_stem[age=0]", "farmland[moisture=7]"), ("pumpkin_stem", "pumpkin_stem[age=3]", "farmland[moisture=7]"),
         ("sweet_berry_bush", "sweet_berry_bush[age=0]", "dirt")]
COPIES = 40
L = ["cmd gamerule send_command_feedback false", "cmd gamemode spectator", "cmd execute in minecraft:overworld run tp @s 60 -50 25", "wait 200",
     "cmd tick freeze", "wait 4", "cmd gamerule do_mob_spawning false", "cmd gamerule random_tick_speed 0", "cmd weather clear 1000000", "cmd time set 6000"]
for y in range(-60, 40, 15):
    for z in range(-2, 60, 16):
        L.append(f"cmd fill -2 {y} {z} 130 {y+14} {z+15} air")
L.append("cmd fill -2 -62 -2 130 -62 60 stone")
L.append("cmd fill -2 -61 -2 130 -61 60 stone")
for i, (name, plant, ground) in enumerate(KINDS):
    z = 6 * i + 1
    for c in range(COPIES):
        x = 3 * c + 1
        L.append(f"cmd setblock {x} -61 {z} {ground}")
        L.append(f"cmd setblock {x} -60 {z} {plant}")
        L.append(f"cmd setblock {x+1} -60 {z} dispenser[facing=west]")
        L.append(f"cmd item replace block {x+1} -60 {z} container.0 with minecraft:bone_meal 1")
L.append("wait 20")
def snap():
    for i in range(len(KINDS)):
        z = 6 * i + 1
        L.append(f"log box 0 -60 {z} {3*COPIES+2} -60 {z}")
snap()
for i in range(len(KINDS)):
    z = 6 * i + 1
    L.append(f"cmd fill 2 -59 {z} {3*COPIES} -59 {z} air")
for i in range(len(KINDS)):
    z = 6 * i + 1
    for c in range(COPIES):
        L.append(f"cmd setblock {3*c+2} -59 {z} redstone_block")
L += ["cmd tick step 10", "wait 100"]
snap()
L += ["cmd tick unfreeze", "cmd gamerule random_tick_speed 3"]
open("E:/mc-reference/scripts/bonemeal.txt", "w").write("\n".join(L) + "\n")
