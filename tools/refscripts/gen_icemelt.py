# Writes icemelt.txt: snow and ice melting by light. Rows (z = 20*i) for light-source levels 8..15 (a light block beside each cell, so the
# cell reads one less); in each row 30 copies, 3 apart in x, of: snow layer, snow block, ice; random tick speed 100, clear weather,
# plains (ice melts to water). Counts per row every 10 ticks for 200.
LEVELS = list(range(8, 16)); COPIES = 30
L = ["cmd gamerule send_command_feedback false", "cmd gamemode spectator", "cmd execute in minecraft:overworld run tp @s 45 -50 80", "wait 200",
     "cmd tick freeze", "wait 4", "cmd gamerule random_tick_speed 100", "cmd weather clear 1000000", "cmd gamerule do_mob_spawning false"]
for y in range(-60, 40, 16):
    for z in range(-2, 160, 16):
        L.append(f"cmd fill -2 {y} {z} 100 {y+15} {z+15} air")
for z in range(-2, 160, 16):
    L.append(f"cmd fill -2 -62 {z} 100 -61 {z+15} stone")
L.append("cmd fillbiome -2 -64 -2 100 -40 160 minecraft:plains")
for i, n in enumerate(LEVELS):
    z = 20 * i + 2
    for c in range(COPIES):
        x = 3 * c
        L.append(f"cmd setblock {x} -60 {z} snow[layers=1]")
        L.append(f"cmd setblock {x+1} -60 {z+4} snow_block")
        L.append(f"cmd setblock {x+1} -60 {z+8} ice")
        L.append(f"cmd setblock {x} -60 {z+1} light[level={n}]")
        L.append(f"cmd setblock {x+1} -60 {z+5} light[level={n}]")
        L.append(f"cmd setblock {x+1} -60 {z+9} light[level={n}]")
L.append("wait 40")
def snap():
    for i in range(len(LEVELS)):
        z = 20 * i + 2
        L.append(f"log box 0 -60 {z} {3*COPIES} -60 {z+9}")
snap()
for s in range(20):
    L += ["cmd tick step 10", "wait 80"]; snap()
L.append("cmd tick unfreeze")
open("E:/mc-reference/scripts/icemelt.txt", "w").write("\n".join(L) + "\n")
