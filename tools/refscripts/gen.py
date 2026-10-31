# Writes the redstone timing scripts. The same file drives the vanilla reference client (ref.sh) and mc-unreal's
# McRefScript test, which replays the setblock, fill and tick step lines and prints the same "states" lines.
BOX = "-1 -60 -1 12 -58 6"
PRE = ["cmd gamerule send_command_feedback false", "cmd gamemode spectator", "cmd tp @s 5 -50 2", "wait 40",
       "cmd tick freeze", "cmd fill -2 -60 -2 14 -50 8 air", "cmd fill -2 -61 -2 14 -61 8 stone", "wait 10"]

def log(): return [f"log states {BOX}"]
def setbox(b):
    global BOX
    BOX = b
def step(n=1):
    out = []
    for _ in range(n):
        out += ["cmd tick step 1", "wait 4"] + log()
    return out
def cmd(c): return [f"cmd {c}"]

def write(name, lines):
    global BOX
    open(f"rs_{name}.txt", "w").write("\n".join(PRE + lines) + "\n")

# Repeaters of delay 1 to 4 in a row, then dust and a lamp. Redstone block on, 30 ticks, off, 30 ticks.
L = []
for i, d in enumerate((1, 2, 3, 4)): L += cmd(f"setblock {1+i} -60 0 repeater[delay={d},facing=west]")
L += cmd("setblock 5 -60 0 redstone_wire") + cmd("setblock 6 -60 0 redstone_lamp")
L += step(10) + log() + cmd("setblock 0 -60 0 redstone_block") + step(30) + cmd("setblock 0 -60 0 air") + step(30)
write("repeater", L + cmd("tick unfreeze"))

# Dust beside a block, a torch on the block's far side, a lamp; a redstone block behind the dust is the switch
# (setblock on a lever would not reach the torch: only the lever's direct neighbours are told). 24 toggles two ticks
# apart then show burnout.
# (The lamp goes in first: a lamp set by command takes no account of power it was placed beside until a neighbour changes.)
L = cmd("setblock 3 -60 0 redstone_lamp") + cmd("setblock 1 -60 0 stone") + cmd("setblock 0 -60 0 redstone_wire")
L += cmd("setblock 2 -60 0 redstone_wall_torch[facing=east,lit=true]")
L += step(10) + log()
L += cmd("setblock -1 -60 0 redstone_block") + step(14)
L += cmd("setblock -1 -60 0 air") + step(14)
for i in range(24):
    L += cmd("setblock -1 -60 0 " + ("redstone_block" if i % 2 == 0 else "air")) + step(2)
L += step(60)
write("torch", L + cmd("tick unfreeze"))
# Comparator: rear 14 (block + 2 dust), side 12. Compare mode, then subtract, then the rear off.
L = cmd("setblock 3 -60 0 redstone_wire") + cmd("setblock 4 -60 0 redstone_wire")
L += cmd("setblock 5 -60 0 comparator[facing=west,mode=compare]") + cmd("setblock 6 -60 0 redstone_wire")
L += cmd("fill 5 -60 1 5 -60 4 redstone_wire") + cmd("setblock 5 -60 5 redstone_block")
L += step(10) + log() + cmd("setblock 2 -60 0 redstone_block") + step(10)
L += cmd("setblock 5 -60 0 comparator[facing=west,mode=subtract]") + step(10)
L += cmd("setblock 2 -60 0 air") + step(10)
write("comparator", L + cmd("tick unfreeze"))

# A plain and a sticky piston, each pushing cobblestone, held 16 ticks and let go.
L = cmd("setblock 1 -60 0 piston[facing=east]") + cmd("fill 2 -60 0 3 -60 0 cobblestone")
L += cmd("setblock 1 -60 3 sticky_piston[facing=east]") + cmd("fill 2 -60 3 3 -60 3 cobblestone")
L += step(5) + log() + cmd("setblock 1 -60 1 redstone_block") + cmd("setblock 1 -60 4 redstone_block") + step(16)
L += cmd("setblock 1 -60 1 air") + cmd("setblock 1 -60 4 air") + step(16)
write("piston", L + cmd("tick unfreeze"))

# An observer watching a block that is placed and removed, a lamp behind it.
L = cmd("setblock 1 -60 0 observer[facing=east]") + cmd("setblock 0 -60 0 redstone_lamp")
L += step(6) + log() + cmd("setblock 2 -60 0 stone") + step(10) + cmd("setblock 2 -60 0 air") + step(10)
write("observer", L + cmd("tick unfreeze"))

# Quasi-connectivity: a piston is also powered by power that would reach the block above it. A redstone block
# diagonally above sends no update to the piston; a block placed beside the piston then does.
L = cmd("setblock 1 -60 0 piston[facing=east]") + cmd("setblock 2 -60 0 cobblestone")
L += step(5) + cmd("setblock 1 -59 1 redstone_block") + step(8) + cmd("setblock 1 -60 -1 stone") + step(12)
L += cmd("setblock 1 -59 1 air") + step(6) + cmd("setblock 1 -60 -1 air") + step(12)
write("quasi", L + cmd("tick unfreeze"))

