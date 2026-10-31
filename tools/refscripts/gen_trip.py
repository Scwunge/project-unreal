# Writes the tripwire and target-block reference scripts to E:/mc-reference/scripts (run through ref.sh).
OUT = "E:/mc-reference/scripts/"
PRE = ["cmd gamerule send_command_feedback false", "cmd gamemode spectator", "cmd tp @s 5 -50 2", "wait 40",
       "cmd tick freeze", "cmd fill -2 -60 -2 14 -46 8 air", "cmd fill -2 -61 -2 14 -61 8 stone", "wait 10"]
def cmd(c): return [f"cmd {c}"]
def step(box, n):
    out = []
    for _ in range(n):
        out += ["cmd tick step 1", "wait 4", f"log states {box}"]
    return out

# Tripwire: hooks at x=0 (facing east) and x=5 (facing west), string between; then an item lies in the string and is
# removed; then a string block is broken, and another replaced by a disarmed one (what shears leave).
B = "-1 -60 0 6 -60 0"
L = cmd("setblock -1 -60 0 stone") + cmd("setblock 6 -60 0 stone")
for x in range(1, 5): L += cmd(f"setblock {x} -60 0 tripwire")
L += cmd("setblock 0 -60 0 tripwire_hook[facing=east]") + cmd("setblock 5 -60 0 tripwire_hook[facing=west]")
L += step(B, 6)
L += cmd("summon item 2.5 -60 0.5 {Item:{id:\"minecraft:stone\",count:1},PickupDelay:32767}") + step(B, 25)
L += cmd("kill @e[type=item]") + step(B, 25)
L += cmd("setblock 3 -60 0 air") + step(B, 6)
L += cmd("setblock 3 -60 0 tripwire") + step(B, 6)
L += cmd("setblock 3 -60 0 tripwire[disarmed=true]") + step(B, 6)
L += cmd("summon item 3.5 -60 0.5 {Item:{id:\"minecraft:stone\",count:1},PickupDelay:32767}") + step(B, 25)
open(OUT + "rs_tripwire.txt", "w").write("\n".join(PRE + L + cmd("kill @e[type=item]") + cmd("tick unfreeze")) + "\n")

# Target blocks: a 7x7 grid, one block each at x=2i, y=-60+2j on the plane z=0; an arrow flies at each from z=1.6
# towards -z, so it meets the face at (x0+u, y0+v) with no time to drop. The power is read every tick.
vals = [0.02, 0.2, 0.4, 0.5, 0.6, 0.8, 0.98]
B = "0 -60 0 14 -46 0"
L = []
for i in range(7):
    for j in range(7): L += cmd(f"setblock {2*i} {-60+2*j} 0 target")
for i in range(7):
    for j in range(7):
        L += cmd(f"summon arrow {2*i+vals[i]} {-60+2*j+vals[j]} 1.6 {{Motion:[0.0,0.0,-3.0]}}")
L += step(B, 30)
open(OUT + "rs_target.txt", "w").write("\n".join(PRE + L + cmd("kill @e[type=arrow]") + cmd("tick unfreeze")) + "\n")
