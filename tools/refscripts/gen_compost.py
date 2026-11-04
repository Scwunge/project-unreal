# Writes lg_compost.txt: composters fed by hoppers. Lane 0 (frozen ticks, one log per tick): 64 cakes (always add a level) into a
# composter with a comparator beside it and a hopper under it taking what comes out; shows the ticks between levels, the
# turn to level 8 and the hopper taking the bone meal. Then the ticks run free and each other lane drops 64 of one item through a
# hopper into a composter, with a hopper under it moving the bone meal to a chest: bone meal made against items used gives how
# often an item adds a level (the chance is not in the cache).
Y = -60
ITEMS = ["wheat_seeds", "carrot", "pumpkin_seeds", "apple", "melon_slice", "wheat", "potato", "cactus", "pumpkin", "hay_block", "bread",
         "oak_leaves", "short_grass", "kelp", "sugar_cane", "baked_potato", "red_mushroom", "dandelion", "cake", "pumpkin_pie", "beetroot",
         "oak_sapling", "fern", "lily_pad", "stone"]
def it(slot, item, n): return f'{{Slot:{slot},id:"minecraft:{item}",count:{n}}}'
def nbt(*items): return "{Items:[" + ",".join(items) + "]}"
N = len(ITEMS)
ZMAX = 4 * N + 8
BOX = f"-1 -60 -1 4 -58 {ZMAX}"
L = ["cmd gamerule send_command_feedback false", "cmd gamemode spectator", "cmd execute in minecraft:overworld run tp @s 3 -50 20", "wait 120",
     "cmd tick freeze",
     f"cmd fill -3 -62 -3 8 -49 {ZMAX + 4} stone", "wait 4",
     f"cmd fill -2 -60 -2 7 -50 {ZMAX + 2} air", f"cmd fill -2 -61 -2 7 -61 {ZMAX + 2} stone", "wait 10"]
def c(s): L.append("cmd " + s)
def lane(z, item, n):
    c(f"setblock 1 {Y+2} {z} hopper[facing=down]{nbt(it(0, item, n))}"); c(f"setblock 1 {Y+1} {z} composter[level=0]")
    c(f"setblock 1 {Y} {z} hopper[facing=east]"); c(f"setblock 2 {Y} {z} chest")
lane(0, "cake", 64)
c(f"setblock 2 {Y+1} 0 comparator[facing=east]"); c(f"setblock 3 {Y+1} 0 redstone_lamp")
L += ["wait 10", f"log states {BOX}", f"log items {BOX}"]
for t in range(1, 101):
    L += ["cmd tick step 1", "wait 4", f"log states {BOX}", f"log items {BOX}"]
L.append("cmd tick unfreeze")
for i, item in enumerate(ITEMS): lane(4 * (i + 1), item, 64)
L += ["wait 20", f"log states {BOX}", f"log items {BOX}", "wait 900", f"log states {BOX}", f"log items {BOX}"]
open("lg_compost.txt", "w").write("\n".join(L) + "\n")
