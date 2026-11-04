# Writes lg_misc2.txt: note block instruments (by the block under it and by a head on top), a note block's powered flag, jukebox
# discs read by a comparator (the disc goes in from a dropper), lightning rods struck by a lightning bolt. Frozen ticks, states per tick.
import json
Y = -60
discs = [k for k in json.load(open("../../cache/data/reports/registries.json"))["minecraft:item"]["entries"] if "music_disc" in k]
BASES = ["stone", "dirt", "sand", "gravel", "glass", "oak_planks", "white_wool", "clay", "gold_block", "packed_ice", "bone_block", "iron_block",
         "soul_sand", "pumpkin", "emerald_block", "hay_block", "glowstone", "copper_block", "netherrack", "obsidian", "oak_log", "cobblestone",
         "sandstone", "white_concrete", "white_concrete_powder", "sea_lantern", "dried_kelp_block", "bamboo_block", "gold_ore", "oak_fence",
         "chest", "ice", "magma_block", "slime_block", "water", "barrier", "air", "oak_leaves", "tnt", "crafting_table", "grass_block", "mud",
         "oak_door", "oak_slab", "oak_stairs", "iron_trapdoor"]
HEADS = ["skeleton_skull", "wither_skeleton_skull", "zombie_head", "creeper_head", "dragon_head", "piglin_head", "player_head"]
L = ["cmd gamerule send_command_feedback false", "cmd gamemode spectator", "cmd execute in minecraft:overworld run tp @s 20 -50 30", "wait 200",
     "cmd tick freeze", "wait 4", "cmd fill -2 -60 -2 49 -58 99 air", "cmd fill -2 -61 -2 49 -61 99 stone", "wait 10"]
def c(s): L.append("cmd " + s)
EV = {}
def ev(t, s): EV.setdefault(t, []).append("cmd " + s)
# note blocks: x = 2 * index along a row; the block under is placed after the note block (a neighbour update), then replaced by air
for i, b in enumerate(BASES):
    x = 2 * (i % 22); z = 2 * (i // 22)
    c(f"setblock {x} {Y+1} {z} note_block")
    ev(2, f"setblock {x} {Y} {z} {b}")
for i, h in enumerate(HEADS):
    x = 2 * i; z = 6
    c(f"setblock {x} {Y} {z} note_block"); ev(2, f"setblock {x} {Y+1} {z} {h}")
# powered flag: redstone block beside, then away; and a note block under a solid block (no sound), and one under water
for k, (name, above) in enumerate((("air", None), ("stone", None))):
    x = 2 * k; z = 10
    c(f"setblock {x} {Y} {z} note_block"); ev(3, f"setblock {x+1} {Y} {z} redstone_block"); ev(9, f"setblock {x+1} {Y} {z} air")
    if above: c(f"setblock {x} {Y+1} {z} {above}")
c(f"setblock 8 {Y} 10 note_block"); c(f"setblock 8 {Y+1} 10 stone"); ev(3, f"setblock 9 {Y} 10 redstone_block")
# jukebox discs: dropper facing east, jukebox, comparator facing east, dust, a redstone block above the dropper at t=2
for i, d in enumerate(discs):
    z = 14 + 4 * i
    c(f'setblock 1 {Y} {z} dropper[facing=east]{{Items:[{{Slot:0,id:"{d}",count:1}}]}}'); c(f"setblock 2 {Y} {z} jukebox")
    c(f"setblock 3 {Y} {z} comparator[facing=east]"); c(f"setblock 4 {Y} {z} redstone_wire")
    ev(2, f"setblock 1 {Y+1} {z} redstone_block"); ev(5, f"setblock 1 {Y+1} {z} air")
ZR = 14 + 4 * len(discs)
# lightning rods struck: plain, weathered over copper, with a lamp beside, with dust
for k, rod in enumerate(("lightning_rod", "weathered_lightning_rod", "oxidized_lightning_rod", "exposed_lightning_rod")):
    x = 4 * k
    c(f"setblock {x} {Y} {ZR} {rod}[facing=up]"); c(f"setblock {x} {Y-0} {ZR+2} redstone_lamp"); c(f"setblock {x} {Y} {ZR+1} {rod.replace('lightning_rod','copper')}")
    c(f"setblock {x+1} {Y} {ZR} redstone_lamp"); c(f"setblock {x+2} {Y} {ZR} redstone_wire")
    ev(2, f"summon lightning_bolt {x}.5 {Y} {ZR}.5")
ZB = ZR + 4
BOX = f"-1 -60 -1 48 -59 {ZB}"
L += ["wait 10", f"log states {BOX}"]
for t in range(1, 81):
    for line in EV.get(t, []): L.append(line)
    L += ["cmd tick step 1", "wait 4", f"log states {BOX}"]
L.append("cmd tick unfreeze")
open("lg_misc2.txt", "w").write("\n".join(L) + "\n")
