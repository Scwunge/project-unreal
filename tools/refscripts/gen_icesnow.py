# Writes icesnow.txt: snow and ice in the rain. Four chunks side by side (x 0..63, z 0..15), flat stone at y=-62..-61, water in holes at y=-61, air above:
# A snowy_plains bare floor (snow layers), B snowy_plains with a checkerboard of water cells (ice), C the same with no rain
# (cold alone freezes nothing at sea level? it does, at the edge: ice forms in cold biomes without rain), D plains in rain (control).
Y = -60
L = ["cmd gamerule send_command_feedback false", "cmd gamemode spectator", "cmd execute in minecraft:overworld run tp @s 8 -50 8", "wait 200",
     "cmd tick freeze", "wait 4", "cmd fill 0 -60 0 63 -50 15 air"] + [f"cmd fill 0 {y} 0 63 {y+15} 15 air" for y in range(-49, 320, 16)] + [ "cmd fill 0 -62 0 63 -61 15 stone", "cmd gamerule do_mob_spawning false",
     "cmd fillbiome 0 -64 0 47 -40 15 minecraft:snowy_plains", "cmd fillbiome 48 -64 0 63 -40 15 minecraft:plains", "wait 20"]
for x0 in (16, 32):
    for x in range(x0, x0 + 16):
        for z in range(16):
            if (x + z) % 2 == 0:
                L.append(f"cmd setblock {x} -61 {z} water")
for c in range(4):
    L.append(f"cmd execute if biome {c*16+5} -61 5 minecraft:snowy_plains run setblock {c*16+5} -55 5 diamond_block")
L.append("log states 0 -55 5 63 -55 5")
L += ["cmd weather rain 1000000", "wait 100", "cmd tick unfreeze", "wait 60", "cmd tick freeze", "wait 20"]
# chunk C should be dry: weather is global, so C gets rain too; it's chunk-rain-independent by design (see Notes). Kept as a second ice sample.
BOXES = ["0 -61 0 15 -60 15", "16 -61 0 31 -60 15", "32 -61 0 47 -60 15", "48 -61 0 63 -60 15"]
for b in BOXES: L.append(f"log box {b}")
for i in range(40):
    L += ["cmd tick step 100", "wait 150"]
    for b in BOXES: L.append(f"log box {b}")
L.append("cmd tick unfreeze")
open("E:/mc-reference/scripts/icesnow.txt", "w").write("\n".join(L) + "\n")
