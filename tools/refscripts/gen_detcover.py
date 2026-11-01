# Writes rs_detcover.txt: daylight detectors (normal at z=0, inverted at z=4) with different things over them, 10 blocks apart
# in x, at y=201 high above the ground, logged each tick at noon and then at dusk. The covers: none, stone 1x1, 3x3, 5x5, 7x7,
# glass 3x3, leaves, ice 3x3, top slab 3x3, bottom slab 3x3, cobweb 3x3, stone 3x3 two blocks up.
COVERS = [("none", None, 0, 202), ("stone1", "stone", 0, 202), ("stone3", "stone", 1, 202), ("stone5", "stone", 2, 202),
          ("stone7", "stone", 3, 202), ("glass3", "glass", 1, 202), ("leaves1", "oak_leaves[persistent=true]", 0, 202),
          ("ice3", "ice", 1, 202), ("slabtop3", "stone_slab[type=top]", 1, 202), ("slabbot3", "stone_slab[type=bottom]", 1, 202),
          ("cobweb3", "cobweb", 1, 202), ("stone3up", "stone", 1, 203)]
OX = 48                    # clear of the chunk the ground set-up works in (its columns read unlit)
X1 = OX + 10 * len(COVERS) + 6
BOX = f"{OX-4} 200 -4 {X1} 203 8"
L = ["cmd gamerule send_command_feedback false", "cmd gamemode spectator", "cmd tp @s 5 -50 2", "wait 40", "cmd tick freeze",
     "cmd fill -2 -60 -2 14 -50 8 air", "cmd fill -2 -61 -2 14 -61 8 stone", "wait 10",
     f"cmd tp @s {(OX+X1)//2}.5 205 2.5", "wait 60", "cmd weather clear 1000000", "cmd time set 6000", f"cmd fill {OX-4} 200 -4 {X1} 215 8 air", "wait 4",
     f"cmd fill {OX-4} 200 -4 {X1} 200 8 stone", "wait 4"]
n = 0
for i, (name, block, r, y) in enumerate(COVERS):
    x = OX + 10 * i
    L.append(f"cmd setblock {x} 201 0 daylight_detector[inverted=false,power=0]")
    L.append(f"cmd setblock {x} 201 4 daylight_detector[inverted=true,power=0]")
    if block:
        for z in (0, 4):
            L.append(f"cmd fill {x-r} {y} {z-r} {x+r} {y} {z+r} {block}")
    n += 1
    if n % 3 == 0: L.append("wait 3")
L += ["wait 40", "cmd tick step 45", "wait 10", f"log states {BOX}"]
for t in range(1, 131):
    if t == 61: L.append("cmd time set 13000")
    L += ["cmd tick step 1", "wait 4", f"log states {BOX}"]
L.append("cmd tick unfreeze")
open("rs_detcover.txt", "w").write("\n".join(L) + "\n")
