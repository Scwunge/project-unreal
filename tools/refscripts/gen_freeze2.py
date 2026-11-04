# Writes freeze2.txt: where ice and snow form. Chunks side by side (x = 16*c), each with a stone floor two thick at height H and water in every
# other cell of its top layer, rain on, counts of ice, water and snow layers at 1000, 2000 and 4000 ticks.
#  c 0..7  : snowy_plains, floor at -61, a light block of level 8..15 beside every other water hole (light: does ice or snow stop near light?)
#  c 8..11 : biomes snowy_beach (0.05), stony_shore (0.2), taiga (0.25), plains (0.8) at the ground (temperature threshold)
#  c 12..14: stony_shore (0.2) with the floor at y=100, 130 and 160 (does height make it colder?)
#  c 15    : a copy of c 0 (is a far chunk as live as a near one)
CH = []
for n in range(8, 16): CH.append(("light%d" % n, "snowy_plains", -61, n))
for b in ("snowy_beach", "stony_shore", "taiga", "plains"): CH.append((b, b, -61, 0))
for h in (100, 130, 160): CH.append(("stony%d" % h, "stony_shore", h, 0))
CH.append(("light8b", "snowy_plains", -61, 8))
L = ["cmd gamerule send_command_feedback false", "cmd gamemode spectator", "cmd execute in minecraft:overworld run tp @s 120 -50 8", "wait 200",
     "cmd tick freeze", "wait 4", "cmd gamerule do_mob_spawning false", "cmd weather clear 1000000"]
for x0 in (0, 128):
    for y in range(-60, 320, 15):
        L.append(f"cmd fill {x0} {y} 0 {x0+127} {min(y+14, 319)} 15 air")
for c, (name, biome, H, light) in enumerate(CH):
    x0 = 16 * c
    L.append(f"cmd fill {x0} {H-1} 0 {x0+15} {H} 15 stone")
    L.append(f"cmd fillbiome {x0} {max(H-4, -64)} 0 {x0+15} {H+8} 15 minecraft:{biome}")
    for x in range(x0, x0 + 16):
        for z in range(16):
            if (x + z) % 2 == 0:
                L.append(f"cmd setblock {x} {H} {z} water")
            elif light and x % 4 == 1 and z % 2 == 0:
                L.append(f"cmd setblock {x} {H} {z} light[level={light}]")
L.append("wait 40")
def snap():
    for c, (name, biome, H, light) in enumerate(CH):
        L.append(f"log box {16*c} {H} 0 {16*c+15} {H+1} 15")
snap()
L += ["cmd weather rain 1000000", "wait 100", "cmd tick unfreeze", "wait 60", "cmd tick freeze", "wait 20"]
for t in (1000, 1000, 2000):
    L += [f"cmd tick step {t}", "wait 1500"]; snap()
L.append("cmd tick unfreeze")
open("E:/mc-reference/scripts/freeze2.txt", "w").write("\n".join(L) + "\n")
