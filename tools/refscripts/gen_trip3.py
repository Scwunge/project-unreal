# Writes rs_trip3.txt: longer tripwires and survival-mode breaks with and without shears (creative gave no difference).
Y = -60
L = ["cmd gamerule send_command_feedback false", "cmd gamemode survival", "cmd tp @s 20 -57 8", "wait 40", "cmd tick freeze",
     "cmd fill -4 -60 -3 48 -55 30 air", "cmd fill -4 -61 -3 48 -61 30 stone", "wait 10"]
def c(s): L.append("cmd " + s)
def pair(z, n):
    c(f"setblock -1 {Y} {z} stone"); c(f"setblock {n+2} {Y} {z} stone")
    for x in range(1, n + 1): c(f"setblock {x} {Y} {z} tripwire")
    c(f"setblock 0 {Y} {z} tripwire_hook[facing=east]"); c(f"setblock {n+1} {Y} {z} tripwire_hook[facing=west]")
cases = {"len41": 0, "len42": 3, "shears": 6, "nopick": 9, "shears2": 12}
pair(0, 41); pair(3, 42)
for n in ("shears", "nopick", "shears2"): pair(cases[n], 3)
BOX = f"-1 {Y} 0 45 {Y} 15"
L += ["wait 10", f"log states {BOX}"]
EV = {}
def ev(t, s): EV.setdefault(t, []).append(s)
for n in ("len41", "len42"): ev(4, f'cmd summon item 1.5 {Y} {cases[n]+0.5} {{Item:{{id:"minecraft:stone",count:1}},PickupDelay:32767}}')
ev(40, "cmd kill @e[type=item]")
def brk(t, name, tool):
    z = cases[name]
    ev(t, f"cmd tp @s 2 {Y+2} {z+2}")
    ev(t + 1, "cmd clear @s" if tool == "none" else "cmd give @s shears")
    ev(t + 2, "slot 0")
    ev(t + 3, f"break 2 {Y} {z}")
brk(60, "shears", "shears"); brk(80, "nopick", "none")
for t in range(1, 111):
    L += EV.get(t, [])
    L += ["cmd tick step 1", "wait 4", f"log states {BOX}"]
L.append("cmd tick unfreeze")
open("rs_trip3.txt", "w").write("\n".join(L) + "\n")
