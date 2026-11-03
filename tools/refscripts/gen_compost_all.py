# Writes lg_compost_all.txt: every item in the registry in a hopper (one stack) over a composter, ticks running free. Which items a
# composter takes at all (the hopper keeps what it refuses) and, roughly, how many it needed to fill (more items = lower chance).
import json
Y = -60
items = list(json.load(open("../../cache/data/reports/registries.json"))["minecraft:item"]["entries"].keys())
PER = 40
rows = (len(items) + PER - 1) // PER
XM, ZM = 2 * PER, 2 * rows
L = ["cmd gamerule send_command_feedback false", "cmd gamemode spectator", f"cmd execute in minecraft:overworld run tp @s {XM // 2} -50 {ZM // 2}", "wait 200",
     "cmd tick freeze", "wait 4",
     f"cmd fill -1 -60 -1 {XM + 1} -58 {ZM + 1} air", f"cmd fill -1 -61 -1 {XM + 1} -61 {ZM + 1} stone", "wait 10"]
for r in range(rows):
    z = 2 * r
    L.append(f"cmd fill 0 {Y} {z} {2 * (PER - 1)} {Y} {z} composter[level=0]")
for i, it in enumerate(items):
    x, z = 2 * (i % PER), 2 * (i // PER)
    L.append(f'cmd setblock {x} {Y+1} {z} hopper[facing=down]{{Items:[{{Slot:0,id:"{it}",count:64}}]}}')
L += ["cmd tick unfreeze", "wait 900", f"log items 0 {Y+1} 0 {XM} {Y+1} {ZM}", f"log states 0 {Y} 0 {XM} {Y} {ZM}"]
open("lg_compost_all.txt", "w").write("\n".join(L) + "\n")
