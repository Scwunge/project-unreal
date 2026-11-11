# Writes pushent.txt: what a piston does to entities. Frozen ticks, server entities logged after every step.
Y = -60
L = ["cmd gamerule send_command_feedback false", "cmd gamemode spectator", "cmd execute in minecraft:overworld run tp @s 20 -50 20", "wait 200",
     "cmd kill @e[type=!player]", "cmd tick freeze", "wait 4", "cmd fill -2 -60 -2 40 -55 60 air", "cmd fill -2 -61 -2 40 -61 60 stone", "cmd gamerule do_mob_spawning false", "wait 10"]
def c(s): L.append("cmd " + s)
EV = {}
def ev(t, s): EV.setdefault(t, []).append("cmd " + s)
PIG = "{NoAI:1b,Silent:1b,PersistenceRequired:1b}"
ITEM = '{Item:{id:"minecraft:stone",count:1},Age:-32768s,PickupDelay:32767s}'
# 0: piston up, nothing on it but a pig standing in the cell the head moves into (3,-59)
c(f"setblock 3 {Y} 0 piston[facing=up]"); c(f"summon pig 3.5 {Y+1} 0.5 {PIG}"); ev(2, f"setblock 3 {Y-1} 0 redstone_block")
# 1: piston east, pig in the head's cell (4,-60)
c(f"setblock 3 {Y} 6 piston[facing=east]"); c(f"summon pig 4.5 {Y} 6.5 {PIG}"); ev(2, f"setblock 3 {Y-1} 6 redstone_block")
# 2: piston east pushes a stone, a pig stands on the stone
c(f"setblock 3 {Y} 12 piston[facing=east]"); c(f"setblock 4 {Y} 12 stone"); c(f"summon pig 4.5 {Y+1} 12.5 {PIG}"); ev(2, f"setblock 3 {Y-1} 12 redstone_block")
# 3: piston east, item entity in the head's cell
c(f"setblock 3 {Y} 18 piston[facing=east]"); c(f"summon item 4.5 {Y} 18.5 {ITEM}"); ev(2, f"setblock 3 {Y-1} 18 redstone_block")
# 4: piston up pushes a stone with a pig on it
c(f"setblock 3 {Y} 24 piston[facing=up]"); c(f"setblock 3 {Y+1} 24 stone"); c(f"summon pig 3.5 {Y+2} 24.5 {PIG}"); ev(2, f"setblock 3 {Y-1} 24 redstone_block")
# 5: piston east pushing a slime block with a pig on it (slime launches?)
c(f"setblock 3 {Y} 30 piston[facing=east]"); c(f"setblock 4 {Y} 30 slime_block"); c(f"summon pig 4.5 {Y+1} 30.5 {PIG}"); ev(2, f"setblock 3 {Y-1} 30 redstone_block")
# 6: sticky piston retracts a stone with a pig on it: extended first (power at start), retract at t=2
c(f"setblock 3 {Y} 36 sticky_piston[facing=east]"); c(f"setblock 4 {Y} 36 stone"); ev(1, f"setblock 3 {Y-1} 36 redstone_block")
ev(8, f"summon pig 5.5 {Y+1} 36.5 {PIG}"); ev(10, f"setblock 3 {Y-1} 36 stone")
# 7: piston east, a pig standing beside the arm (z+1) not in the way
c(f"setblock 3 {Y} 42 piston[facing=east]"); c(f"summon pig 4.5 {Y} 43.5 {PIG}"); ev(2, f"setblock 3 {Y-1} 42 redstone_block")
# 8: piston up, a player-size armor stand on top of the head cell
c(f"setblock 3 {Y} 48 piston[facing=up]"); c(f"summon armor_stand 3.5 {Y+1} 48.5"); ev(2, f"setblock 3 {Y-1} 48 redstone_block")
L += ["wait 10", "log sents minecraft: 90"]
for t in range(1, 21):
    for line in EV.get(t, []): L.append(line)
    L += ["cmd tick step 1", "wait 4", "log sents minecraft: 90"]
L.append("cmd tick unfreeze")
open("pushent.txt", "w").write("\n".join(L) + "\n")
