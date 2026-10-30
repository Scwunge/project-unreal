# Writes the redstone timing scripts. The same file drives the vanilla reference client (ref.sh) and mc-unreal's
# McRefScript test, which replays the setblock, fill and tick step lines and prints the same "states" lines.
BOX = "-1 -60 -1 12 -58 6"
PRE = ["cmd gamerule send_command_feedback false", "cmd gamemode spectator", "cmd tp @s 5 -50 2", "wait 40",
       "cmd tick freeze", "cmd fill -2 -60 -2 14 -50 8 air", "cmd fill -2 -61 -2 14 -61 8 stone", "wait 10"]

def log(): return [f"log states {BOX}"]
def step(n=1):
    out = []
    for _ in range(n):
        out += ["cmd tick step 1", "wait 4"] + log()
    return out
def cmd(c): return [f"cmd {c}"]

def write(name, lines):
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
