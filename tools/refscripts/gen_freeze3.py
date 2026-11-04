# Writes freeze3a/b/c.txt (the layout of gen_icesnow.py, which worked, four chunks at a time): where ice and snow form.
#  a: snowy_plains, snowy_beach (0.05), stony_shore (0.2), taiga (0.25)         -- the temperature threshold at the ground
#  b: snowy_plains with a light block of level 8, 11, 12 and 15 beside every second water hole  -- light
#  c: stony_shore (0.2) with the floor at y=60, 100, 130, 160                   -- height
SETS = {"a": [("snowy_plains", -61, 0), ("snowy_beach", -61, 0), ("stony_shore", -61, 0), ("taiga", -61, 0)],
        "b": [("snowy_plains", -61, 8), ("snowy_plains", -61, 11), ("snowy_plains", -61, 12), ("snowy_plains", -61, 15)],
        "c": [("stony_shore", 60, 0), ("stony_shore", 100, 0), ("stony_shore", 130, 0), ("stony_shore", 160, 0)]}
for key, CH in SETS.items():
    L = ["cmd gamerule send_command_feedback false", "cmd gamemode spectator", "cmd execute in minecraft:overworld run tp @s 32 -50 8", "wait 200",
         "cmd tick freeze", "wait 4", "cmd gamerule do_mob_spawning false"]
    for y in range(-60, 320, 15):
        L.append(f"cmd fill 0 {y} 0 63 {min(y+14, 319)} 15 air")
    for c, (biome, H, light) in enumerate(CH):
        x0 = 16 * c
        L.append(f"cmd fill {x0} {H-1} 0 {x0+15} {H} 15 stone")
        L.append(f"cmd fillbiome {x0} {max(H-4, -64)} 0 {x0+15} {H+8} 15 minecraft:{biome}")
        for x in range(x0, x0 + 16):
            for z in range(16):
                if (x + z) % 2 == 0:
                    L.append(f"cmd setblock {x} {H} {z} water")
                elif light and x % 4 == 1 and z % 2 == 0:
                    L.append(f"cmd setblock {x} {H} {z} light[level={light}]")
    L.append("wait 20")
    def snap():
        for c, (biome, H, light) in enumerate(CH):
            L.append(f"log box {16*c} {H} 0 {16*c+15} {H+1} 15")
    L += ["cmd weather rain 1000000", "wait 100", "cmd tick unfreeze", "wait 60", "cmd tick freeze", "wait 20"]
    snap()
    for i in range(20):
        L += ["cmd tick step 100", "wait 150"]; snap()
    L.append("cmd tick unfreeze")
    open(f"E:/mc-reference/scripts/freeze3{key}.txt", "w").write("\n".join(L) + "\n")
