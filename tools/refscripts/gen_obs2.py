# Writes rs_obs2.txt: observer edge cases. Each lane (4 cells apart in z) has an observer at x=1 facing east, watching x=2, with a
# lamp behind it at x=0 that lights while it pulses. What starts a pulse (a lever, dust, a replacement by another block, the same
# block, a door, a piston, water, an observer's own output), changes during a pulse, and observers facing each other.
Y = -60
L = ["cmd gamerule send_command_feedback false", "cmd gamemode spectator", "cmd execute in minecraft:overworld run tp @s 3 -50 20", "wait 120", "cmd tick freeze",
     "cmd fill -4 -60 -3 12 -55 90 air", "cmd fill -4 -61 -3 12 -61 90 stone", "cmd gamerule do_mob_spawning false", "wait 10"]
def c(s): L.append("cmd " + s)
EV = {}
def ev(t, s): EV.setdefault(t, []).append("cmd " + s)
lane = [0]
def obs(extra=(), watched=None, facing="east"):
    z = 4 * lane[0]; lane[0] += 1
    c(f"setblock 1 {Y} {z} observer[facing={facing}]"); c(f"setblock 0 {Y} {z} redstone_lamp")
    if watched: c(f"setblock 2 {Y} {z} {watched}")
    for e in extra: c(e.format(z=z))
    return z
def at(t, z, x, dy, block, dz=0): ev(t, f"setblock {x} {Y + dy} {z + dz} {block}")
# 0 lever on the floor, toggled twice
z = obs(watched="lever[face=floor,facing=north]"); at(2, z, 2, 0, "lever[face=floor,facing=north,powered=true]"); at(12, z, 2, 0, "lever[face=floor,facing=north,powered=false]")
# 1 dust powered then unpowered by a block beside it
z = obs(watched="redstone_wire"); at(2, z, 2, 0, "redstone_block", 1); at(12, z, 2, 0, "air", 1)
# 2 stone replaced by granite (another block), then by stone state-identical (setblock of the same block)
z = obs(watched="stone"); at(2, z, 2, 0, "granite"); at(12, z, 2, 0, "granite")
# 3 air to block and back
z = obs(); at(2, z, 2, 0, "stone"); at(12, z, 2, 0, "air")
# 4 changes during a pulse: at 2, 3, 4, 5, 6 (a stone/granite flip each tick)
z = obs(watched="stone")
for t in range(2, 7): at(t, z, 2, 0, "granite" if t % 2 == 0 else "stone")
# 5 a change on the tick the pulse turns off (2 -> pulse starts t=2+1.., second change at 4, 5)
z = obs(watched="stone"); at(2, z, 2, 0, "granite"); at(4, z, 2, 0, "stone"); at(5, z, 2, 0, "granite"); at(6, z, 2, 0, "stone")
# 6 a door being opened and closed
z = obs(watched="oak_door[half=lower,facing=north,open=false]", extra=["setblock 2 -59 {z} oak_door[half=upper,facing=north,open=false]"])
at(2, z, 2, 0, "oak_door[half=lower,facing=north,open=true]"); at(12, z, 2, 0, "oak_door[half=lower,facing=north,open=false]")
# 7 the upper half of a door changing (not watched)
z = obs(watched="oak_door[half=lower,facing=north,open=false]", extra=["setblock 2 -59 {z} oak_door[half=upper,facing=north,open=false]"])
at(2, z, 2, 1, "oak_door[half=upper,facing=north,open=true]")
# 8 a piston extending / retracting under power
z = obs(watched="piston[facing=up]"); at(2, z, 2, -1, "redstone_block", 0) if False else None
at(2, z, 3, 0, "redstone_block"); at(14, z, 3, 0, "air")
# 9 (moved to the end, water spreads into neighbouring lanes)
# 10 an observer facing the watched cell, which holds another observer facing it (a pair watching each other)
z = obs(watched="observer[facing=west]")
# 11 the watched cell is an observer facing away (pulses of that observer are changes of powered)
z = obs(watched="observer[facing=east]"); at(2, z, 3, 0, "stone"); at(2, z, 3, 0, "granite")
at(2, z, 4, 0, "air")
# 12 a long change into a lit lamp: observer facing up watching a lamp above
z = 4 * lane[0]; lane[0] += 1
c(f"setblock 1 {Y} {z} observer[facing=up]"); c(f"setblock 1 {Y - 0} {z} observer[facing=up]")
at(2, z, 1, 1, "stone"); at(12, z, 1, 1, "air")
# 13 observer facing north (watching z-1); output south
z = 4 * lane[0]; lane[0] += 1
c(f"setblock 1 {Y} {z} observer[facing=north]"); c(f"setblock 1 {Y} {z+1} redstone_lamp"); at(2, z, 1, 0, "stone", -1)
# 14 placed while its watched cell is changing the same tick
z = 4 * lane[0]; lane[0] += 1
c(f"setblock 0 {Y} {z} redstone_lamp"); at(2, z, 1, 0, "observer[facing=east]"); at(2, z, 2, 0, "stone")
# 15 observer removed during its pulse
z = obs(watched="stone"); at(2, z, 2, 0, "granite"); at(4, z, 1, 0, "air")
# water arriving beside the watched cell, two empty lanes from the others
lane[0] += 2
z = obs(); at(2, z, 3, 0, "water")
BOX = f"-1 -60 -1 5 -59 {4 * lane[0]}"
L += ["wait 10", f"log states {BOX}"]
for t in range(1, 41):
    for line in EV.get(t, []): L.append(line)
    L += ["cmd tick step 1", "wait 4", f"log states {BOX}"]
L.append("cmd tick unfreeze")
open("rs_obs2.txt", "w").write("\n".join(L) + "\n")
