# Writes rs_pist2.txt: piston edge cases. Quasi-connectivity (power above the block over the piston, over air, from the side of that
# block), a power block in front (does not power), pulses of 1/2/3 ticks by quasi power, the push limit (12 and 13), what cannot move or
# breaks, and a sticky piston pulling a slime block with a stone on it. Retention of an extended piston until a block update.
Y = -60
L = ["cmd gamerule send_command_feedback false", "cmd gamemode creative", "cmd tp @s 20 -57 8", "wait 40", "cmd tick freeze",
     "cmd fill -4 -60 -3 26 -55 130 air", "cmd fill -4 -61 -3 26 -61 130 stone", "cmd gamerule do_mob_spawning false", "wait 10"]
def c(s): L.append("cmd " + s)
EV = {}
def ev(t, s): EV.setdefault(t, []).append(s)
Z = {}
def piston(n, name, kind="piston", facing="east", ahead=()):
    z = 4 * n; Z[name] = z
    c(f"setblock 2 {Y} {z} {kind}[facing={facing}]")
    for dx, b in ahead: c(f"setblock {2 + dx} {Y} {z} {b}")
    return z
def at(t, name, dx, dy, block, dz=0): ev(t, f"setblock {dx} {Y + dy} {Z[name] + dz} {block}")
n = 0
# quasi-connectivity: power two above the piston, with a stone or air between; or beside the block above (diagonal)
for name, mid, src in (("q_stone", "stone", (2, 2)), ("q_air", None, (2, 2)), ("q_diag", "stone", (3, 1)), ("q_diagair", None, (3, 1)), ("q_side", None, (3, 0)),
                       ("q_back", None, (1, 0)), ("q_below", None, (2, -1))):
    piston(n, name, ahead=[(1, "stone")])
    if mid: c(f"setblock 2 {Y + 1} {Z[name]} {mid}")
    if name == "q_below": c(f"setblock 2 {Y} {Z[name]} piston[facing=east]")
    at(2, name, src[0], src[1], "redstone_block"); at(12, name, src[0], src[1], "air")
    at(40, name, 2, 0, "stone", -1)    # a block update beside the piston
    n += 1
piston(n, "front", ahead=[(1, "air")]); at(2, "front", 3, 0, "redstone_block"); at(12, "front", 3, 0, "air"); n += 1
# pulses by quasi power, plain and sticky
for ln in (1, 2, 3):
    for kind in ("piston", "sticky_piston"):
        nm = f"p{ln}_{kind[:1]}"
        piston(n, nm, kind, ahead=[(1, "stone")]); c(f"setblock 2 {Y+1} {Z[nm]} stone")
        at(2, nm, 2, 2, "redstone_block"); at(2 + ln, nm, 2, 2, "air"); at(30, nm, 2, 0, "stone", -1)
        n += 1
# push limit
for cnt in (12, 13):
    nm = f"push{cnt}"; piston(n, nm, ahead=[(i, "stone") for i in range(1, cnt + 1)])
    at(2, nm, 1, 0, "redstone_block"); at(14, nm, 1, 0, "air"); n += 1
# what does not move, what breaks, what moves oddly
for nm, blk in (("obsidian", "obsidian"), ("chest", "chest"), ("furnace", "furnace"), ("glazed", "white_glazed_terracotta"), ("torch", "torch"), ("flower", "dandelion"),
                ("sand", "sand"), ("bell", "bell"), ("hopper", "hopper"), ("piston2", "piston[facing=east]")):
    piston(n, nm, ahead=[(1, blk)]); c(f"setblock 3 {Y-1} {Z[nm]} dirt"); at(2, nm, 1, 0, "redstone_block"); at(14, nm, 1, 0, "air"); n += 1
# sticky + slime
nm = "slime"; piston(n, nm, "sticky_piston", ahead=[(1, "slime_block"), (2, "stone")]); c(f"setblock 3 {Y+1} {Z[nm]} stone"); c(f"setblock 3 {Y-0} {Z[nm]+1} stone")
at(2, nm, 1, 0, "redstone_block"); at(14, nm, 1, 0, "air"); n += 1
nm = "honey"; piston(n, nm, "sticky_piston", ahead=[(1, "honey_block"), (2, "slime_block"), (3, "stone")]); at(2, nm, 1, 0, "redstone_block"); at(14, nm, 1, 0, "air"); n += 1
BOX = f"-1 {Y} -2 22 {Y+2} {4 * n}"
L += ["wait 10", f"log states {BOX}"]
for t in range(1, 61):
    L += ["cmd " + s for s in EV.get(t, [])]
    L += ["cmd tick step 1", "wait 4", f"log states {BOX}"]
L.append("cmd tick unfreeze")
open("rs_pist2.txt", "w").write("\n".join(L) + "\n")
print(n, "rows")
