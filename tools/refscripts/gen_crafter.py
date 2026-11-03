# Writes rs_crafter.txt: crafters (power pulse, craft delay, what comes out, full and empty, no recipe), and the copper bulb beside dust,
# a lever and a repeater (what counts as power for it).
Y = -60
L = ["cmd gamerule send_command_feedback false", "cmd gamemode creative", "cmd tp @s 20 -57 8", "wait 40", "cmd tick freeze",
     "cmd fill -4 -60 -3 20 -55 60 air", "cmd fill -4 -61 -3 20 -61 60 stone", "cmd gamerule do_mob_spawning false", "wait 10"]
def c(s): L.append("cmd " + s)
EV = {}
def ev(t, s): EV.setdefault(t, []).append(s)
def crafter(z, name, items, orient="north_up"):
    c(f"setblock 2 {Y} {z} crafter[orientation={orient},crafting=false,triggered=false]")
    for slot, it in items: c(f"item replace block 2 {Y} {z} container.{slot} with minecraft:{it[0]} {it[1]}")
    c(f"setblock 1 {Y} {z} comparator[facing=west]"); c(f"setblock 0 {Y} {z} redstone_wire")
ROW = {}
def row(n, name, items, orient="north_up"):
    ROW[name] = 4 * n; crafter(4 * n, name, items, orient)
def on(t, name, dx=3): ev(t, f"setblock {dx} {Y} {ROW[name]} redstone_block")
def off(t, name, dx=3): ev(t, f"setblock {dx} {Y} {ROW[name]} air")
row(0, "table", [(s, ("oak_planks", 2)) for s in (0, 1, 3, 4)])
for a, b in ((2, 3), (20, 30), (40, 41), (60, 61)): on(a, "table"); off(b, "table")
row(1, "norecipe", [(s, ("stone", 1)) for s in range(9)])
on(2, "norecipe"); off(3, "norecipe")
row(2, "chest", [(s, ("oak_planks", 1)) for s in (0, 1, 2, 3, 5, 6, 7, 8)])
on(2, "chest"); off(3, "chest")
row(3, "empty", [])
on(2, "empty"); off(3, "empty")
row(4, "ingots", [(s, ("iron_ingot", 1)) for s in range(9)])
on(2, "ingots"); off(3, "ingots")
row(5, "hold", [(s, ("oak_planks", 2)) for s in (0, 1, 3, 4)])
on(2, "hold"); off(40, "hold"); on(41, "hold")
row(6, "down", [(s, ("oak_planks", 2)) for s in (0, 1, 3, 4)], "down_north")
on(2, "down"); off(3, "down")
row(7, "buckets", [(0, ("water_bucket", 1)), (1, ("oak_planks", 1)), (3, ("oak_planks", 1)), (4, ("oak_planks", 1))])
on(2, "buckets"); off(3, "buckets")
# bulb beside dust (powered), a lever, a repeater, a lone dust that does not point at it
BULB = 40
for i, kind in enumerate(["dustpoint", "dustside", "lever", "repeater", "powerblock"]):
    z = BULB + 4 * i; ROW["b_" + kind] = z
    c(f"setblock 2 {Y} {z} copper_bulb")
    if kind == "dustpoint": c(f"setblock 3 {Y} {z} redstone_wire"); c(f"setblock 4 {Y} {z} redstone_wire"); ev(2, f"setblock 5 {Y} {z} redstone_block")
    if kind == "dustside": c(f"setblock 3 {Y} {z} redstone_wire"); c(f"setblock 3 {Y} {z+1} redstone_wire"); ev(2, f"setblock 3 {Y} {z+2} redstone_block")
    if kind == "lever": ev(2, f"setblock 3 {Y} {z} lever[face=wall,facing=west,powered=true]")
    if kind == "repeater": c(f"setblock 3 {Y} {z} repeater[facing=west,delay=1]"); ev(2, f"setblock 4 {Y} {z} redstone_block")
    if kind == "powerblock": c(f"setblock 3 {Y} {z} stone"); ev(2, f"setblock 4 {Y} {z} redstone_block")
BOX = f"-1 {Y} 0 6 {Y} 60"
BOXI = f"-1 {Y} -3 12 {Y+3} 40"
L += ["wait 10", f"log states {BOX}", f"log items {BOXI}"]
for t in range(1, 71):
    L += ["cmd " + s for s in EV.get(t, [])]
    L += ["cmd tick step 1", "wait 4", f"log states {BOX}", f"log items {BOXI}"]
L.append("cmd tick unfreeze")
open("rs_crafter.txt", "w").write("\n".join(L) + "\n")
