# Writes lg_trip2.txt: tripwire lengths, placement and shears, one row of the plane y=-60 per case, the whole plane logged
# every tick. Run through ref.sh (it needs the "break" verb, which breaks a block as the creative player does).
Y = -60
ROWS = []          # (name, builder)
L = ["cmd gamerule send_command_feedback false", "cmd gamemode creative", "cmd tp @s 20 -57 8", "wait 40", "cmd tick freeze",
     "cmd fill -4 -62 -3 48 -49 62 air", "cmd fill -4 -61 -3 48 -61 62 stone", "cmd give @s shears", "wait 10"]
def c(s): L.append("cmd " + s)
def row(k): return 3 * k
def pair(z, n, x0=0, tail=False):
    # hooks at x0 (facing east) and x0+n+1 (facing west), n strings between, stone behind each hook
    c(f"setblock {x0-1} {Y} {z} stone"); c(f"setblock {x0+n+2} {Y} {z} stone")
    for x in range(x0 + 1, x0 + n + 1): c(f"setblock {x} {Y} {z} tripwire")
    c(f"setblock {x0} {Y} {z} tripwire_hook[facing=east]"); c(f"setblock {x0+n+1} {Y} {z} tripwire_hook[facing=west]")
k = 0
cases = {}
for n in (1, 38, 39, 40):                      # length limit: n strings between the hooks
    cases[f"len{n}"] = row(k); pair(row(k), n); k += 1
# no partner: one hook and string, no second hook
cases["alone"] = row(k); z = row(k); k += 1
c(f"setblock -1 {Y} {z} stone"); c(f"setblock 0 {Y} {z} tripwire_hook[facing=east]")
for x in (1, 2, 3): c(f"setblock {x} {Y} {z} tripwire")
# partner facing the same way (east, east): no attach
cases["same"] = row(k); z = row(k); k += 1
c(f"setblock -1 {Y} {z} stone"); c(f"setblock 5 {Y} {z} stone")
c(f"setblock 0 {Y} {z} tripwire_hook[facing=east]"); c(f"setblock 4 {Y} {z} tripwire_hook[facing=east]")
for x in (1, 2, 3): c(f"setblock {x} {Y} {z} tripwire")
# a gap in the string
cases["gap"] = row(k); z = row(k); k += 1
pair(z, 4); c(f"setblock 3 {Y} {z} air")
# hook support removed after pairing, and a hook placed with nothing behind it
cases["nosupport"] = row(k); z = row(k); k += 1
pair(z, 3)
cases["nowall"] = row(k); z = row(k); k += 1
for x in (1, 2, 3): c(f"setblock {x} {Y} {z} tripwire")
c(f"setblock 0 {Y} {z} tripwire_hook[facing=east]"); c(f"setblock 4 {Y} {z} tripwire_hook[facing=west]")
# strings before hooks, hooks before strings
cases["hooksfirst"] = row(k); z = row(k); k += 1
c(f"setblock -1 {Y} {z} stone"); c(f"setblock 5 {Y} {z} stone")
c(f"setblock 0 {Y} {z} tripwire_hook[facing=east]"); c(f"setblock 4 {Y} {z} tripwire_hook[facing=west]")
# shears, no shears: two pairs of length 3, broken at x=2 by a creative player
cases["shears"] = row(k); pair(row(k), 3); k += 1
cases["nopick"] = row(k); pair(row(k), 3); k += 1
cases["shearsend"] = row(k); pair(row(k), 3); k += 1     # shears break the string next to a hook
cases["shearspost"] = row(k); pair(row(k), 3); k += 1    # shears break a hook
BOX = f"-1 {Y} 0 44 {Y} {row(k)}"
L += ["wait 10", f"log states {BOX}"]
EV = {}
def ev(t, s): EV.setdefault(t, []).append(s)
# t=4: items into the first string of every row that has one (summon at the string)
for name, z in cases.items():
    if name.startswith("len"): ev(4, f'summon item 1.5 {Y} {z+0.5} {{Item:{{id:"minecraft:stone",count:1}},PickupDelay:32767}}')
    if name in ("alone", "same", "gap"): ev(4, f'summon item 1.5 {Y} {z+0.5} {{Item:{{id:"minecraft:stone",count:1}},PickupDelay:32767}}')
ev(40, "kill @e[type=item]")
# t=60: support removed behind the hook on the nosupport row; a late second hook on the hooksfirst row's strings
ev(60, f"setblock -1 {Y} {cases['nosupport']} air")
for x in (1, 2, 3): ev(60, f"setblock {x} {Y} {cases['hooksfirst']} tripwire")
# t=80..: the shears cases, one after another (the player stands beside each break)
def brk(t, name, x, tool):
    z = cases[name]
    ev(t, f"tp @s {x} {Y+2} {z+2}")
    ev(t + 1, "clear @s" if tool == "none" else "give @s shears")
    ev(t + 2, f"BREAK {x} {Y} {z}")
brk(80, "shears", 2, "shears"); brk(90, "nopick", 2, "none")
brk(100, "shearsend", 1, "shears"); brk(110, "shearspost", 0, "shears")
steps = 135
out = L[:]
for t in range(1, steps + 1):
    for s in EV.get(t, []):
        if s.startswith("BREAK "): out.append("break " + s[6:])
        else: out.append("cmd " + s)
    out += ["cmd tick step 1", "wait 4", f"log states {BOX}"]
out.append("cmd tick unfreeze")
open("lg_trip2.txt", "w").write("\n".join(out) + "\n")
