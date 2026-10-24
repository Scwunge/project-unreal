# Minecraft in Unreal: a private client for Java Edition 26.3

Status: **M0 in progress.** Written 2026-10-24. The repo has the module scaffold (`McClient.uproject`,
`Source/`), not yet opened in Unreal, and `tools/fetch_cache.py`, which has filled all of `cache/`
(assets, server, data reports and the client jar's assets). Only opening the project in Unreal is left.

Private project, not for release. The repo holds only our own code. Everything of Mojang's (textures,
models, sounds, data) is read from your own installed copy at runtime into a local cache that git ignores.

## 1. The idea

**The real vanilla server runs the game; Unreal is the client.**

- World generation, mobs, redstone, crafting, loot and every rule are Mojang's own code, running unchanged in
  the official `server.jar`. That's what makes gameplay 1:1 by construction, and it stays 1:1 when the
  version moves.
- Singleplayer works the way Minecraft's own does: a server runs locally in the background (offline mode,
  `127.0.0.1`) and the client connects to it.
- The Unreal client does what the vanilla client does: rendering, input, sound, UI, and predicting your own
  movement.
- **No decompiled or translated Mojang code**, in any language. The client is written from the public
  protocol documentation, Mojang's own data generator output, and watching the real game.

## 2. Target

| | |
|---|---|
| Minecraft | **Java Edition 26.3** (released 2026-09-15) |
| Java | 25 (the launcher's `java-runtime-epsilon`) |
| Asset index | 34 |
| Protocol number | **777** (world version 5023), read from `version.json` inside `server.jar` |
| Unreal | 5.8 |

Stay on 26.3 until at least M6. Snapshots (26.4) change the protocol often. Nothing protocol-specific is
hardcoded: the packet tables are generated, so a version move means regenerating them, not a rewrite.

**Still needed for M0** (as of 2026-10-24):

- Install Unreal 5.8 (none was found on this PC) and check that the editor opens `McClient.uproject`.

The cache is done: protocol 777, 1,286 blocks with 35,723 states, 95 registries, 260 packets, 11,604 client
jar files (1,288 blockstates, 4,192 models, 218 `.mcmeta` animations) and 5,147 asset objects. The client jar
comes from Prism Launcher's copy when the official launcher has none; the script checks its sha1 either way.

## 3. Architecture

```
 Unreal 5.8 client (C++)                         Vanilla server.jar (Java 25)
 ─────────────────────────                       ────────────────────────────
 McNet     TCP, VarInt framing, zlib, packets ◄──► the real game logic
 McWorld   chunk sections, palettes, light        singleplayer: started locally,
 McAssets  reads YOUR client jar + assets index     offline mode, on 127.0.0.1
 McRender  section meshing, texture array, sky    or any server you run
 McPlayer  movement prediction, input, camera
 McEntity  entity models, animation, interpolation
 McUI      HUD, inventory screens, chat, menus (UMG)
 McAudio   OGG from your assets, sounds.json events
```

### Where the data comes from (all local, all in `cache/`, all gitignored)

- **Mojang's data generator:**
  `java -DbundlerMainClass=net.minecraft.data.Main -jar server.jar --reports`
  - `blocks.json`: every block and block state, with state IDs.
  - `registries.json`: registry contents and IDs.
  - `packets.json`: packet IDs for each protocol state and direction.
- **The client jar** (`%APPDATA%\.minecraft\versions\26.3\26.3.jar`): blockstates and model JSON, textures and
  `.mcmeta` animations, GUI sprites (with nine-slice data), fonts, lang files and colormaps.
- **The assets index** (`.minecraft\assets\indexes\34.json` plus `objects\`): sounds and `sounds.json`, by hash.
- **Entity and block-entity geometry.** Java Edition defines these models in code, not JSON. The fix is a small
  Fabric dev mod, run once in your own 26.3, that dumps the baked model layers to JSON in `cache/`. Look at
  Entity Model Features' export first.

### Modules

- **McNet.** `FSocket` on its own `FRunnable` thread. VarInt framing, zlib compression above the server's
  threshold. AES/CFB8 encryption only when online mode is added (see decisions). States: Handshake, Status,
  Login, Configuration, Play. Packet structs and readers are generated from `packets.json` plus the field
  layouts in the protocol docs on minecraft.wiki (formerly wiki.vg). Network NBT reader, text components,
  item data components.
- **McWorld.** Chunk columns of 16x16 sections over the dimension's height (the overworld is -64 to 320,
  24 sections; take it from the dimension type sent during Configuration). Paletted containers for block
  states and biomes, heightmaps, sky and block light per section from the server. Applies block updates
  and section updates.
- **McAssets.** Opens the jar and the assets index, and fills `cache/` on first run. Bakes every block state into
  a quad list: model elements, rotations, UV lock, multipart states, cull faces, tint index, the AO flag.
  Builds one texture array (nearest filtering, mips) and the animation table from `.mcmeta`.
- **McRender.** A section mesher on worker threads (UE Tasks). Faces against opaque neighbours are skipped,
  and vanilla-style smooth lighting and AO are worked out per vertex from the server's light values and
  stored in vertex colours. Meshes go up through RealtimeMeshComponent Core (MIT). Separate opaque,
  cutout and translucent passes, the last one sorted. Biome tint blending, fluids (heights from fluid
  levels, flow direction), the block crack overlay and the selection outline. No Nanite.
- **McPlayer.** Movement prediction: gravity, drag, sprint, sneak, swimming, ladders and step-up, with
  collision from block collision shapes. Digging (progress, crack stages), placing, hotbar, first and
  third person.
- **McEntity.** Entities from the dumped model layers, positions blended between server updates, walk
  cycles, head look, attacks. Item entities, and block entities: chests, signs, beds, banners, shulker boxes.
- **McUI.** UMG. HUD (hearts, hunger, armour, XP, the hotbar, the crosshair), chat, every container menu,
  title and pause menus, all drawn with your jar's GUI sprites and font.
- **McAudio.** Decodes OGG at runtime (stb_vorbis, public domain, or the RuntimeAudioImporter plugin; check
  its licence first) and plays `sounds.json` events, positioned, plus music.

## 4. Milestones

| # | What | Done means |
|---|---|---|
| **M0** Scaffold | UE 5.8 C++ project and modules; a script that fetches `server.jar` for 26.3, reads its protocol number, runs the data generator, and reads your client jar and assets index into `cache/` | the editor opens; `cache/` holds the reports, models, textures and sounds |
| **M1** Protocol | Handshake, then Login (offline mode), then Configuration (registries, tags), then Play; keep-alive; NBT; generated packet code | connects to a local vanilla 26.3 server and stays connected; chat works both ways |
| **M2** Chunks | Paletted sections, heightmaps, light, block updates; the threaded mesher; greybox materials | spawn terrain renders; edits made from the server console appear |
| **M3** The look | Model baker, texture array and animations, biome tints, smooth lighting and AO, fluids, the translucent pass | side-by-side screenshots against real 26.3 at the same seed, position and time look the same |
| **M4** Playing | Movement prediction, collision, digging, placing, hotbar | walk, dig and build with **no server corrections** (correction packets are the score) |
| **M5** Entities | The model dump mod, entities, animation, item entities, block entities | mobs move and fight correctly; chests open |
| **M6** Screens | Inventory, crafting, furnace, chests and every container menu; HUD, chat, title and pause menus | survival works from start to finish |
| **M7** Sky and sound | Sky colour, sun, moon, stars, clouds, weather, fog by biome, Nether and End effects; positional sound and music | the Nether, the End and a thunderstorm look and sound right |
| **M8** Singleplayer | A world list; the client starts and stops the local server with the launcher's Java 25, creates worlds and takes seeds | "New World" works like the real thing |
| **M9** Unreal extras | An optional enhanced mode (Lumen, real shadows, volumetric fog), kept apart from the 1:1 vanilla mode | toggles between the two |

## 5. Testing

- **Screenshot pairs.** Fixed spots, seeds and times, shot in real 26.3 and in this client, compared side by side
  for every M3 and M7 change.
- **Correction counter.** Count every position correction the server sends. It's the measure of movement
  prediction in M4 and after.
- **A bot.** mineflayer (MIT) on the same local server, to set up scenes, move mobs and change blocks
  around the player on cue.
- **Protocol fuzzing.** Replay captured sessions through McNet in a headless test and check that every
  packet is read with no bytes left over.

## 6. Hard parts, in order of pain

1. **Movement prediction (M4).** Any mismatch means rubber-banding. The bot and the correction counter are
   how it gets fixed.
2. **The model baker (M3).** Mostly mechanical, with a long tail of edge cases: UV lock, multipart states,
   AO flags, tint indexes, rotated elements.
3. **Entity models (M5).** They need the dump mod, and the animation logic has to be recreated by watching the
   real game.
4. **Lumen with runtime meshes (M9 only).** RealtimeMeshComponent's distance-field and Lumen card support is
   in its paid Pro edition. The 1:1 mode doesn't need it: it uses Minecraft's own light values.
5. **Version churn.** Contained by generating the packet tables from the data generator.

## 7. Decisions

1. **Version:** 26.3 (decided 2026-10-24).
2. **Online servers:** a Microsoft sign-in needs an Azure app ID that Mojang has approved, and approval for a
   hobby project isn't certain. Start with offline-mode servers you run yourself; revisit after M8.
3. **Repo:** this folder, kept private and apart from `enderbio` (whose `plans/` ships with the site).
   FormaTerra's `realms-engine` (Rust) is a reference for the patterns: its threaded mesher, lighting and
   net thread.

## 8. References

- Unreal Engine 5.8: https://wnhub.io/news/engines/item-51157
- RealtimeMeshComponent (Core is MIT): https://github.com/TriAxis-Games/RealtimeMeshComponent
- Minecraft EULA: https://minecraft.net/en-us/eula
- Data generators: https://wiki.vg/Data_Generators
- Running the data generator: https://minecraft.gamepedia.com/Tutorials/Running_the_Data_Generator
- 26.3 server notes (Java 25): https://space-node.net/blog/minecraft-26-3-server-update-guide-2026
- Microsoft authentication: https://minecraft.wiki/w/Minecraft_Wiki:Projects/wiki.vg_merge/Microsoft_Authentication_Scheme
- Entity Model Features: https://modrinth.com/mod/entity-model-features
