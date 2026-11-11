# Writes lg_misc3.txt: jukebox with a disc put in four ways (dropper, hopper, two NBT spellings), read by a comparator and dust, with a
# block update beside the comparator at tick 20 to see whether it was reading stale; lightning rods struck by a bolt (plain,
# weathered, oxidized, exposed). Frozen ticks, states per tick.
Y = -60
L = ["cmd gamerule send_command_feedback false", "cmd gamemode spectator", "cmd execute in minecraft:overworld run tp @s 10 -50 20", "wait 200",
     "cmd tick freeze", "wait 4", "cmd fill -2 -60 -2 30 -58 60 air", "cmd fill -2 -61 -2 30 -61 60 stone", "wait 10"]
def c(s): L.append("cmd " + s)
EV = {}
def ev(t, s): EV.setdefault(t, []).append("cmd " + s)
def reader(z):
    c(f"setblock 3 {Y} {z} comparator[facing=east]"); c(f"setblock 4 {Y} {z} redstone_wire")
    ev(20, f"setblock 3 {Y} {z+1} stone")
disc = "minecraft:music_disc_cat"
z = 0   # dropper
c(f'setblock 1 {Y} {z} dropper[facing=east]{{Items:[{{Slot:0,id:"{disc}",count:1}}]}}'); c(f"setblock 2 {Y} {z} jukebox"); reader(z); ev(2, f"setblock 1 {Y+1} {z} redstone_block")
z = 4   # hopper
c(f'setblock 1 {Y} {z} hopper[facing=east]{{Items:[{{Slot:0,id:"{disc}",count:1}}]}}'); c(f"setblock 2 {Y} {z} jukebox"); reader(z)
z = 8   # NBT: RecordItem
c(f'setblock 2 {Y} {z} jukebox{{RecordItem:{{id:"{disc}",count:1}}}}'); reader(z)
z = 12  # NBT: record_item
c(f'setblock 2 {Y} {z} jukebox{{record_item:{{id:"{disc}",count:1}}}}'); reader(z)
z = 16  # dropper with the other disc 13 for the level
c(f'setblock 1 {Y} {z} dropper[facing=east]{{Items:[{{Slot:0,id:"minecraft:music_disc_13",count:1}}]}}'); c(f"setblock 2 {Y} {z} jukebox"); reader(z); ev(2, f"setblock 1 {Y+1} {z} redstone_block")
# lightning rods
for k, rod in enumerate(("lightning_rod", "weathered_lightning_rod", "oxidized_lightning_rod", "exposed_lightning_rod")):
    x = 4 * k; z = 24
    c(f"setblock {x} {Y} {z} {rod}[facing=up]"); c(f"setblock {x} {Y} {z+1} {rod.replace('lightning_rod','copper')}")
    c(f"setblock {x+1} {Y} {z} redstone_lamp"); c(f"setblock {x+2} {Y} {z} redstone_wire")
    ev(2, f"summon lightning_bolt {x}.5 {Y} {z}.5")
BOX = f"-1 -60 -1 20 -59 30"
L += ["wait 10", f"log states {BOX}"]
for t in range(1, 41):
    for line in EV.get(t, []): L.append(line)
    L += ["cmd tick step 1", "wait 4", f"log states {BOX}"]
L.append("cmd tick unfreeze")
open("lg_misc3.txt", "w").write("\n".join(L) + "\n")
