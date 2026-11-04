# Writes lg_juke.txt: each music disc put into a jukebox by a dropper, read by a comparator whose input side faces the jukebox; the
# dust beyond the comparator shows the signal. Frozen ticks, states per tick.
import json
Y = -60
discs = [k for k in json.load(open("../../cache/data/reports/registries.json"))["minecraft:item"]["entries"] if "music_disc" in k]
L = ["cmd gamerule send_command_feedback false", "cmd gamemode spectator", "cmd execute in minecraft:overworld run tp @s 10 -50 40", "wait 200",
     "cmd tick freeze", "wait 4", f"cmd fill -2 -60 -2 12 -58 {4 * len(discs) + 4} air", f"cmd fill -2 -61 -2 12 -61 {4 * len(discs) + 4} stone", "wait 10"]
def c(s): L.append("cmd " + s)
EV = {}
def ev(t, s): EV.setdefault(t, []).append("cmd " + s)
for i, d in enumerate(discs):
    z = 4 * i
    c(f'setblock 1 {Y} {z} dropper[facing=east]{{Items:[{{Slot:0,id:"{d}",count:1}}]}}'); c(f"setblock 2 {Y} {z} jukebox")
    c(f"setblock 3 {Y} {z} comparator[facing=west]"); c(f"setblock 4 {Y} {z} redstone_wire")
    ev(2, f"setblock 1 {Y+1} {z} redstone_block"); ev(5, f"setblock 1 {Y+1} {z} air")
BOX = f"-1 -60 -1 6 -60 {4 * len(discs)}"
L += ["wait 10", f"log states {BOX}"]
for t in range(1, 21):
    for line in EV.get(t, []): L.append(line)
    L += ["cmd tick step 1", "wait 4", f"log states {BOX}"]
L.append("cmd tick unfreeze")
open("lg_juke.txt", "w").write("\n".join(L) + "\n")
