# Writes lectern2.txt: the comparator signal of a lectern by book length and page (provenance: McBook::GetComparatorSignal).
import re
src = [l for l in open("E:/mc-reference/scripts/lectern.txt") if l.startswith("cmd setblock 2 -60 2 lectern")][0]
CASES = [(n, p) for n in (1, 2, 3, 4, 5, 8, 10, 16, 20, 100) for p in sorted(set([0, 1, n // 2, n - 2, n - 1])) if 0 <= p < n]
L = ["cmd gamerule send_command_feedback false", "cmd gamemode spectator", "cmd execute in minecraft:overworld run tp @s 40 -50 6", "wait 200",
     "cmd tick freeze", "wait 4", "cmd gamerule do_mob_spawning false"]
for x0 in range(-2, 3 * len(CASES) + 8, 60):
    L.append(f"cmd fill {x0} -62 -2 {x0+59} -56 10 air")
    L.append(f"cmd fill {x0} -62 -2 {x0+59} -61 10 stone")
for i, (n, p) in enumerate(CASES):
    x = 3 * i
    pages = ",".join('"p%d"' % k for k in range(n))
    book = f'{{id:"minecraft:written_book",count:1,components:{{"minecraft:written_book_content":{{title:"t",author:"a",pages:[{pages}]}}}}}}'
    L.append(f"cmd setblock {x} -60 2 lectern[facing=south,has_book=true]{{Page:{p},Book:{book}}}")
    L.append(f"cmd setblock {x} -60 3 comparator[facing=north]")
    L.append(f"cmd setblock {x} -60 4 redstone_wire")
L += ["cmd tick step 5", "wait 40"]
for i, (n, p) in enumerate(CASES):
    L.append(f"log states {3*i} -60 4 {3*i} -60 4")
L.append("cmd tick unfreeze")
open("E:/mc-reference/scripts/lectern2.txt", "w").write("\n".join(L) + "\n")
print(len(CASES))
