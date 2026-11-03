# Writes rs_sculk.txt: sculk sensors by distance and event type (rows 10 blocks apart, each with its own source at x=0 and sensors
# at x=2..10), a wool shield, and a calibrated sculk sensor ladder (rows 2 apart, dust of 15 - L + 1 at the back, listening for that frequency).
Y = -60
L = ["cmd gamerule send_command_feedback false", "cmd gamemode creative", "cmd tp @s 0.5 -60 0.5", "wait 40", "cmd tick freeze",
     "cmd fill -4 -60 -3 24 -55 130 air", "cmd fill -4 -61 -3 24 -61 130 stone", "cmd gamerule do_mob_spawning false", "wait 10"]
def c(s): L.append("cmd " + s)
TYPES = ["break", "place", "button", "lever", "door", "chest", "wool"]
ROW = {t: 10 * i for i, t in enumerate(TYPES)}
for t, z in ROW.items():
    for x in range(2, 11): c(f"setblock {x} {Y} {z} sculk_sensor")
    src = {"break": "stone", "place": "air", "button": "stone_button[face=floor,facing=north]", "lever": "lever[face=floor,facing=north]",
           "door": "oak_door[half=lower,facing=north,hinge=left,open=false]", "chest": "chest[facing=north]", "wool": "stone"}[t]
    if src != "air": c(f"setblock 0 {Y} {z} {src}")
    if t == "door": c(f"setblock 0 {Y+1} {z} oak_door[half=upper,facing=north,hinge=left,open=false]")
    if t == "wool":
        for dz in (-1, 0, 1): c(f"setblock 1 {Y} {z+dz} white_wool")
CAL = 80
for k in range(15):                      # row k listens for frequency p = 1 + k: dust chain of L = 16 - p, source block at its end
    p = 1 + k; z = CAL + 2 * k; Ln = 16 - p
    c(f"setblock 3 {Y} {z} calibrated_sculk_sensor[facing=west]")
    for x in range(4, 4 + Ln): c(f"setblock {x} {Y} {z} redstone_wire")
    c(f"setblock {4+Ln} {Y} {z} redstone_block")
CEN = CAL + 14
c(f"setblock 0 {Y} {CEN} stone")
BOXES = [f"2 {Y} {ROW[t]} 10 {Y} {ROW[t]}" for t in TYPES] + [f"3 {Y} {CAL+2*k} 3 {Y} {CAL+2*k}" for k in range(15)]
def log():
    for b in BOXES: L.append("log states " + b)
EV = {}
def ev(t, s): EV.setdefault(t, []).append(s)
def hit(t, kind, z):
    ev(t - 1, f"cmd tp @s 0.5 {Y} {z-2}.5".replace("}", ""))
    if kind == "break": ev(t, f"break 0 {Y} {z}")
    elif kind == "place": ev(t - 1, "cmd give @s stone 4"); ev(t - 1, "slot 0"); ev(t, f"use_on 0 {Y-1} {z} up")
    else: ev(t, f"press 0 {Y} {z}")
t0 = 5
for i, ty in enumerate(TYPES): hit(t0 + 70 * i, "break" if ty == "wool" else ty, ROW[ty])
# the calibrated ladder: the same five event kinds at its centre
base = t0 + 70 * len(TYPES) + 10
for j, ty in enumerate(["break", "place", "button", "lever", "door", "chest"]):
    z = CEN
    if ty == "button": ev(base + 70 * j - 3, f"cmd setblock 0 {Y} {z} stone_button[face=floor,facing=north]")
    if ty == "lever": ev(base + 70 * j - 3, f"cmd setblock 0 {Y} {z} lever[face=floor,facing=north]")
    if ty == "door": ev(base + 70 * j - 3, f"cmd setblock 0 {Y} {z} oak_door[half=lower,facing=north,hinge=left,open=false]"); ev(base + 70 * j - 3, f"cmd setblock 0 {Y+1} {z} oak_door[half=upper,facing=north,hinge=left,open=false]")
    if ty == "chest": ev(base + 70 * j - 3, f"cmd setblock 0 {Y} {z} chest[facing=north]")
    if ty in ("break", "place"): ev(base + 70 * j - 3, f"cmd setblock 0 {Y} {z} stone" if ty == "break" else f"cmd setblock 0 {Y} {z} air")
    hit(base + 70 * j, ty, z)
    ev(base + 70 * j + 3, f"cmd fill 0 {Y} {z} 0 {Y+1} {z} air")
END = base + 70 * 6 + 5
log()
for t in range(1, END):
    for s in EV.get(t, []):
        L.append(s if not s.startswith(("cmd ", "break ", "slot ", "use_on ", "press ")) else s)
    L += ["cmd tick step 1", "wait 4"]; log()
L.append("cmd tick unfreeze")
open("rs_sculk.txt", "w").write("\n".join(L) + "\n")
