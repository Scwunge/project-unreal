# Writes rs_world.txt, the M5 exit scene: one redstone test world, built the same way in the reference client and in
# mc-unreal's McRefScript test, with every block state logged each tick for 600 ticks. Regions sit 16 blocks apart in x.
# Run from this folder, then copy rs_world.txt to E:/mc-reference/scripts/ and run it with ref.sh.
T_END = 600
BOX = "-1 -60 -1 130 -55 10"
W = []                      # setup lines
EV = {}                     # tick -> command lines issued just before that tick is stepped
POST = []                   # lines put in after the setup has settled
def at(t, *c): EV.setdefault(t, []).extend(f"cmd {x}" for x in c)
def put(ox, x, y, z, b): W.append(f"cmd setblock {ox+x} {y} {z} {b}")
def fill(ox, x1, y1, z1, x2, y2, z2, b): W.append(f"cmd fill {ox+x1} {y1} {z1} {ox+x2} {y2} {z2} {b}")
Y = -60

# 0: a ring of four repeaters (delay 2) and four dust, one pulse
o = 0
for x, z in ((0, 0), (2, 0), (2, 2), (0, 2)): put(o, x, Y, z, "redstone_wire")
put(o, 1, Y, 0, "repeater[delay=2,facing=west]"); put(o, 2, Y, 1, "repeater[delay=2,facing=north]")
put(o, 1, Y, 2, "repeater[delay=2,facing=east]")
POST.append(f"cmd setblock {o} {Y} 1 repeater[delay=2,facing=south]")   # closes the ring once everything has settled
at(6, f"setblock {o} {Y} -1 redstone_block"); at(8, f"setblock {o} {Y} -1 air")
# a delay 1 and a delay 4 repeater into a lamp
put(o, 5, Y, 0, "repeater[delay=1,facing=east]"); put(o, 6, Y, 0, "repeater[delay=4,facing=east]"); put(o, 7, Y, 0, "redstone_lamp")
at(20, f"setblock {o+4} {Y} 0 redstone_block"); at(60, f"setblock {o+4} {Y} 0 air")

# 1: comparator, rear 14 and side 12, compare then subtract then off
o = 16
put(o, 3, Y, 0, "redstone_wire"); put(o, 4, Y, 0, "redstone_wire"); put(o, 5, Y, 0, "comparator[facing=west,mode=compare]"); put(o, 6, Y, 0, "redstone_wire")
fill(o, 5, Y, 1, 5, Y, 4, "redstone_wire"); put(o, 5, Y, 5, "redstone_block")
at(12, f"setblock {o+2} {Y} 0 redstone_block"); at(30, f"setblock {o+5} {Y} 0 comparator[facing=west,mode=subtract]")
at(50, f"setblock {o+2} {Y} 0 air")

# 2: a chain of six observers, lamp at the end; a block put in front of the first and taken away
o = 32
for x in range(1, 7): put(o, x, Y, 0, "observer[facing=east]")
put(o, 0, Y, 0, "redstone_lamp")
at(10, f"setblock {o+7} {Y} 0 stone"); at(40, f"setblock {o+7} {Y} 0 air")

# 3: sticky pistons pulling cobblestone on pulses of 1, 2, 3 and 16 ticks
o = 48
for i, (z, n) in enumerate(((0, 1), (3, 2), (6, 3), (9, 16))):
    put(o, 1, Y, z, "sticky_piston[facing=east]"); put(o, 2, Y, z, "cobblestone")
    at(10 + 40 * i, f"setblock {o+1} {Y} {z+1} redstone_block"); at(10 + 40 * i + n, f"setblock {o+1} {Y} {z+1} air")

# 4: plain pistons on 1-tick pulses started on even and odd ticks, a same-tick place and remove, a 2-tick pulse
o = 64
for i, z in enumerate((0, 3, 6, 9)):
    put(o, 1, Y, z, "piston[facing=east]"); put(o, 2, Y, z, "cobblestone")
at(10, f"setblock {o+1} {Y} 1 redstone_block"); at(11, f"setblock {o+1} {Y} 1 air")
at(40, f"setblock {o+1} {Y} 4 redstone_block"); at(41, f"setblock {o+1} {Y} 4 air")
at(70, f"setblock {o+1} {Y} 7 redstone_block", f"setblock {o+1} {Y} 7 air")
at(100, f"setblock {o+1} {Y} 10 redstone_block"); at(102, f"setblock {o+1} {Y} 10 air")

