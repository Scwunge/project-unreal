# Writes lg_logi.txt: a hopper clock and a dropper elevator, one block-state log per tick. Container contents are not in a
# block state, so comparators beside the containers show them (powered while the container holds anything; a comparator takes its input from the side it faces). Replayed by
# McClient.McSim.RefScriptsLogistics (McLogisticsReplayTest.cpp), since hoppers and droppers live in the block-use subsystem.
BOX = "-1 -60 -3 10 -57 12"
Y = -60
L = ["cmd gamerule send_command_feedback false", "cmd gamemode spectator", "cmd tp @s 5 -50 4", "wait 40", "cmd tick freeze",
     "cmd fill -3 -62 -3 12 -49 -3 stone", "cmd fill -3 -62 14 12 -49 14 stone", "cmd fill -3 -62 -3 -3 -49 14 stone",
     "cmd fill 12 -62 -3 12 -49 14 stone", "cmd fill -3 -49 -3 12 -49 14 stone", "wait 4",
     "cmd fill -2 -60 -2 11 -50 13 air", "cmd fill -2 -61 -2 11 -61 13 stone", "wait 10"]
def c(s): L.append("cmd " + s)
# hopper clock: two hoppers pointing at each other, one item; a comparator and lamp read each
c(f'setblock 1 {Y} 0 hopper[facing=east]{{Items:[{{Slot:0,id:"minecraft:cobblestone",count:1}}]}}')
c(f"setblock 2 {Y} 0 hopper[facing=west]")
c(f"setblock 1 {Y} -1 comparator[facing=south]"); c(f"setblock 2 {Y} -1 comparator[facing=south]")
c(f"setblock 1 {Y} -2 redstone_lamp"); c(f"setblock 2 {Y} -2 redstone_lamp")
# a hopper clock that is also locked by a redstone block part of the way
c(f'setblock 5 {Y} 0 hopper[facing=east]{{Items:[{{Slot:0,id:"minecraft:cobblestone",count:1}}]}}')
c(f"setblock 6 {Y} 0 hopper[facing=west]")
c(f"setblock 5 {Y} -1 comparator[facing=south]"); c(f"setblock 6 {Y} -1 comparator[facing=south]")
# dropper elevator: three droppers facing up and a chest on top, each beside a comparator, a redstone block on each one's west side pulsed
for i in range(3):
    it = '{Items:[{Slot:0,id:"minecraft:cobblestone",count:3}]}' if i == 0 else ""
    c(f"setblock 8 {Y+i} 6 dropper[facing=up]{it}")
    c(f"setblock 9 {Y+i} 6 comparator[facing=west]")
c(f"setblock 8 {Y+3} 6 chest")
L.append("wait 10")
L.append(f"log states {BOX}"); L.append(f"log items {BOX}")
EV = {}
for t in (10, 22, 34, 46):                      # all three droppers at once, four times
    for i in range(3):
        EV.setdefault(t, []).append(f"cmd setblock 7 {Y+i} 6 redstone_block"); EV.setdefault(t + 2, []).append(f"cmd setblock 7 {Y+i} 6 air")
EV.setdefault(60, []).append(f"cmd setblock 5 {Y+1} 0 redstone_block")      # locks the second clock's hopper at 5
EV.setdefault(100, []).append(f"cmd setblock 5 {Y+1} 0 air")
for t in range(1, 141):
    for line in EV.get(t, []): L.append(line)
    L += ["cmd tick step 1", "wait 4", f"log states {BOX}", f"log items {BOX}"]
L.append("cmd tick unfreeze")
open("lg_logi.txt", "w").write("\n".join(L) + "\n")
