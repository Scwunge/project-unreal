# Writes lg_juke2.txt: a jukebox holding a disc above a hopper (facing down) above a chest; and the same with the disc put in by a dropper
# after the hopper is there. Does the hopper under it take the disc, or does the jukebox's signal lock the hopper?
Y = -60
L = ["cmd gamerule send_command_feedback false", "cmd gamemode spectator", "cmd execute in minecraft:overworld run tp @s 10 -50 40", "wait 200",
     "cmd tick freeze", "wait 4", "cmd fill -2 -60 -2 12 -55 20 air", "cmd fill -2 -61 -2 12 -61 20 stone", "wait 10"]
def c(s): L.append("cmd " + s)
# lane 0 (z=0): jukebox at y=-58, hopper -59, chest -60; disc placed by item replace after
c(f"setblock 1 {Y} 0 chest"); c(f"setblock 1 {Y+1} 0 hopper[facing=down]"); c(f"setblock 1 {Y+2} 0 jukebox")
# lane 1 (z=4): same but the hopper is beside the jukebox (east side) facing into a chest to the east
c(f"setblock 5 {Y} 4 chest"); c(f"setblock 5 {Y} 4 chest"); c(f"setblock 4 {Y} 4 jukebox"); c(f"setblock 5 {Y} 4 hopper[facing=east]"); c(f"setblock 6 {Y} 4 chest")
c(f"setblock 1 {Y} 8 chest"); c(f"setblock 1 {Y+1} 8 hopper[facing=down]"); c(f"setblock 1 {Y+2} 8 jukebox[has_record=true]")
c(f"item replace block 1 {Y+2} 0 container.0 with minecraft:music_disc_cat 1")
c(f"item replace block 1 {Y+2} 8 container.0 with minecraft:music_disc_13 1")
c(f"item replace block 4 {Y} 4 container.0 with minecraft:music_disc_11 1")
BOX = f"0 {Y} 0 7 {Y+2} 8"
L += ["wait 10", f"log states {BOX}", f"log items {BOX}"]
for t in range(1, 25):
    L += ["cmd tick step 1", "wait 4", f"log states {BOX}", f"log items {BOX}"]
L.append("cmd tick unfreeze")
open("lg_juke2.txt", "w").write("\n".join(L) + "\n")
