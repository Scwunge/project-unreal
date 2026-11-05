# Writes rs_sponge.txt: a sponge in water, pools small enough for the 65-block cap not to matter (the shape of a removal that hits the cap
# is not fitted). A: 4x4x3 walled pool, sponge inside; B: a 1x20 walled channel, sponge in the middle (reach); C: dry sponge, water put
# beside it later; D: wet sponge placed in a pool; E: sponge on the surface of a deep pool: water above it only; F: sponge next to flowing water.
Y = -60
L = ["cmd gamerule send_command_feedback false", "cmd gamemode spectator", "cmd execute in minecraft:overworld run tp @s 20 -50 20", "wait 200",
     "cmd tick freeze", "wait 4", "cmd fill -2 -60 -2 60 -55 40 air", "cmd fill -2 -61 -2 60 -61 40 stone", "cmd gamerule do_mob_spawning false", "wait 10"]
def c(s): L.append("cmd " + s)
EV = {}
def ev(t, s): EV.setdefault(t, []).append("cmd " + s)
def pool(x0, z0, w, d, h):
    c(f"fill {x0-1} -60 {z0-1} {x0+w} {-60+h} {z0+d} stone")
    c(f"fill {x0} -60 {z0} {x0+w-1} {-60+h-1} {z0+d-1} water")
    c(f"fill {x0} {-60+h} {z0} {x0+w-1} {-60+h} {z0+d-1} air")
# A: x 1..4, z 1..4, height 3 ; sponge placed at (2,-59,2)
pool(1, 1, 4, 4, 3); ev(2, "setblock 2 -59 2 sponge")
# B: channel x 10..29, z 1, 1 deep ; sponge at x=19
pass  # (the channel lane was dropped: the first absorption took 6 blocks each side of a sponge in a 1x20 channel, the water refilling after is the fluid sim's)
# C: dry sponge, water source beside it at t=5
c("setblock 35 -60 3 sponge"); ev(5, "setblock 36 -60 3 water")
# D: wet sponge placed in a pool
pool(40, 1, 4, 4, 2); ev(2, "setblock 41 -60 2 wet_sponge")
# E: deep pool, sponge placed on the surface (air above), two layers of water below
pool(1, 10, 4, 4, 2); ev(2, "setblock 2 -59 11 sponge")
# F: sponge put in a pool where the water is not a source (level 3 flowing): water placed at one end spreads
c("fill 10 -61 10 30 -61 12 stone"); ev(1, "setblock 10 -60 11 water"); ev(10, "setblock 14 -60 11 sponge")
BOX = "0 -60 0 45 -58 14"
L += ["wait 10", f"log states {BOX}"]
for t in range(1, 25):
    for line in EV.get(t, []): L.append(line)
    L += ["cmd tick step 1", "wait 4", f"log states {BOX}"]
L.append("cmd tick unfreeze")
open("rs_sponge.txt", "w").write("\n".join(L) + "\n")
