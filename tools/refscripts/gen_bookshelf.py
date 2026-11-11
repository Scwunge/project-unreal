# Writes bookshelf.txt: the comparator signal of a chiseled bookshelf by last interacted slot and which slots hold books.
CASES = []
for last in (-1, 0, 1, 2, 3, 4, 5):
    CASES.append((last, 63))   # all six slots full
    CASES.append((last, 0))    # none full
    CASES.append((last, 1 << (last if last >= 0 else 0)))  # only that slot full
L = ["cmd gamerule send_command_feedback false", "cmd gamemode spectator", "cmd execute in minecraft:overworld run tp @s 20 -50 6", "wait 200",
     "cmd tick freeze", "wait 4", "cmd gamerule do_mob_spawning false"]
L.append(f"cmd fill -2 -62 -2 {3*len(CASES)+2} -56 10 air")
L.append(f"cmd fill -2 -62 -2 {3*len(CASES)+2} -61 stone".replace(f"{3*len(CASES)+2} -61 stone", f"{3*len(CASES)+2} -61 10 stone"))
for i, (last, mask) in enumerate(CASES):
    x = 3 * i
    props = ",".join(f"slot_{s}_occupied={'true' if mask >> s & 1 else 'false'}" for s in range(6))
    L.append(f"cmd setblock {x} -60 2 chiseled_bookshelf[facing=south,{props}]{{last_interacted_slot:{last}}}")
    L.append(f"cmd setblock {x} -60 3 comparator[facing=north]")
    L.append(f"cmd setblock {x} -60 4 redstone_wire")
L += ["cmd tick step 5", "wait 40"]
for i in range(len(CASES)):
    L.append(f"log states {3*i} -60 4 {3*i} -60 4")
L.append("cmd tick unfreeze")
open("E:/mc-reference/scripts/bookshelf.txt", "w").write("\n".join(L) + "\n")
print(len(CASES))