# Repeater locking: B (3) takes its input from (4); C (3,z1) points into B's side.
L = cmd("setblock 3 -60 0 repeater[delay=1,facing=east]") + cmd("setblock 2 -60 0 redstone_wire") + cmd("setblock 1 -60 0 redstone_lamp")
L += cmd("setblock 3 -60 1 repeater[delay=1,facing=south]")
L += step(6) + cmd("setblock 3 -60 2 redstone_block") + step(6) + cmd("setblock 4 -60 0 redstone_block") + step(8)
L += cmd("setblock 3 -60 2 air") + step(10) + cmd("setblock 3 -60 2 redstone_block") + step(8)
L += cmd("setblock 4 -60 0 air") + step(8) + cmd("setblock 3 -60 2 air") + step(12)
write("lock", L + cmd("tick unfreeze"))

# Slime and the push limit: a slime block with blocks stuck to it, 12 blocks pushed, 13 blocks not.
BOX = "-1 -60 -1 16 -58 8"
L = cmd("setblock 1 -60 0 piston[facing=east]") + cmd("setblock 2 -60 0 slime_block") + cmd("setblock 3 -60 0 cobblestone")
L += cmd("setblock 2 -60 1 cobblestone") + cmd("setblock 2 -59 0 cobblestone")
L += cmd("setblock 1 -60 4 piston[facing=east]") + cmd("fill 2 -60 4 13 -60 4 cobblestone")
L += cmd("setblock 1 -60 6 piston[facing=east]") + cmd("fill 2 -60 6 14 -60 6 cobblestone")
L += step(5) + cmd("setblock 1 -60 -1 redstone_block") + cmd("setblock 1 -60 3 redstone_block") + cmd("setblock 1 -60 7 redstone_block")
L += step(12) + cmd("setblock 1 -60 -1 air") + cmd("setblock 1 -60 3 air") + cmd("setblock 1 -60 7 air") + step(10)
write("slime", L + cmd("tick unfreeze"))
BOX = "-1 -60 -1 12 -58 6"

# Doors, trapdoor, fence gate and iron door powered by a redstone block beside each.
L = cmd("setblock 1 -60 0 oak_door[half=lower,facing=south]") + cmd("setblock 1 -59 0 oak_door[half=upper,facing=south]")
L += cmd("setblock 3 -60 0 oak_trapdoor[facing=south,half=bottom]") + cmd("setblock 5 -60 0 oak_fence_gate[facing=south]")
L += cmd("setblock 7 -60 0 iron_door[half=lower,facing=south]") + cmd("setblock 7 -59 0 iron_door[half=upper,facing=south]")
L += step(5)
for x in (1, 3, 5, 7): L += cmd(f"setblock {x} -60 1 redstone_block")
L += step(14)
for x in (1, 3, 5, 7): L += cmd(f"setblock {x} -60 1 air")
L += step(14)
write("doors", L + cmd("tick unfreeze"))

# A ring of four repeaters and four dust, given one pulse: the signal circulates.
L = cmd("fill 0 -60 0 2 -60 2 air")
L += cmd("setblock 0 -60 0 redstone_wire") + cmd("setblock 2 -60 0 redstone_wire") + cmd("setblock 2 -60 2 redstone_wire") + cmd("setblock 0 -60 2 redstone_wire")
L += cmd("setblock 1 -60 0 repeater[delay=1,facing=west]") + cmd("setblock 2 -60 1 repeater[delay=1,facing=north]")
L += cmd("setblock 1 -60 2 repeater[delay=1,facing=east]") + cmd("setblock 0 -60 1 repeater[delay=1,facing=south]")
L += step(6) + cmd("setblock 0 -60 -1 redstone_block") + step(2) + cmd("setblock 0 -60 -1 air") + step(70)
write("ring", L + cmd("tick unfreeze"))

# Buttons and a lever, pressed by the client's use-block call: stone button 20 ticks, oak 30, lever stays; each beside a lamp
# through a block so the lamp shows the power. Daylight detector (and inverted) follows the clock under `time set`.
L = []
for i, (name, extra) in enumerate((("stone_button", "face=floor,facing=north"), ("oak_button", "face=floor,facing=north"), ("lever", "face=floor,facing=north"))):
    x = 1 + 3 * i
    L += cmd(f"setblock {x} -60 1 stone") + cmd(f"setblock {x} -60 2 redstone_lamp") + cmd(f"setblock {x} -60 0 {name}[{extra}]")
    L += cmd(f"setblock {x} -59 1 redstone_wire")
L = cmd("gamemode creative") + cmd("tp @s 4 -58 2") + L
L += step(4) + ["press 1 -60 0"] + step(30) + ["press 4 -60 0"] + step(40) + ["press 7 -60 0"] + step(4) + ["press 7 -60 0"] + step(4)
write("buttons", L + cmd("tick unfreeze"))

L = cmd("setblock 0 -60 0 daylight_detector[inverted=false,power=0]") + cmd("setblock 2 -60 0 daylight_detector[inverted=true,power=0]")
L += cmd("setblock 4 -60 0 daylight_detector[inverted=false,power=0]") + cmd("setblock 4 -59 0 stone")
L = cmd("weather clear") + L + ["wait 100"]
for t in (6000, 0, 12500, 13500, 18000, 23000, 1000):
    L += cmd(f"time set {t}") + step(24)
write("daylight", L + cmd("tick unfreeze"))
