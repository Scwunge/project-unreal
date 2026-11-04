# Writes lg_misc4.txt: what a jukebox with a disc in it (put in by a dropper) does to its neighbours (a lamp, dust, a locked hopper, a
# comparator, a repeater, a piston, a sticky observer is not needed), run frozen for 60 ticks then 100 ticks free before a last
# comparator refresh; and lightning rods struck by a bolt summoned in the cell above the rod.
Y = -60
L = ["cmd gamerule send_command_feedback false", "cmd gamemode spectator", "cmd execute in minecraft:overworld run tp @s 10 -50 20", "wait 200",
     "cmd tick freeze", "wait 4", "cmd fill -2 -60 -2 30 -58 60 air", "cmd fill -2 -61 -2 30 -61 60 stone", "wait 10"]
def c(s): L.append("cmd " + s)
EV = {}
def ev(t, s): EV.setdefault(t, []).append("cmd " + s)
disc = "minecraft:music_disc_cat"
# jukebox at (4,Y,6) with a dropper west (3,Y,6) facing east; neighbours: lamp north, dust east (5), hopper south (4,Y,7) facing east,
# lamp above (4,Y+1,6), repeater facing east beyond the dust? A comparator at (6,Y,6) reading the dust cell is not the jukebox, so none.
c(f'setblock 3 {Y} 6 dropper[facing=east]{{Items:[{{Slot:0,id:"{disc}",count:1}}]}}'); c(f"setblock 4 {Y} 6 jukebox")
c(f"setblock 4 {Y} 5 redstone_lamp"); c(f"setblock 5 {Y} 6 redstone_wire"); c(f"setblock 4 {Y} 7 hopper[facing=east]"); c(f"setblock 4 {Y+1} 6 redstone_lamp")
c(f"setblock 4 {Y-0} 9 jukebox"); c(f"setblock 3 {Y} 9 comparator[facing=west]")   # a comparator whose back is away from the jukebox: side input
c(f'setblock 12 {Y} 6 dropper[facing=east]{{Items:[{{Slot:0,id:"{disc}",count:1}}]}}'); c(f"setblock 13 {Y} 6 jukebox"); c(f"setblock 14 {Y} 6 comparator[facing=east]")
c(f"setblock 15 {Y} 6 redstone_wire"); c(f"setblock 13 {Y} 7 comparator[facing=north]"); c(f"setblock 13 {Y} 8 redstone_wire")
ev(2, f"setblock 3 {Y+1} 6 redstone_block"); ev(5, f"setblock 3 {Y+1} 6 air")
ev(2, f"setblock 12 {Y+1} 6 redstone_block"); ev(5, f"setblock 12 {Y+1} 6 air")
ev(30, f"setblock 12 {Y+1} 7 stone")
# lightning rods, bolt one cell above the rod (and one at the rod's own cell for a plain rod)
for k, rod in enumerate(("lightning_rod", "weathered_lightning_rod", "oxidized_lightning_rod", "exposed_lightning_rod")):
    x = 4 * k; z = 20
    c(f"setblock {x} {Y} {z} {rod}[facing=up]"); c(f"setblock {x} {Y-0} {z+1} {rod.replace('lightning_rod','copper')}")
    c(f"setblock {x+1} {Y} {z} redstone_lamp"); c(f"setblock {x+2} {Y} {z} redstone_wire")
    ev(2, f"summon lightning_bolt {x}.5 {Y+1} {z}.5")
c(f"setblock 0 {Y} 26 lightning_rod[facing=up]"); c(f"setblock 1 {Y} 26 redstone_lamp"); ev(2, f"summon lightning_bolt 0.5 {Y} 26.5")
BOX = f"-1 -60 -1 20 -58 30"
L += ["wait 10", f"log states {BOX}"]
for t in range(1, 61):
    for line in EV.get(t, []): L.append(line)
    L += ["cmd tick step 1", "wait 4", f"log states {BOX}"]
L += ["cmd tick unfreeze", "wait 100", "cmd tick freeze", "wait 4", f"cmd setblock 14 {Y+1} 6 stone", f"cmd setblock 3 {Y+1} 9 stone", "cmd tick step 3", "wait 4", f"log states {BOX}", "cmd tick unfreeze"]
open("lg_misc4.txt", "w").write("\n".join(L) + "\n")
