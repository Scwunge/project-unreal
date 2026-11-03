# Writes lg_cmpread.txt: what a comparator reads from a furnace, blast furnace, smoker and brewing stand with different things in
# them (items nothing smelts or brews, so the contents stay as set), shown as the power of the dust behind the comparator.
# Replayed by McClient.McSim.RefScriptsLogistics (the contents live in McClient).
Y = -60
def it(slot, item, n): return f'{{Slot:{slot},id:"minecraft:{item}",count:{n}}}'
CASES = [
    ("furnace", []), ("furnace", [it(0, "dirt", 1)]), ("furnace", [it(0, "dirt", 64)]), ("furnace", [it(2, "dirt", 1)]),
    ("furnace", [it(1, "stick", 64)]), ("furnace", [it(1, "lava_bucket", 1)]), ("furnace", [it(0, "dirt", 64), it(1, "stick", 64), it(2, "dirt", 64)]),
    ("furnace", [it(0, "dirt", 32), it(1, "stick", 16), it(2, "dirt", 8)]),
    ("blast_furnace", [it(0, "dirt", 64)]), ("smoker", [it(0, "dirt", 64), it(1, "stick", 64)]),
    ("brewing_stand", []), ("brewing_stand", [it(0, "potion", 1)]),
    ("brewing_stand", [it(0, "potion", 1), it(1, "potion", 1), it(2, "potion", 1)]), ("brewing_stand", [it(3, "dirt", 1)]),
    ("brewing_stand", [it(0, "potion", 1), it(1, "potion", 1), it(2, "potion", 1), it(3, "dirt", 64), it(4, "stick", 64)]),
    ("brewing_stand", [it(3, "dirt", 32), it(4, "stick", 16)]),
]
X1 = 4 * len(CASES) + 2
BOX = f"-1 -60 0 {X1} -60 6"
L = ["cmd gamerule send_command_feedback false", "cmd gamemode spectator", f"cmd tp @s {X1//2} -50 4", "wait 40", "cmd tick freeze",
     f"cmd fill -3 -62 -3 {X1+2} -49 -3 stone", f"cmd fill -3 -62 9 {X1+2} -49 9 stone", f"cmd fill -3 -62 -3 -3 -49 9 stone",
     f"cmd fill {X1+2} -62 -3 {X1+2} -49 9 stone", f"cmd fill -3 -49 -3 {X1+2} -49 9 stone", "wait 4",
     f"cmd fill -2 -60 -2 {X1+1} -50 8 air", f"cmd fill -2 -61 -2 {X1+1} -61 8 stone", "wait 10", f"log states {BOX}"]
for i, (kind, items) in enumerate(CASES):
    x = 4 * i
    state = "[lit=false]" if "furnace" in kind or kind == "smoker" else ""
    nbt = "{Items:[" + ",".join(items) + "]}" if items else ""
    L.append(f"cmd setblock {x} {Y} 4 {kind}{state}{nbt}")
    L.append(f"cmd setblock {x} {Y} 3 comparator[facing=south]")
    L.append(f"cmd setblock {x} {Y} 2 redstone_wire")
    if i % 4 == 3: L.append("wait 3")
L += ["wait 10", f"log states {BOX}"]
for _ in range(8): L += ["cmd tick step 1", "wait 4", f"log states {BOX}"]
L.append("cmd tick unfreeze")
open("lg_cmpread.txt", "w").write("\n".join(L) + "\n")
