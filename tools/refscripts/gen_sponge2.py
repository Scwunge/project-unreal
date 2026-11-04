# Writes sponge2.txt: how much water a sponge takes. Walled basins w x d x h full of water (sources), the sponge set at the middle of the top
# layer (or the bottom, "low"), the water counted right after (tick 0, 1, 2) and after 30 ticks. Basins 30 apart in x, 20 in z.
BASINS = [("flat9", 9, 9, 1, "top"), ("flat13", 13, 13, 1, "top"), ("cube4", 4, 4, 4, "top"), ("cube5", 5, 5, 5, "top"), ("cube5low", 5, 5, 5, "low"),
          ("cube6", 6, 6, 6, "top"), ("tall3", 3, 3, 12, "top"), ("flat5deep3", 5, 5, 3, "top"), ("line30", 30, 1, 1, "mid"), ("cube3", 3, 3, 3, "top")]
L = ["cmd gamerule send_command_feedback false", "cmd gamemode spectator", "cmd execute in minecraft:overworld run tp @s 30 -40 10", "wait 200",
     "cmd tick freeze", "wait 4", "cmd gamerule do_mob_spawning false", "cmd weather clear 1000000"]
for y in range(-60, 40, 15):
    for z in range(-2, 80, 16):
        L.append(f"cmd fill -2 {y} {z} 130 {y+14} {z+15} air")
L.append("cmd fill -2 -62 -2 130 -61 80 stone")
pos = {}
for i, (name, w, d, h, where) in enumerate(BASINS):
    x0 = 3 + 32 * (i % 4); z0 = 3 + 20 * (i // 4) * 1
    pos[name] = (x0, z0, w, d, h)
    L.append(f"cmd fill {x0-1} -60 {z0-1} {x0+w} {-60+h} {z0+d} stone")
    L.append(f"cmd fill {x0} -60 {z0} {x0+w-1} {-60+h-1} {z0+d-1} water")
    L.append(f"cmd fill {x0} {-60+h} {z0} {x0+w-1} {-60+h} {z0+d-1} air")
L.append("wait 20")
def snap():
    for name, (x0, z0, w, d, h) in pos.items():
        L.append(f"log box {x0} -60 {z0} {x0+w-1} {-60+h-1} {z0+d-1}")
snap()
for name, w_ in zip(pos, BASINS):
    x0, z0, w, d, h = pos[name]; where = w_[4]
    y = -60 + h - 1 if where == "top" else (-60 if where == "low" else -60)
    L.append(f"cmd setblock {x0 + w//2} {y} {z0 + d//2} sponge")
for t in range(3):
    L += ["cmd tick step 1", "wait 6"]; snap()
L += ["cmd tick step 30", "wait 60"]; snap()
L.append("cmd tick unfreeze")
open("E:/mc-reference/scripts/sponge2.txt", "w").write("\n".join(L) + "\n")
