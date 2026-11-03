# Writes rs_trip4.txt: what a tripwire does when it is already attached and shears break it, when a player places the hooks,
# and when a pig or an arrow is in the string. Survival player; needs the "use_on X Y Z dir" and "break X Y Z" verbs.
Y = -60
L = ["cmd gamerule send_command_feedback false", "cmd gamemode survival", "cmd tp @s 20 -57 8", "wait 40", "cmd tick freeze",
     "cmd fill -4 -60 -3 20 -55 40 air", "cmd fill -4 -61 -3 20 -61 40 stone", "cmd gamerule do_mob_spawning false", "wait 10"]
def c(s): L.append("cmd " + s)
def pair(z, n=3):
    c(f"setblock -1 {Y} {z} stone"); c(f"setblock {n+2} {Y} {z} stone")
    for x in range(1, n + 1): c(f"setblock {x} {Y} {z} tripwire")
    c(f"setblock 0 {Y} {z} tripwire_hook[facing=east]"); c(f"setblock {n+1} {Y} {z} tripwire_hook[facing=west]")
ROW = {"shearsatt": 0, "pickatt": 3, "playerhooks": 6, "pig": 9, "arrow": 12, "playerstr": 18}
pair(0); pair(3); pair(9); pair(12)
z = ROW["playerstr"]
c(f"setblock -1 {Y} {z} stone"); c(f"setblock 5 {Y} {z} stone"); c(f"setblock 0 {Y} {z} tripwire_hook[facing=east]"); c(f"setblock 4 {Y} {z} tripwire_hook[facing=west]")
# playerhooks: strings and stones only; the player puts the hooks on
z = ROW["playerhooks"]
c(f"setblock -1 {Y} {z} stone"); c(f"setblock 5 {Y} {z} stone")
for x in (1, 2, 3): c(f"setblock {x} {Y} {z} tripwire")
BOX = f"-1 {Y} 0 6 {Y} 19"
L += ["wait 10", f"log states {BOX}"]
EV = {}
def ev(t, s): EV.setdefault(t, []).append(s)
# attach the two pairs first: an item in the string, gone again
for n in ("shearsatt", "pickatt"):
    ev(2, f'cmd summon item 1.5 {Y} {ROW[n]+0.5} {{Item:{{id:"minecraft:stone",count:1}},PickupDelay:32767}}')
ev(3, "cmd kill @e[type=item]")
# a pig in the string of "pig" from tick 4 until 40; an arrow lying in the string of "arrow"
ev(4, f"cmd summon pig 2.5 {Y} {ROW['pig']+0.5} {{NoAI:1b,PersistenceRequired:1b}}")
ev(4, f"cmd summon arrow 2.5 {Y} {ROW['arrow']+0.5} {{Motion:[0.0,0.0,0.0]}}")
ev(40, "cmd kill @e[type=pig]"); ev(40, "cmd kill @e[type=arrow]")
def brk(t, name, x, shears):
    z = ROW[name]
    ev(t, f"cmd tp @s {x} {Y+2} {z+2}")
    ev(t + 1, "cmd give @s shears" if shears else "cmd clear @s")
    ev(t + 2, "slot 0")
    ev(t + 3, f"break {x} {Y} {z}")
brk(60, "shearsatt", 2, True); brk(80, "pickatt", 2, False)
# the player puts the two hooks on the stones, the first with the string already there
z = ROW["playerhooks"]
ev(100, f"cmd tp @s 0.5 {Y} {z+2}"); ev(101, "cmd clear @s"); ev(102, "cmd give @s tripwire_hook 2"); ev(103, "slot 0")
ev(104, f"use_on -1 {Y} {z} east"); ev(108, f"cmd tp @s 4.5 {Y} {z+2}"); ev(110, f"use_on 5 {Y} {z} west")
# the player puts string down between hooks that are already there
zs = ROW["playerstr"]
ev(120, f"cmd tp @s 2.5 {Y} {zs+2}"); ev(121, "cmd clear @s"); ev(122, "cmd give @s string 3"); ev(123, "slot 0")
for k, x in enumerate((1, 2, 3)): ev(124 + 3 * k, f"use_on {x} {Y-1} {zs} up")
for t in range(1, 141):
    L += EV.get(t, [])
    L += ["cmd tick step 1", "wait 4", f"log states {BOX}"]
L.append("cmd tick unfreeze")
open("rs_trip4.txt", "w").write("\n".join(L) + "\n")
