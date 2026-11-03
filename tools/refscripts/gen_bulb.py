# Writes rs_bulb.txt: copper bulbs (toggle on a rising edge of power, quasi-connectivity, oxidation variants, what a comparator reads).
# Row z = 4 * n: bulb at x=2, power block at x=3, comparator at x=1 facing west reading it, dust at x=0 showing the strength.
Y = -60
NAMES = ["copper_bulb", "exposed_copper_bulb", "weathered_copper_bulb", "oxidized_copper_bulb", "waxed_copper_bulb"]
L = ["cmd gamerule send_command_feedback false", "cmd gamemode creative", "cmd tp @s 20 -57 8", "wait 40", "cmd tick freeze",
     "cmd fill -4 -60 -3 20 -55 80 air", "cmd fill -4 -61 -3 20 -61 80 stone", "wait 10"]
def c(s): L.append("cmd " + s)
EV = {}
def ev(t, s): EV.setdefault(t, []).append(s)
ROWS = {}
def row(n, name, bulb="copper_bulb", bulb_at_start=True):
    ROWS[name] = n; z = 4 * n
    if bulb_at_start: c(f"setblock 2 {Y} {z} {bulb}")
    c(f"setblock 1 {Y} {z} comparator[facing=west]"); c(f"setblock 0 {Y} {z} redstone_wire")
    return z
def on(t, name, dx=3, dy=0): ev(t, f"setblock {dx} {Y+dy} {4*ROWS[name]} redstone_block")
def off(t, name, dx=3, dy=0): ev(t, f"setblock {dx} {Y+dy} {4*ROWS[name]} air")
row(0, "pulse")
for a, b in ((2, 3), (10, 12), (20, 30), (40, 41), (50, 51), (52, 53)): on(a, "pulse"); off(b, "pulse")
row(1, "hold")
on(2, "hold"); off(30, "hold"); on(31, "hold"); off(40, "hold"); on(41, "hold")
row(2, "quasi")
on(2, "quasi", 2, 2); off(10, "quasi", 2, 2); on(20, "quasi", 2, 1); off(30, "quasi", 2, 1)
row(3, "placed", bulb_at_start=False)
c(f"setblock 3 {Y} {4*3} redstone_block")
ev(2, f"setblock 2 {Y} {4*3} copper_bulb"); off(10, "placed"); on(12, "placed")
for i, nm in enumerate(NAMES[1:]):
    row(4 + i, nm, nm)
    for a, b in ((2, 4), (6, 8)): on(a, nm); off(b, nm)
row(8, "litstart", "copper_bulb[lit=true,powered=false]")
on(2, "litstart"); off(4, "litstart")
row(9, "stalepowered", "copper_bulb[lit=false,powered=true]")
on(6, "stalepowered"); off(10, "stalepowered")
row(10, "stalelit", "copper_bulb[lit=true,powered=true]")
off(2, "stalelit")
BOX = f"-1 {Y} 0 4 {Y} 40"
L += ["wait 10", f"log states {BOX}"]
for t in range(1, 61):
    L += ["cmd " + s for s in EV.get(t, [])]
    L += ["cmd tick step 1", "wait 4", f"log states {BOX}"]
L.append("cmd tick unfreeze")
open("rs_bulb.txt", "w").write("\n".join(L) + "\n")
