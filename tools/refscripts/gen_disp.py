# Writes lg_disp.txt: what a dispenser (and a dropper) does with each kind of item, one pulse each, block states per tick and container item counts
# (log items), plus server entities (log sents) for the ones that make entities. Replayed by McClient.McSim.RefScriptsLogistics once baked (bake_lg.py disp).
Y = -60
L = ["cmd gamerule send_command_feedback false", "cmd gamemode spectator", "cmd tp @s 5 -50 4", "wait 40", "cmd tick freeze",
     "cmd kill @e[type=!player]",
     "cmd fill -4 -60 -3 14 -55 240 air", "cmd fill -4 -61 -3 14 -61 240 stone", "cmd gamerule do_mob_spawning false", "cmd gamerule do_fire_tick false", "wait 10"]
def c(s): L.append("cmd " + s)
EV = {}
def ev(t, s): EV.setdefault(t, []).append(s)
rows = []
def row(name, item, front=None, floor=None, kind="dispenser", facing="east", count=1, extra=()):
    z = 4 * len(rows); rows.append(name)
    c(f"setblock 2 {Y} {z} {kind}[facing={facing}]")
    c(f'item replace block 2 {Y} {z} container.0 with {item} {count}' if item else "gamerule send_command_feedback false")
    if floor: c(f"setblock 3 {Y-1} {z} {floor}")
    if front: c(f"setblock 3 {Y} {z} {front}")
    for e in extra: c(e.format(z=z))
    ev(2, f"setblock 1 {Y} {z} redstone_block"); ev(3, f"setblock 1 {Y} {z} air")
    return z
row("arrow", "arrow", count=3)
row("water", "water_bucket"); row("lava", "lava_bucket")
row("bucket_water", "bucket", front="water"); row("bucket_lava", "bucket", front="lava")
row("water_on_block", "water_bucket", front="stone")
row("flint_tnt", "flint_and_steel", front="tnt"); row("flint_campfire", "flint_and_steel", front="campfire[lit=false]")
row("flint_air", "flint_and_steel"); row("flint_candle", "flint_and_steel", front="candle")
row("charge_air", "fire_charge"); row("charge_campfire", "fire_charge", front="campfire[lit=false]")
row("bone_wheat", "bone_meal", front="wheat[age=0]", floor="farmland"); row("bone_sapling", "bone_meal", front="oak_sapling", floor="dirt")
row("bone_grass", "bone_meal", front="grass_block"); row("bone_air", "bone_meal")
row("shulker", "shulker_box"); row("carved_pumpkin", "carved_pumpkin"); row("pumpkin_head_on_golem_base", "carved_pumpkin", front="air")
row("bottle_water_cauldron", "glass_bottle", front="water_cauldron[level=3]"); row("bottle_hive", "glass_bottle", front="beehive[honey_level=5,facing=north]")
row("shears_hive", "shears", front="beehive[honey_level=5,facing=north]"); row("honeycomb_copper", "honeycomb", front="copper_block")
row("minecart_rail", "minecart", front="rail[shape=east_west]"); row("minecart_air", "minecart")
row("tnt_item", "tnt"); row("stone", "stone", count=4); row("egg", "egg"); row("snowball", "snowball")
row("water_cauldron", "water_bucket", front="cauldron"); row("lava_cauldron", "lava_bucket", front="cauldron")
row("bucket_water_cauldron", "bucket", front="water_cauldron[level=3]")
row("boat", "oak_boat", front="water", floor="stone"); row("boat_land", "oak_boat")
row("skull", "wither_skeleton_skull"); row("skull_soul", "wither_skeleton_skull", floor="soul_sand")
row("empty", None)
row("sword", "iron_sword"); row("helmet_on_stand", "iron_helmet", extra=["summon armor_stand 3.5 -60 {z}.5 {{NoGravity:1b}}"])
row("spawn_egg", "pig_spawn_egg"); row("firework", "firework_rocket"); row("xp_bottle", "experience_bottle")
row("splash", "splash_potion"); row("brush_sand", "brush", front="suspicious_sand")
row("harvest_pumpkin_stem", "bone_meal", front="pumpkin_stem[age=0]", floor="farmland")
# a dropper into each kind of container, and a dropper that is facing a full one
row("drop_chest", "cobblestone", kind="dropper", front="chest", count=5)
row("drop_hopper", "cobblestone", kind="dropper", front="hopper[facing=down]", count=5)
row("drop_furnace", "coal", kind="dropper", front="furnace", count=5)
row("drop_furnace_ore", "iron_ore", kind="dropper", front="furnace", count=5)
row("drop_composter", "wheat_seeds", kind="dropper", front="composter", count=5)
row("drop_brewing", "blaze_powder", kind="dropper", front="brewing_stand")
row("drop_jukebox", "music_disc_cat", kind="dropper", front="jukebox")
row("drop_lectern", "writable_book", kind="dropper", front="lectern")
row("drop_cauldron", "water_bucket", kind="dropper", front="cauldron")
row("drop_barrel", "cobblestone", kind="dropper", front="barrel", count=5)
row("drop_shulker", "cobblestone", kind="dropper", front="shulker_box", count=5)
row("drop_dispenser", "cobblestone", kind="dropper", front="dispenser[facing=east]", count=5)
row("drop_air", "cobblestone", kind="dropper", count=5)
row("drop_down_hopper", "cobblestone", kind="dropper", facing="east", front="hopper[facing=east]", count=5)
BOX = f"-1 {Y} -2 8 {Y+3} {4 * len(rows)}"
L += ["wait 10", f"log states {BOX}", f"log items {BOX}", "log sents minecraft 600"]
for t in range(1, 41):
    L += ["cmd " + s for s in EV.get(t, [])]
    L += ["cmd tick step 1", "wait 4", f"log states {BOX}", f"log items {BOX}"]
L.append("cmd tick unfreeze")
open("lg_disp.txt", "w").write("\n".join(L) + "\n")
print(len(rows), "rows")
