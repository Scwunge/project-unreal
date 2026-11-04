# Writes displace.txt: what flowing water and lava do to each block they reach (provenance gap 4). One row per block,
# water source (and lava source) beside it on a flat floor; control column with no liquid.
B = ["oak_sign", "oak_wall_sign", "ladder", "oak_slab", "oak_stairs", "chain", "lantern", "iron_bars", "glass_pane", "oak_fence",
     "oak_trapdoor", "chest", "light", "barrier", "scaffolding", "candle", "end_rod", "lightning_rod", "sea_pickle", "hopper",
     "torch", "redstone_wire", "rail", "stone_button", "lever", "oak_pressure_plate", "white_carpet", "snow", "flower_pot",
     "cauldron", "oak_door", "short_grass", "poppy", "wheat", "sugar_cane", "kelp", "seagrass", "tall_grass", "fire", "cobweb",
     "oak_sapling", "repeater", "comparator", "oak_button", "tripwire", "white_bed", "decorated_pot", "stone_slab", "copper_chain", "pointed_dripstone"]
L = ["cmd gamerule send_command_feedback false", "cmd gamemode spectator", "cmd execute in minecraft:overworld run tp @s 10 -50 40", "wait 200",
     "cmd gamerule random_tick_speed 0"]
N = len(B)
for z0 in range(0, 3 * N + 3, 40):
    L.append(f"cmd fill -2 -59 {z0} 70 -55 {min(z0+39, 3*N+2)} air")
L.append(f"cmd fill -2 -60 0 70 -60 {3*N+2} grass_block")
for i, b in enumerate(B):
    z = 3 * i + 1
    for x, liq in ((2, "water"), (40, "lava"), (60, None)):
        L.append(f"cmd setblock {x} -59 {z+1} stone")
        L.append(f"cmd setblock {x} -59 {z} minecraft:{b}")
        if liq:
            L.append(f"cmd setblock {x+1} -59 {z} minecraft:{liq}")
    # stone fences between rows
for z in range(0, 3 * N + 2, 3):
    L.append(f"cmd fill -2 -59 {z} 70 -59 {z} stone")
L.append("wait 20")
L.append("log states 0 -59 0 70 -59 %d" % (3 * N + 1))
L.append("wait 300")
L.append("log states 0 -59 0 70 -59 %d" % (3 * N + 1))
L.append("wait 600")
L.append("log states 0 -59 0 70 -59 %d" % (3 * N + 1))
open("E:/mc-reference/scripts/displace.txt", "w").write("\n".join(L) + "\n")
print(len(L))
