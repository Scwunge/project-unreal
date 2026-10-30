# Survival playtest: rough points (before any fixing)

Method, honestly: I cannot press keys in the game, so this is NOT a 20 minute hands-on run. It is the title/new world
path read from the code and driven with the dev switches (-McMenuPress, -McChat, shots), plus a code audit of each thing
a new player does. Items marked (code) are from the source only; nothing here was seen in a live session unless it
says (shot). Jarring: 3 = a player notices in the first minutes, 2 = notices soon, 1 = polish.

| # | What a new player does | What vanilla does | What we do | Jarring |
|---|---|---|---|---|
| 1 | Press F3 | Debug screen (position, fps, biome, light) | No F-key handling at all (only F7); F1 (hide HUD), F2 (screenshot), F3, F5 (third person), F11 (fullscreen) all do nothing (code) | 3 (F3/F5), 2 (F1/F11) |
| 2 | Press Q to drop the held item in the world | Drops one (Ctrl+Q the stack) | Q only works inside a container screen (McHUDScreens DropKey); no in-world drop key (code) | 3 |
| 3 | Find a bed, right-click at night | Sleeps, skips the night, sets the respawn point; message when too early | No bed use in McBlockUse; only a /sleep-ish command exists. Night cannot be skipped, spawn point cannot be set (code) | 3 |
| 4 | Punch/dig a block | Crack overlay grows over the block as it breaks, selection outline around the target | Hardness timing exists (McBlockBreak) but I found no destroy_stage crack overlay in the code, and no sign of a target-block outline in McRender/McPlayer (code, to confirm in a shot) | 3 |
| 5 | Break, place, walk, hurt | Sounds for every one | Footsteps, hurt/death, mob and dispenser sounds are wired (code); I could not hear the game here, so break/place/eat/pickup/UI-click/XP sounds are unverified | 2 (verify first) |
| 6 | Walk, look around | Held item/arm with swing and bob | Done this session (arm only on an empty hand; block is still more face-on than vanilla's) (shot) | 1 |
| 7 | Hover an item | Name tooltip (with enchants/lore) | Exists in containers (McHUDScreens ~1285). Not shown for the hotbar item name when switching slots (code, to confirm) | 1-2 |
| 8 | Die | "You died!" screen with score, Respawn and Title Screen buttons | Death screen with "You died!" and Respawn exists (McHUDScreens ~1315); I did not find the score line or a Title Screen button (code) | 2 |
| 9 | Esc in game | Pause menu; Esc again returns | Done this session (commit 56fb200). Open to LAN: a listen server starts but a second client joining is not tested | 1 |
| 10 | Quit to title | Save and Quit | Done (56fb200); not tested that a second launch of the same world loads the changes | 2 |
| 11 | Eat an animal | Raw meat drops, eating with the animation and sound | Eating code exists (McPlayerPawn bEating); no held-item eat animation in the hand yet (the hand just stays up) | 2 |
| 12 | New world creation | World name, game mode, difficulty, seed, "More World Options" | Create New World exists (McHUDMenus); whether it offers game mode/difficulty/seed I did not check | 1-2 |
| 13 | Black boxes on the horizon | Mobs with models | Entities without a model show as black/magenta boxes (seen in my shots, e.g. hand_block.png top right; 689dcd3 now draws magenta + logs once per kind). Which kind it is I did not find | 2 |
| 14 | Translucent blocks against each other (glass behind glass, stained glass) | Sorted per face | Sorted per chunk mesh only; water vs ice/glass fixed in afb0d6c | 1 |

## Not examined
Crafting recipes book (the vanilla recipe book button), the achievements/advancements screen (not built), maps,
fishing, elytra, totems (the queued features), villager trading UI details, sprint FOV change, underwater overlay and
fog, damage tilt/red flash, hunger-bar shake, chat history.

## Suggested fix order
1. In-world Q drop (small, daily use). 2. F3/F5/F1/F2/F11. 3. Crack overlay + target outline (confirm in a shot first).
4. Beds and sleeping + respawn point. 5. Death screen buttons/score. 6. Sounds audit with the game running.