# 5: quasi-connectivity
o = 80
put(o, 1, Y, 0, "piston[facing=east]"); put(o, 2, Y, 0, "cobblestone")
at(10, f"setblock {o+1} {Y+1} 1 redstone_block"); at(18, f"setblock {o+1} {Y} -1 stone"); at(30, f"setblock {o+1} {Y+1} 1 air")
at(36, f"setblock {o+1} {Y} -1 air")

# 6: daylight detectors, normal and inverted, as the clock is set. Below ground the sky gives them nothing, so they stand
# high up, logged in a second box.
o = 96
BOX2 = f"{o-1} 200 -1 {o+5} 203 1"
W.append(f"cmd tp @s {o+2} 205 0")
W.append("wait 40")
W.append(f"cmd fill {o-2} 200 -2 {o+6} 319 2 air")
W.append(f"cmd fill {o-2} 200 -2 {o+6} 200 2 stone")
put(o, 0, 201, 0, "daylight_detector[inverted=false,power=0]"); put(o, 2, 201, 0, "daylight_detector[inverted=true,power=0]")
# (a detector with a block over it reads the light that reaches the cell, 14 beside open sky: not modelled, so none here)
W.append("cmd time set 6000")
for i, t in enumerate((6000, 0, 12500, 13500, 18000, 23000, 1000)): at(100 + 50 * i, f"time set {t}")

# 7: sugar cane and an observer harvester: cane grows (by command), the observer fires, the piston breaks it
o = 112
put(o, 3, Y, 0, "sand"); put(o, 3, Y + 1, 0, "sugar_cane[age=0]")
put(o, 2, Y + 2, 0, "observer[facing=east]"); put(o, 1, Y + 2, 0, "stone"); put(o, 1, Y + 1, 0, "piston[facing=east]"); put(o, 2, Y + 1, 0, "cobblestone")
at(30, f"setblock {o+3} {Y+2} 0 sugar_cane[age=0]"); at(80, f"setblock {o+3} {Y+2} 0 sugar_cane[age=0]")
POST += [f"cmd setblock {o+3} {Y+1} 0 sugar_cane[age=0]", f"cmd setblock {o+2} {Y+1} 0 cobblestone"]
# kelp in a walled water column, an observer in its wall, a lamp behind it
fill(o, 7, Y, 3, 9, Y + 4, 5, "stone"); fill(o, 8, Y, 4, 8, Y + 3, 4, "water")
put(o, 8, Y, 4, "kelp[age=1]"); put(o, 8, Y + 1, 4, "kelp[age=1]")
put(o, 7, Y + 2, 4, "observer[facing=east]"); put(o, 6, Y + 2, 4, "redstone_lamp")
at(40, f"setblock {o+8} {Y+2} 4 kelp[age=1]"); at(90, f"setblock {o+8} {Y+3} 4 kelp[age=1]")

def main():
    L = ["cmd gamerule send_command_feedback false", "cmd gamemode spectator", "cmd tp @s 60 -45 4", "wait 40",
         "cmd tick freeze", "cmd weather clear 1000000"]
    for x in range(-2, 132, 20): L.append(f"cmd fill {x} -60 -2 {x+19} -50 12 air")
    L.append("wait 6")
    # the sea outside floods the area: wall it in
    for a in ("-3 -62 -3 134 -49 -3", "-3 -62 13 134 -49 13", "-3 -62 -3 -3 -49 13", "134 -62 -3 134 -49 13", "-3 -49 -3 134 -49 13"): L.append(f"cmd fill {a} stone")
    L.append("wait 6")
    for x in range(-2, 132, 20): L.append(f"cmd fill {x} -61 -2 {x+19} -61 12 stone")
    L.append("wait 6")
    for i, w in enumerate(W):
        L.append(w)
        if i % 12 == 11: L.append("wait 3")
    # Blocks set by command send their neighbours updates (observers fire as their neighbours are placed): let that settle,
    # put back what it spent, and settle again before the first sample.
    L += ["wait 40", "cmd tick step 60", "wait 10"]
    for i, c in enumerate(POST):
        L.append(c)
        if i % 12 == 11: L.append("wait 3")
    # (the last 12 ticks of the settling are sampled too)
    L += ["wait 5", "cmd tick step 4", "wait 8", f"log states {BOX}", f"log states {BOX2}"]
    for _ in range(12): L += ["cmd tick step 1", "wait 4", f"log states {BOX}", f"log states {BOX2}"]
    for t in range(1, T_END + 1):
        for c in EV.get(t, []): L.append(c)
        L += ["cmd tick step 1", "wait 4", f"log states {BOX}", f"log states {BOX2}"]
    L.append("cmd tick unfreeze")
    open("rs_world.txt", "w").write("\n".join(L) + "\n")
main()
