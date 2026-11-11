# Writes pushent2.txt: piston entity push, slime/honey carry and retraction. Pigs logged each tick; blocks of each row logged each tick.
Y = -57
L = ["cmd gamerule send_command_feedback false", "cmd gamemode spectator", "cmd execute in minecraft:overworld run tp @s 20 -50 20", "wait 200",
     "cmd kill @e[type=!player]", "cmd tick freeze", "wait 4", "cmd fill -2 -60 -2 40 -52 60 air", "cmd fill -2 -61 -2 40 -61 60 stone", "cmd gamerule do_mob_spawning false", "wait 10"]
def c(s): L.append("cmd " + s)
EV = {}
def ev(t, s): EV.setdefault(t, []).append("cmd " + s)
PIG = "{NoAI:1b,Silent:1b,PersistenceRequired:1b}"
ROWS = []
def row(z): ROWS.append(z)
# A: piston east pushes slime block, pig on it
c(f"setblock 3 {Y} 0 piston[facing=east]"); c(f"setblock 4 {Y} 0 slime_block"); c(f"summon pig 4.5 {Y+1} 0.5 {PIG}"); ev(2, f"setblock 3 {Y-1} 0 redstone_block"); row(0)
# B: sticky piston extended holding slime (head at 4, slime at 5), pig on slime; retract
c(f"setblock 3 {Y} 6 sticky_piston[facing=east]"); c(f"setblock 4 {Y} 6 slime_block"); ev(1, f"setblock 3 {Y-1} 6 redstone_block")
ev(8, f"summon pig 5.5 {Y+1} 6.5 {PIG}"); ev(10, f"setblock 3 {Y-1} 6 stone"); row(6)
# C: sticky retracts stone, pig on stone
c(f"setblock 3 {Y} 12 sticky_piston[facing=east]"); c(f"setblock 4 {Y} 12 stone"); ev(1, f"setblock 3 {Y-1} 12 redstone_block")
ev(8, f"summon pig 5.5 {Y+1} 12.5 {PIG}"); ev(10, f"setblock 3 {Y-1} 12 stone"); row(12)
# D: honey block pushed east, pig on it
c(f"setblock 3 {Y} 18 piston[facing=east]"); c(f"setblock 4 {Y} 18 honey_block"); c(f"summon pig 4.5 {Y+1} 18.5 {PIG}"); ev(2, f"setblock 3 {Y-1} 18 redstone_block"); row(18)
# E: piston up pushes slime, pig on it
c(f"setblock 3 {Y} 24 piston[facing=up]"); c(f"setblock 3 {Y+1} 24 slime_block"); c(f"summon pig 3.5 {Y+2} 24.5 {PIG}"); ev(2, f"setblock 3 {Y-1} 24 redstone_block"); row(24)
# F: piston east pushes slime, a pig beside the slime in its path (x=5 cell)
c(f"setblock 3 {Y} 30 piston[facing=east]"); c(f"setblock 4 {Y} 30 slime_block"); c(f"summon pig 5.5 {Y} 30.5 {PIG}"); ev(2, f"setblock 3 {Y-1} 30 redstone_block"); row(30)
# G: piston east pushes a stone, a pig on a block next to it that is not pushed (control, no motion)
c(f"setblock 3 {Y} 36 piston[facing=east]"); c(f"setblock 4 {Y} 36 stone"); c(f"summon pig 4.5 {Y+1} 37.5 {PIG}"); ev(2, f"setblock 3 {Y-1} 36 redstone_block"); row(36)
def snap():
    L.append("log sents minecraft: 90")
    for z in ROWS:
        L.append(f"log states 3 {Y-1} {z} 7 {Y+3} {z}")
L += ["wait 10"]; snap()
for t in range(1, 21):
    for line in EV.get(t, []): L.append(line)
    L += ["cmd tick step 1", "wait 4"]; snap()
L.append("cmd tick unfreeze")
open("E:/mc-reference/scripts/pushent2.txt", "w").write("\n".join(L) + "\n")
