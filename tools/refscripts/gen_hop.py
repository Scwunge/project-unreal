# Writes lg_hop.txt: hopper edge cases, one block-state and item log per tick (frozen ticks): what a hopper takes from the
# container above it, how it fills a chest, a furnace (from above and from the side), a composter, a hopper with a nearly
# full stack, a shulker box going into a shulker box, a locked hopper and one facing a block that holds nothing.
# Replayed by McClient.McSim.RefScriptsLogistics (the contents live in McClient).
Y = -60
def it(slot, item, n): return f'{{Slot:{slot},id:"minecraft:{item}",count:{n}}}'
def nbt(*items): return "{Items:[" + ",".join(items) + "]}" if items else ""
L = ["cmd gamerule send_command_feedback false", "cmd gamemode spectator", "cmd tp @s 3 -50 20", "wait 40", "cmd tick freeze",
     "cmd fill -3 -62 -3 8 -49 60 stone", "wait 4",
     "cmd fill -2 -60 -2 7 -50 58 air", "cmd fill -2 -61 -2 7 -61 58 stone", "wait 10"]
def c(s): L.append("cmd " + s)
rows = []
def row(fn): rows.append(fn)
def r0(z): c(f"setblock 1 {Y} {z} hopper[facing=east]{nbt(it(0,'cobblestone',5))}"); c(f"setblock 2 {Y} {z} chest")
def r1(z): c(f"setblock 1 {Y+1} {z} chest{nbt(it(0,'cobblestone',5))}"); c(f"setblock 1 {Y} {z} hopper[facing=east]"); c(f"setblock 2 {Y} {z} chest")
def r2(z): c(f"setblock 1 {Y} {z} hopper[facing=east]{nbt(it(0,'cobblestone',2),it(1,'dirt',2),it(2,'stick',1),it(4,'dirt',1))}"); c(f"setblock 2 {Y} {z} chest")
def r3(z): c(f"setblock 1 {Y+1} {z} hopper[facing=down]{nbt(it(0,'iron_ore',3),it(1,'coal',2))}"); c(f"setblock 1 {Y} {z} furnace[lit=false]")
def r4(z): c(f"setblock 1 {Y} {z} hopper[facing=east]{nbt(it(0,'iron_ore',2),it(1,'coal',2),it(2,'oak_planks',2))}"); c(f"setblock 2 {Y} {z} furnace[lit=false]")
def r5(z): c(f"setblock 1 {Y+1} {z} hopper[facing=down]{nbt(it(0,'wheat_seeds',5))}"); c(f"setblock 1 {Y} {z} composter")
def r6(z):
    c(f"setblock 1 {Y} {z} hopper[facing=east]{nbt(it(0,'cobblestone',3))}"); c(f"setblock 2 {Y} {z} hopper[facing=east]")
    c(f"setblock 3 {Y} {z} hopper[facing=east]"); c(f"setblock 4 {Y} {z} chest")
def r7(z):
    c(f"setblock 1 {Y} {z} hopper[facing=east]{nbt(it(0,'cobblestone',3))}"); c(f"setblock 2 {Y} {z} chest"); c(f"setblock 1 {Y+1} {z} redstone_block")
def r8(z): c(f"setblock 1 {Y} {z} hopper[facing=east]{nbt(it(0,'cobblestone',3))}"); c(f"setblock 2 {Y} {z} hopper[facing=south]{nbt(it(0,'cobblestone',63))}")
def r9(z): c(f"setblock 1 {Y} {z} hopper[facing=east]{nbt(it(0,'iron_pickaxe',2))}"); c(f"setblock 2 {Y} {z} chest{nbt(it(0,'iron_pickaxe',1))}")
def r10(z): c(f"setblock 1 {Y} {z} hopper[facing=east]{nbt(it(0,'cobblestone',3))}"); c(f"setblock 2 {Y} {z} dropper[facing=up]")
def r11(z): c(f"setblock 1 {Y} {z} hopper[facing=east]{nbt(it(0,'shulker_box',1))}"); c(f"setblock 2 {Y} {z} shulker_box")
def r12(z): c(f"setblock 1 {Y} {z} hopper[facing=east]{nbt(it(0,'cobblestone',3))}"); c(f"setblock 2 {Y} {z} stone")
def r13(z): c(f"setblock 1 {Y} {z} hopper[facing=east]{nbt(it(0,'cobblestone',3))}"); c(f"setblock 2 {Y} {z} barrel[facing=up]")
def r14(z): c(f"setblock 1 {Y+1} {z} hopper[facing=down]{nbt(it(0,'blaze_powder',3),it(1,'glass_bottle',2))}"); c(f"setblock 1 {Y} {z} brewing_stand")
def r15(z): c(f"setblock 1 {Y} {z} hopper[facing=east]{nbt(it(0,'blaze_powder',3),it(1,'glass_bottle',2))}"); c(f"setblock 2 {Y} {z} brewing_stand")
for fn in (r0,r1,r2,r3,r4,r5,r6,r7,r8,r9,r10,r11,r12,r13,r14,r15): row(fn)
for i, fn in enumerate(rows): fn(4 * i)
BOX = f"-1 -60 0 5 -59 {4 * (len(rows) - 1)}"
L += ["wait 10", f"log states {BOX}", f"log items {BOX}"]
for t in range(1, 71):
    if t == 24: c(f"setblock 1 {Y+1} {4*7} air")
    L += ["cmd tick step 1", "wait 4", f"log states {BOX}", f"log items {BOX}"]
L.append("cmd tick unfreeze")
open("lg_hop.txt", "w").write("\n".join(L) + "\n")
