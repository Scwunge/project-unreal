# Writes rs_mov.txt: what happens while a block is mid-push (the moving piston block). Entities (pig, item) on top of, in front of and inside a pushed
# block, lifted by a piston facing up; an observer watching the pushed cell and the cell it moves into; a second piston told to push into the
# moving cell at 0/1/2 ticks after the first; a sticky pull with a second piston.
Y = -60
L = ["cmd gamerule send_command_feedback false", "cmd gamemode creative", "cmd tp @s 20 -57 8", "wait 40", "cmd tick freeze",
     "cmd kill @e[type=!player]",
     "cmd fill -4 -60 -3 26 -55 90 air", "cmd fill -4 -61 -3 26 -61 90 stone", "cmd gamerule do_mob_spawning false", "wait 10"]
def c(s): L.append("cmd " + s)
EV = {}
def ev(t, s): EV.setdefault(t, []).append(s)
n = 0
def z0(): return 4 * n
# entity rows: A piston pushes a stone east; the entity stands on it, in front of it, or (item) is dropped on the next cell
for nm, setup in (("pig_top", lambda z: c(f"summon pig 3.5 {Y+1} {z+0.5} {{NoAI:1b,Invulnerable:1b,PersistenceRequired:1b}}")),
                  ("pig_front", lambda z: c(f"summon pig 4.5 {Y} {z+0.5} {{NoAI:1b,Invulnerable:1b,PersistenceRequired:1b}}")),
                  ("item_top", lambda z: c(f"summon item 3.5 {Y+1} {z+0.5} {{Item:{{id:\"minecraft:stone\",count:1}},PickupDelay:32767s,Age:-32768s}}")),
                  ("item_front", lambda z: c(f"summon item 4.5 {Y} {z+0.5} {{Item:{{id:\"minecraft:stone\",count:1}},PickupDelay:32767s,Age:-32768s}}"))):
    z = z0()
    c(f"setblock 2 {Y} {z} piston[facing=east]"); c(f"setblock 3 {Y} {z} stone"); setup(z)
    ev(2, f"setblock 1 {Y} {z} redstone_block"); ev(14, f"setblock 1 {Y} {z} air"); n += 1
# up-facing piston lifts a stone with a pig / item on it
for nm, ent in (("up_pig", "pig 2.5 {y} {z} {{NoAI:1b,Invulnerable:1b,PersistenceRequired:1b}}"),
                ("up_item", "item 2.5 {y} {z} {{Item:{{id:\"minecraft:stone\",count:1}},PickupDelay:32767s,Age:-32768s}}")):
    z = z0()
    c(f"setblock 2 {Y} {z} piston[facing=up]"); c(f"setblock 2 {Y+1} {z} stone"); c("summon " + ent.format(y=Y + 2, z=z + 0.5))
    ev(2, f"setblock 2 {Y-1} {z} redstone_block"); n += 1   # (the floor under it is replaced by the block)
# observer watching the cell the stone leaves, and one watching the cell it enters
for nm, ob in (("ob_from", (3, 1, "down")), ("ob_into", (4, 1, "down"))):
    z = z0()
    c(f"setblock 2 {Y} {z} piston[facing=east]"); c(f"setblock 3 {Y} {z} stone")
    c(f"setblock {ob[0]} {Y+ob[1]} {z} observer[facing={ob[2]}]")
    ev(2, f"setblock 1 {Y} {z} redstone_block"); ev(14, f"setblock 1 {Y} {z} air"); n += 1
# a second piston told to push into the cell the stone is moving into (pushing north from the south side), 0/1/2/3 ticks after the first
for d in (0, 1, 2, 3):
    z = z0()
    c(f"setblock 2 {Y} {z} piston[facing=east]"); c(f"setblock 3 {Y} {z} stone")
    c(f"setblock 4 {Y} {z+1} piston[facing=north]")
    ev(2, f"setblock 1 {Y} {z} redstone_block"); ev(2 + d, f"setblock 4 {Y} {z+2} redstone_block")
    ev(14, f"setblock 1 {Y} {z} air"); ev(14, f"setblock 4 {Y} {z+2} air"); n += 1
# a second piston pushing into the cell the stone leaves
for d in (0, 1, 2):
    z = z0()
    c(f"setblock 2 {Y} {z} piston[facing=east]"); c(f"setblock 3 {Y} {z} stone")
    c(f"setblock 3 {Y} {z+1} piston[facing=north]")
    ev(2, f"setblock 1 {Y} {z} redstone_block"); ev(2 + d, f"setblock 3 {Y} {z+2} redstone_block")
    ev(14, f"setblock 1 {Y} {z} air"); ev(14, f"setblock 3 {Y} {z+2} air"); n += 1
# sticky pull: the pulled block's cell while it moves back, pushed into from the side
for d in (0, 1, 2):
    z = z0()
    c(f"setblock 2 {Y} {z} sticky_piston[facing=east,extended=true]"); c(f"setblock 3 {Y} {z} piston_head[facing=east,type=sticky]"); c(f"setblock 4 {Y} {z} stone")
    c(f"setblock 3 {Y} {z+1} piston[facing=north]"); c(f"setblock 1 {Y} {z} redstone_block")
    ev(2, f"setblock 1 {Y} {z} air"); ev(2 + d, f"setblock 3 {Y} {z+2} redstone_block"); ev(14, f"setblock 3 {Y} {z+2} air"); n += 1
BOX = f"-1 {Y} -2 8 {Y+2} {4 * n}"
L += ["wait 10", f"log states {BOX}", "log sents minecraft 460"]
for t in range(1, 41):
    L += ["cmd " + s for s in EV.get(t, [])]
    L += ["cmd tick step 1", "wait 4", f"log states {BOX}"]
L.append("cmd tick unfreeze")
open("rs_mov.txt", "w").write("\n".join(L) + "\n")
print(n, "rows")
