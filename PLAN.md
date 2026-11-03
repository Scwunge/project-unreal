# Minecraft in Unreal: its own game, playing like Java Edition 26.3

Status: **M0 to M3 done; M4 world generation started** (2026-10-26, branch `m1/blocks-and-chunks`).

- **M2** draws the real block models and textures, with sky and block light, smooth lighting, AO, water and
  lava.
- **M3** has Minecraft movement and collision (exact shapes, swimming, climbing, cobwebs), breaking and
  placing with the hotbar and crosshair, and a listen server that streams chunks and edits to clients.
- **M3 leftovers:**
  - Item drops (need item entities, M6/M7).
  - Other players' bodies (in progress).
  - Mining time (M7).
  - Server-side movement checks.

- **M9** (2026-10-28):
  - **Starting the game.** With no `-Mc` switches the game opens on the title screen: Singleplayer (the
    world list, New World with name, game mode, world type and seed, Delete) and Multiplayer (name and
    address).
  - **The pause menu.** Esc in a world opens it, with Save and Quit to Title (or Disconnect on a client).
  - **Worlds.** They live in `Saved/Worlds/<folder>`, and `level.json` now holds the world's name and game
    mode.
  - **The dedicated server.** Run it with `tools/run_server.ps1`. Its settings are in `Saved/Server`:
    `server.properties` (gamemode, level-name, level-seed, level-type, max-players, motd, server-port,
    white-list), `allowlist.json` and `ops.json`, matched by player name since there are no accounts.
    Ops get past the allowlist, and past the player limit with `bypassesPlayerLimit`. The default port
    is 25565, the same port the join screen uses when an address doesn't give one.
  - **Not done:** a packaged `McClientServer` build needs a source-built engine, because the launcher's
    can't build Server targets, so the script runs the editor binary with `-server`.
  - **Dev switches:**
    - `-McTitle` shows the title screen even with other switches.
    - `-McMenu=worlds|newworld|join|pause` opens that screen.
    - `-McMenuPress=Singleplayer,Create_New_World` presses buttons in turn.
    - `-McName=` sets the player name.

- **M10** (2026-10-28):
  - **Two looks, switched in game.** F7, or the pause menu's Graphics button, switches between vanilla and
    enhanced at any time; the choice is kept in `Saved/options.json`.
  - **Vanilla** is unchanged: unlit blocks with the game's own light curve, no tonemapper.
  - **Enhanced** swaps the block, entity and player materials for lit twins (`M_McBlock*Lit`, made by
    `tools/make_materials.py`). It adds:
    - a sun light that follows the day cycle and becomes the moon at night, with virtual shadow maps;
    - Lumen global illumination and reflections;
    - volumetric fog, bloom, SSAO and the filmic tonemapper.
  - **What the chunks still supply.** Sky ambient comes only where the chunk's sky light reaches, so caves
    stay dark. Block light (torches, lava) is added as warm emissive.
  - **Not done:**
    - The Nether and the End have no sun, so enhanced mode lights them only by block light.
    - Lumen traces the screen only, because the procedural meshes have no distance fields.
    - Clouds, rain and the sky stay unlit.
  - **Dev switches:**
    - `-McEnhanced` starts in enhanced mode.
    - `-McSunLux=` and `-McEnhancedExposure=` are for tuning.
  - **Unreal Mode** is the enhanced look under its player-facing name: the Options screen (from the title and
    pause menus) has an "Unreal Mode: ON/OFF" button, and F7 still switches it. On top of the above it has:
    - **Surfaces.** The mesher marks each vertex's kind in UV1.y (`McSurface`), and the lit materials act on it:
      water waves, glosses and turns to a mirror at a slant; lava glows; leaves and plants sway; light-giving
      blocks glow; every other face is bump-mapped from its own texture and goes wet in rain.
    - **Particles** (`UMcParticleComponent`): block-break debris, dig chips, torch, campfire and furnace flames
      and smoke, lava drops and sparks, falling leaves. There are more of them in Unreal Mode; the vanilla look
      draws a few of the same. Bits of a block's texture use a flat normal, because the bump path left them black.
    - **Block lights** (`UMcBlockLightComponent`): point lights at the nearest light-giving blocks.
    - **Post grade**: bloom, saturation, contrast, vignette, fringe, Lumen quality, sun shafts, contact shadows
      and fog inscattering.
    - **Immersive audio** (`UMcAudioSubsystem` and `McAcoustics`, vanilla sounds only; vanilla mode sounds as before):
      a reverb that follows the space (12 rays a few times a second: closet = short and dry, cavern = long echo,
      open sky = almost none, Nether = always big and dark); sounds behind solid blocks are quieter and low-passed
      (block ray, refreshed every few frames); under water everything is low-passed and the vanilla
      `ambient.underwater.*` enter, loop, bubbles and exit play; far sounds lose highs (air absorption) and use
      HRTF only if a spatializer plugin is already enabled; rain is muffled by the sky above; drips from pointed
      dripstone and water or lava in ceilings; far mobs in caves echo; steps scale with speed, a harder landing
      thud, and music and ambience duck for explosions and thunder. The rules are pure and tested in
      `McClient.McAudio.Acoustics`. There is no wind sound in vanilla, so altitude wind is not done.
    - **Cave mood** (`ambient.cave`, both looks): a meter fills in the dark (block light 7 or less, no open sky) and
      the sound plays 8 blocks or less away at a dark spot after 6000 ticks in vanilla; in Unreal Mode it is quicker
      (2400 ticks), 8 to 18 blocks out in the dark and sent through the big reverb. Sky light is approximated
      (open sky counts as light), so night on the surface does not trigger it.
    - **HD Textures** (a second switch on top of Unreal Mode: Options "HD Textures: ON/OFF", saved as `hdTextures`,
      `-McHd` to force it; greyed out with Unreal Mode off, and a runtime switch rebuilds the materials like the
      Unreal Mode one). Everything is made on this machine from the textures already in `cache/`: no downloads,
      no generated art, nothing committed.
      - **Generator** (`McHdTextures`, pure functions, tested in `McClient.McAssets.HdScaler/HdClasses/HdMaps`): an
        edge-aware 2x upscaler (Scale2x rule plus a similarity-limited blend) repeated from 16 px to 64 px
        (`-McHdSize=128` and up to 512), then detail by material class (picked from the block tags and names:
        stone cracks and pits, wood grain along the trunk or boards with seams and rings on log ends, dirt, sand
        and gravel grains, brushed metal, ore with the gems raised well above sunken stone and shiny, leaves and
        plants with veins and their transparency kept, wool fibre, clear glass). The maps follow the LabPBR
        layout: normal (x, y, occlusion, height), specular (smoothness, F0 with 230+ as metal, emission).
      - **Cache:** the first use builds the arrays and writes `Saved/HdCache/Blocks.bin`, keyed by the generator
        version, size, layer list and the 16 px inputs; a start with nothing changed loads the file. Animated
        layers (water, lava, fire...) are redone from each new frame.
      - **Materials:** `M_McBlock{Opaque,Cutout,Translucent}Hd` (`tools/make_materials.py`, `MC_ONLY_HD=1`) read the
        three arrays filtered with mips and anisotropy; opaque blocks add parallax occlusion mapping (14 steps,
        0.12 block depth) from the height, all use metalness, occlusion and the map's emission. Water, lava, sway,
        glow, particles and wetness are the same as the Unreal Mode twins.
      - **Skins and mobs:** player skins and mob textures go through the same pipeline (upscale, light detail,
        normal, roughness; UVs unchanged, so the models are too). `-McSelfShot=<blocks>` puts the camera in front
        of the player for a skin picture, and `-McSpawnMobs=cow,4` a few mobs near the player.
      - **Resource packs: later** (zip and LabPBR reading are not built; the arrays use the same layout so a
        pack's maps could be dropped in).
  - **Dev switches:** `-McMenu=options` opens the Options screen; `tools/scenes/unreal.json` is a look-test scene
    (`-McScene=unreal -McTime=18000` for torch light). Take shots with `-McShotDelay=12`: later ones drift.

- **Chat and commands** (2026-10-28):
  - **Chat.** T opens the chat box and / opens it with a command started. Enter sends, and Up and Down go
    back through what was sent. Lines show above the hotbar and fade after ten seconds. Joining and leaving
    are announced in multiplayer.
  - **Commands** (`McCommands`, run on the server) use the jar's own reply text (`McLang`, from
    `en_us.json`). The commands are /clear, /difficulty, /effect, /enchant, /fill, /gamemode, /gamerule, /give,
    /help, /kick, /kill, /list, /me, /msg (/tell, /w), /op, /deop, /say, /seed, /setblock, /summon, /teleport (/tp),
    /time, /weather, /whitelist and /xp (/experience).
  - **Difficulty and game rules** are saved in `level.json`. Difficulty sets hunger's harm and stops
    /summon making monsters on peaceful. Peaceful also removes the monsters and stops them spawning. These
    rules change the game: doDaylightCycle, doWeatherCycle, doMobSpawning, doMobLoot, doTileDrops,
    keepInventory (inventory and experience kept on death), naturalRegeneration, fallDamage, fireDamage,
    drowningDamage and randomTickSpeed (`McGameRules` reads them; the others are kept but do nothing yet).
  - **Permission levels.** The host of a world has every level. On a dedicated server a player has the
    level `ops.json` gives them.
  - **Selectors.** @s, @p, @a, @e, @r and player names work. Selector arguments such as `[type=...]` don't.
  - **Effects** (2026-10-29): `McEffects` keeps a player's status effects in `FMcPlayerStats.Effects` (saved,
    stats version 2). Working: regeneration, poison (stops at half a heart), wither, hunger, saturation,
    instant health and damage, absorption, health boost, speed, slowness, jump boost, haste, mining
    fatigue, strength, weakness, resistance and fire resistance. The rest are kept and shown only. The icons
    show along the top right (blinking in the last ten seconds). `/effect give|clear` and `/enchant` (which
    checks the item, the level and what it clashes with) are in. Potions, food and beacons don't give effects yet.
  - **Not done:** /spawnpoint, /clone, /execute, data tags, selector arguments, and tab
    completion.
  - **Dev switch:** `-McChat="/time set night|hello"` sends those lines three seconds in.
- **Creative inventory** (2026-10-28):
  - E in creative opens the search tab: every item in registry order, 9 by 5, searched by the jar's names
    (typing goes to the search), scrolled with the wheel or the bar, over the hotbar.
  - Click takes one, middle-click a stack, and shift-click puts a stack in the inventory. Anything put back
    on the list is destroyed. The server checks each take (`ServerCreativeTake`): the player must be in
    creative and the item must exist.
  - **Not done:** the other tabs (building blocks, colored blocks and so on, whose contents are in the game's
    code rather than its data), the survival-inventory tab with armour, and the destroy-item slot. Operator
    items are left out by a fixed list.
  - **Dev switch:** `-McInventory` opens the inventory once the world is up, and `-McInventory=log` also
    searches.
- **Enchanting table** (2026-10-28):
  - Using a table opens it: an item and lapis lazuli go in, and three options are offered. Their costs come
    from the bookshelves around the table (`McEnchanting::CountBookshelves`: the ring two blocks out, on the
    table's level and one up, with the block between clear, at most 15) and from the player's enchantment seed.
  - The seed changes only when the player enchants, so offers stay put until then. Client and server work
    out the same offers from the shelf count and the seed, which travel with the window.
  - What an option gives (`McEnchanting::Select`): the enchantments in `in_enchanting_table` that suit the
    item, picked by weight at the power the cost and the item's enchantability give. Each further pick
    halves the power. A book becomes an enchanted book and gets one fewer.
  - Option N takes N lapis and N levels, and needs its cost in levels. Creative players pay nothing. The
    server checks and does the enchanting (`ServerEnchant`).
  - Hovering an option shows its clue, plus the lapis and levels it takes or the level it needs.
  - Each option shows three or four made-up words in the standard galactic alphabet (`ascii_sga.png`), the
    same ones until the enchantment seed changes (`McGui::Enchanting::GetGalacticNames`).
  - **Not done:** the book model above the table, and its particles.
  - **Dev switches:** `-McUse=X,Y,Z` uses that block five seconds in. `-McShiftClick=29,30` then
    shift-clicks those slots of the window it opens.
- **Loom** (2026-10-29), in `FMcLoomMenu`, following minecraft.wiki's "Loom": a banner, a dye and optionally a banner
  pattern item. With a banner and a dye in, the 32 patterns that need no item are offered (or, with a pattern
  item, its own), 4 by 4 and scrolled with the wheel, each drawn as a small banner in the dye's colour. The one
  chosen gives the banner with that layer added (`minecraft:banner_patterns`). A banner with six layers takes
  no more. Taking the result uses one banner and one dye; the pattern item stays.
  - **Not done:** drawing banners with their patterns as items or in the world, and the cartography table (it
    needs maps first).
- **Stonecutter, grindstone and smithing table** (2026-10-28), in `McWorkstationMenus`:
  - **Stonecutter.** Its recipes for the input are shown 4 by 3, scrolled with the wheel, sorted by what
    they make. Clicking one chooses it; it stays chosen while the same item is in. A menu button goes to
    the server as `ServerMenuButton`, and the choice comes back with the window (`FMcMenu::SerializeState`).
  - **Grindstone.** One item loses every enchantment but its curses. Two of the same damageable item join,
    adding their durability plus 5%. Taking the result gives between half and all of the enchantments'
    minimum costs as experience (`FMcMenu::TakeExperience`).
  - **Smithing table.** Template, base and addition make the transform or trim result from the recipe book.
    Shift-click puts each item in the slot whose part of some recipe takes it.
  - **Not done:** the smithing table's empty-slot pictures and armour-stand preview, and the grindstone's
    ghost icons.
- **Anvil** (2026-10-28), in `McAnvilMenu`, following minecraft.wiki's "Anvil mechanics":
  - Repair with the item's material: each one mends a quarter, for a level each.
  - Joining two of the same item: their durability added plus 12%, for 2 levels.
  - Enchantments merge: equal levels go up one. Each costs its anvil cost (halved from a book) times its
    final level. One that clashes is left off for a level.
  - A rename costs a level. The item's prior-work penalty is added, and the result's penalty becomes
    twice the higher input's plus 1.
  - A cost of 40 or more is "Too Expensive!", except in creative. A rename alone is capped at 39.
  - Typing goes to the name field (`ServerMenuText`). The menu is told the player's levels before each click
    (`PrepareMenuClick`). The levels the result costs are taken from the player (`TakeLevelsSpent`).
  - Each result taken has a 12% chance to wear the anvil a step: chipped, damaged, then gone.
  - The name field starts at the item's name (custom, else its usual one); the usual name typed back sends no
    custom name, so the menu sees it as taking the name off (`McGui::Anvil`).
- **Brewing stand** (2026-10-28), in `McBrewingStand`, following minecraft.wiki's "Brewing Stand":
  - Three bottle slots, the ingredient and the blaze powder fuel. A powder gives 20 brews and is taken when
    the stand runs out.
  - A brew takes 400 ticks. It starts when the ingredient makes something of at least one bottle (the recipe
    book's brewing recipes) and stops if the ingredient is taken or changed. When it ends, every bottle it
    fits is brewed and one ingredient is used.
  - It is a machine like the furnace (`FMcMachine`). `UMcBlockUseSubsystem` keeps furnaces and brewing stands
    alike, working with nobody looking and saved as block entities.
  - The screen shows the fuel bar, the arrow filling as it brews and the rising bubbles.
  - **Not done:** the stand block showing its bottles, and the sound when a brew finishes.
- **Hopper** (2026-10-28), in `McHopper` and `UMcBlockUseSubsystem`, following minecraft.wiki's "Hopper":
  - Five slots, opened like a chest. Placed, its spout points into the block clicked on (down from a top or
    bottom face).
  - Every 8 ticks after a move it puts one item into the block it faces and takes one from the block above, or
    picks up an item lying on it. A powered hopper (`enabled=false`) holds still.
  - Chests, barrels and hoppers give every slot. Furnaces take into the input from above and fuel from the
    side, and give their output below. Brewing stands take the ingredient from above and bottles and fuel from
    the side, and give the bottles below.
  - A powered hopper is locked (redstone sets `enabled`).
  - **Double chests** (2026-10-29), following minecraft.wiki's "Chest": a chest or trapped chest placed beside a single one
    of the same facing joins it (`type` left or right; a right half's partner is clockwise of its facing). Both
    halves open as one 54-slot "Large Chest", the right half's slots first (the order is an assumption: the wiki
    doesn't say), and taking one half away leaves the other single. A hopper or dropper still sees one half.
  - **Comparator readings** (2026-10-29), following minecraft.wiki's "Redstone Comparator": besides containers (a double
    chest reads both halves as 54 slots) it reads a cake (2 per bite left), composter and cauldron level, an end
    portal frame with an eye (15), a respawn anchor (0, 4, 8, 12, 15), and a beehive or bee nest (3 per honey level).
  - **Not done:** hopper minecarts, furnace, brewing stand, jukebox, lectern and item frame readings, and hoppers
    seeing a double chest as one.
- **Dispenser and dropper** (2026-10-28), following minecraft.wiki's "Dispenser" and "Dropper":
  - Nine slots. Powered (quasi-connectivity included, as for pistons), it is `triggered` and fires 4 ticks
    later, once a pulse, taking one item from a random slot.
  - A dropper puts the item into a container in front through the face it meets (nothing if it is full), or
    throws it out.
  - A dispenser shoots arrows, pours or fills buckets, uses bone meal on the block in front, and throws
    anything else out.
  - A fire charge or flint and steel lights the air in front (charge used up, steel worn by one); a shulker
    box is placed in front, facing the way the dispenser does. A blocked front does nothing.
  - **Not done:** dispensing TNT, armour, minecarts and boats, thrown items, spawn eggs and the rest of the item
    behaviours.
- **Buckets** (2026-10-28), in `McBuckets`: an empty bucket aimed at a water or lava source takes it (the aim
  passes through fluids, so it looks for the source). A full one pours into the block clicked if that can be
  replaced, else beside it. Water boils away where the dimension's `gameplay/water_evaporates` says so (the
  Nether). Creative keeps the bucket as it was.
  - Right-clicking a workstation, brewing stand, hopper, dispenser or dropper now opens it in play; only the
    crafting table, chests and furnaces did before.
  - **Not done:** buckets of fish, axolotls and powder snow, milk, and waterlogging blocks.

### M4 build order

The 26.3 worldgen data (`cache/client/data/minecraft/worldgen`) is the source of truth. The overworld biome
table comes from `cache/data/reports/biome_parameters`.

26.3 changed the formats:
- Noises have `base_octave`, `octave_count`, `base_amplitude` and `normalize`, and are evaluated in
  single precision.
- `gradient`, `lerp`, `find_top_surface`, `interval_select` and `distance_to_point` replace older types.
- Surface rules live in `material_rule` and `material_condition`.

The wiki doesn't document the noise maths exactly, so noises are built to the documented semantics
(normalized fBm whose expected amplitude is `base_amplitude`) and calibrated against world statistics. They
are not seed-matched.

1. **Random and noise** (McGen): a seeded random (Xoroshiro128++), improved Perlin noise, octave noise per
   the noise JSON, and simplex noise for the End.
2. **Density functions:** parse every type the data uses into a graph and evaluate it, with caches and cell
   interpolation. Test that every density function file loads.
3. **Noise chunk generator:** `final_density` to stone, water and air by cells, aquifers and ore veins. This
   replaces the flat generator for new worlds.
4. **Biomes:** a multi-noise lookup over the report's parameter points, stored per 4x4x4 cell in chunks and
   used for tints.
5. **Surface rules:** grass, dirt, sand, snow, badlands bands and deepslate from `material_rule`.
6. **Carvers:** caves and canyons.
7. **Features:** placed features by biome and step (ores, trees, plants, lakes, and so on); the long tail.
8. **Structures:** jigsaw template pools (villages and others) and fixed ones.

Private project, not for release. The repo holds only our own code. Everything of Mojang's (textures,
models, sounds, data) is read from your own installed copy into a local cache that git ignores.

## 1. The idea

**Unreal is the whole game: client, server, simulation and hosting.**

- It plays the same as Java Edition 26.3: the same blocks, items, crafting, mobs, world shapes and rules.
  The goal is that someone who knows Minecraft notices no difference while playing.
- It is not seed-exact. Worlds from the same seed won't match vanilla, and fine details (redstone timing
  edge cases, mob AI quirks) will drift and be closed one by one.
- It uses no Mojang code, servers or services: no `server.jar` at runtime, no Minecraft protocol, no Mojang
  or Microsoft accounts. Multiplayer and hosting use Unreal's own networking and dedicated server.
- **No decompiled or translated Mojang code**, in any language. Behaviour comes from minecraft.wiki, from
  the data in your own 26.3 jar, and from watching the real game side by side.
- The real game stays the reference: 26.3 in Prism Launcher for comparison, and the vanilla server host on
  branch `claude/server-host` for scripted comparisons (see decisions).

### Much of the game is data, not code

Your 26.3 jar ships most of the content as JSON, which the game reads directly instead of us retyping it:

| In `data/minecraft/` | Files | Used for |
|---|---|---|
| `recipe/` | 2,042 | crafting, smelting, stonecutting, smithing |
| `loot_table/` | 1,447 | block drops, mob drops, chests |
| `tags/` | 883 | block and item groups the rules refer to |
| `structure/` | 1,511 | NBT templates: villages, temples, trial chambers |
| `worldgen/` | ~1,100 | noise settings, 55 density functions, 67 biomes, 240 features, 273 placed features, structures, template pools |
| `advancement/`, `enchantment/`, `damage_type/`, `dimension_type/`, variants, trades | ~2,700 | the rest |

The code we write is the engine that interprets this data: the density function evaluator, the feature
placers, the recipe matcher, the loot table roller. The wiki documents what each data type means.

## 2. Target

| | |
|---|---|
| Behaviour reference | **Java Edition 26.3** (released 2026-09-15) |
| Asset index | 34 |
| Unreal | 5.8 |
| Java | 25, only for the reference tools (data generator, reference server), never for the game |

Stay on 26.3. A version move means refreshing `cache/` and fixing what changed in the data, not chasing a
protocol.

**Still needed for M0** (as of 2026-10-24):

- Install Unreal 5.8 (none was found on this PC) and check that the editor opens `McClient.uproject` and that
  all three targets build: `McClient`, `McClientEditor` and the dedicated server `McClientServer`.

Done: the scaffold has the twelve modules below (empty, never compiled). McAssets, McRender, McUI and McAudio
are client only, left out of the dedicated server by `TargetDenyList` in the `.uproject` and by
`McClient.Build.cs`. The cache is complete: 1,286 blocks with 35,723 states and 95 registries from the data
generator, 11,604 client jar files from `assets/` (1,288 blockstates, 4,192 models, 218 `.mcmeta`
animations), 9,884 from `data/` (1,144 of them worldgen) and 5,147 asset objects. The client jar comes from
Prism Launcher's copy when the official launcher has none; the script checks its sha1.

## 3. Architecture

```
 Unreal 5.8 game (C++)
 ─────────────────────────────────────────────────────────────────────────────
 Server side (authoritative; runs in the dedicated server and the listen server)
   McCore    registries: blocks, states, items, tags, recipes, loot tables
   McWorld   chunks, paletted sections, heightmaps, the light engine, saving
   McGen     world generation from the jar's worldgen data
   McSim     the 20 TPS tick: block ticks, random ticks, fluids, redstone, block updates
   McEntity  entities, physics, mob AI, combat, spawning
   McServer  hosting: listen server, dedicated server target, world folders, server settings

 Shared
   McNet     Unreal replication: chunk streaming, block changes, entities, inventories

 Client side
   McAssets  reads cache/: block models, textures, animations, sounds.json
   McRender  section meshing, texture array, sky
   McPlayer  input, camera, movement prediction, digging and placing
   McUI      HUD, inventory screens, chat, menus (UMG)
   McAudio   OGG from your assets, sounds.json events
```

Singleplayer is a listen server, the same code path as multiplayer, so hosting is never bolted on later.

### Where the data comes from (all local, all in `cache/`, all gitignored)

- **The client jar** (`26.3.jar`, or Prism's `minecraft-26.3-client.jar`):
  - `assets/`: blockstates and model JSON, textures and `.mcmeta` animations, GUI sprites, fonts, lang
    files and colormaps.
  - `data/`: recipes, loot tables, tags, structures, worldgen and the rest (see section 1).
- **The assets index** (`.minecraft\assets\indexes\34.json` plus `objects\`): sounds and `sounds.json`.
- **Mojang's data generator**, run once on the reference `server.jar`: `blocks.json` (every block state
  with its properties) and `registries.json`. These give the state list McCore is built from.
- **Entity and block-entity geometry.** Java Edition defines these models in code, not JSON. A small Fabric
  dev mod, run once in your own 26.3, dumps the baked model layers to JSON in `cache/`. Look at Entity Model
  Features' export first.
- **Things that aren't data:** block behaviour (what a piston, hopper or crop does), mob AI, physics
  constants, light rules. These come from the wiki and from measuring the real game.

### Modules

- **McCore.** Builds the block and item registries from `blocks.json` and the jar data: block states with
  their properties, hardness, tool tags, collision and outline shapes. Loads tags, recipes and loot tables.
- **McWorld.** Chunk columns of 16x16 sections over the dimension's height (overworld -64 to 320, from
  `dimension_type`). Paletted containers for block states and biomes, heightmaps. Our own light engine (sky
  and block light, propagated per section). Saving in our own region format.
- **McGen.** Interprets the worldgen data: the density function graph and noise settings for terrain, the
  multi-noise biome source, surface rules, carvers, features and placed features, and jigsaw structures from
  template pools. Runs on worker threads.
- **McSim.** The server tick at 20 TPS: scheduled and random block ticks, fluid spread, redstone, block
  updates and shape updates, gravity blocks, crops, fire, weather and time.
- **McEntity.** Entity physics and collision, mob AI (goals, pathfinding), combat and damage types,
  spawning rules, item entities, block entities (chests, furnaces, hoppers, signs).
- **McServer.** Hosting. A listen server for singleplayer and for opening a world to friends, a
  `McClientServer` dedicated server target, world folders, a settings file (port, max players, game mode,
  difficulty, allowlist, ops) and a console.
- **McNet.** Unreal replication. Chunks are too big for property replication, so sections stream as
  compressed blobs over reliable RPCs, nearest first, with block changes as small deltas. Entities replicate
  as actors. The client predicts its own movement and the server corrects it.
- **McAssets.** Opens `cache/` and bakes every block state into a quad list: model elements, rotations, UV
  lock, multipart states, cull faces, tint index, the AO flag. Builds one texture array (nearest filtering,
  mips) and the animation table from `.mcmeta`.
- **McRender.** A section mesher on worker threads (UE Tasks). Faces against opaque neighbours are skipped,
  and vanilla-style smooth lighting and AO are worked out per vertex from the light engine's values. Meshes
  go up through RealtimeMeshComponent Core (MIT). Separate opaque, cutout and translucent passes. Biome tint
  blending, fluids, the block crack overlay and the selection outline. No Nanite.
- **McPlayer.** Input, first and third person cameras, movement (gravity, drag, sprint, sneak, swimming,
  ladders, step-up), digging progress and crack stages, placing, the hotbar.
- **McUI.** UMG. HUD, chat, every container menu, title and pause menus, the world list and New World
  screen, all drawn with your jar's GUI sprites and font.
- **McAudio.** Decodes OGG at runtime (stb_vorbis, public domain, or the RuntimeAudioImporter plugin; check
  its licence first) and plays `sounds.json` events, positioned, plus music.

## 4. Milestones

| # | What | Done means |
|---|---|---|
| **M0** Scaffold | UE 5.8 project with the modules above; `fetch_cache.py` fills `cache/` with assets, data and reports | the editor opens; `cache/` holds the reports, models, textures, sounds and `data/` |
| **M1** Blocks and chunks | McCore registries from `blocks.json`; chunk storage; a flat world from `flat_level_generator_preset`; the threaded mesher with greybox materials; a free camera | a flat world of real block states renders and you fly over it |
| **M2** The look | Model baker, texture array and animations, biome tints, the light engine with smooth lighting and AO, fluids, the translucent pass | side-by-side screenshots of the same hand-built scene in 26.3 and here look the same |
| **M3** Playing, networked from day one | Player movement and collision, digging and placing with loot table drops, the hotbar; the listen server and McNet chunk streaming | walk, dig and build in singleplayer; a second client on the same PC joins and sees the same edits |
| **M4** World generation | Density functions, noise settings, biomes, surface rules, carvers, features, then structures | overworld terrain, caves, ores, trees and villages that look and feel like 26.3 (not seed-matched) |
| **M5** Simulation | Block ticks, fluids flowing, redstone, pistons, crops, gravity blocks, fire, day and night, weather | the redstone and farm test worlds behave the same as in 26.3 |
| **M6** Entities and mobs | The model dump mod, entity physics, AI and pathfinding, combat, spawning, block entities | mobs spawn, move and fight like in 26.3; chests and furnaces work |
| **M7** Survival | Inventory and every container menu, crafting from recipe JSON, smelting, enchanting, HUD, health, hunger, XP, death | survival works from start to finish |
| **M8** Sky, dimensions and sound | Sky, sun, moon, stars, clouds, fog by biome; the Nether and the End with their generation; portals; positional sound and music | the Nether, the End and a thunderstorm look and sound right |
| **M9** Saves and hosting | Our save format, the world list and New World screen, the dedicated server target, settings file, allowlist and ops, joining by address | "New World" works like the real thing; friends join a dedicated server |
| **M10** Unreal extras | An optional enhanced mode (Lumen, real shadows, volumetric fog), kept apart from the vanilla-look mode | toggles between the two |

## 5. Testing

- **Side-by-side pairs.** The same scene, built by hand or from a structure template, shot in real 26.3
  (Prism) and here, for every M2 and M8 change.
- **Behaviour checklists.** For each block and mob, a list of what the wiki says it does, ticked off in a
  test world. Redstone and farms get saved test worlds that both games can load as a build to compare.
- **Measured constants.** Movement speeds, jump height, fall damage, knockback and mining times measured in
  26.3 and asserted in automated tests.
- **Data coverage.** Automated tests that every recipe, loot table, tag, density function and feature in
  `cache/data/` loads and is understood, so nothing is silently skipped.
- **Reference server.** The vanilla host on `claude/server-host` can run a real 26.3 server to script
  comparisons (place the same blocks, step the same ticks) when a behaviour is unclear.
- **Network soak.** Two or more clients on a dedicated server, editing and moving, checked for desync.

## 6. Hard parts, in order of pain

1. **World generation (M4).** The density function graph is large and its evaluation has to be fast enough
   to generate chunks while you fly. Features and jigsaw structures have a long tail.
2. **Mob AI and pathfinding (M6).** Not data; each mob's behaviour has to be rebuilt from the wiki and from
   watching it.
3. **Redstone and block updates (M5).** Update order and timing are what make contraptions work; this is
   where "plays the same" is hardest to reach.
4. **The light engine (M2).** Has to be correct (it drives the look) and incremental (it runs on every edit).
5. **Chunk streaming over Unreal networking (M3).** Unreal replication isn't built for voxel data; McNet's
   custom streaming has to stay ahead of a sprinting or flying player.
6. **The model baker (M2).** Mechanical but long: UV lock, multipart states, AO flags, rotated elements.
7. **Lumen with runtime meshes (M10 only).** RealtimeMeshComponent's distance-field and Lumen card support is
   in its paid Pro edition. The vanilla-look mode doesn't need it.
8. **Held items on mobs (after the villagers branch merges).** Every mob-held item (zombie and skeleton weapons, the
   vindicator's axe, the pillager's crossbow, the witch's potion, the fox's mouth item, the allay) should go through the
   same item display-transform path as the player's hand on `main` (the models' `display` entries, e.g.
   `thirdperson_righthand`), and the flat-picture special cases in `McEntityModel` (`bFlat`, `bFlatTop`) should go.
9. **Entity shadows (cross-branch).** Vanilla draws a round, soft shadow under every mob (size from the hitbox width, fading with
   height above the ground and with light). Nothing on `m1/blocks-and-chunks` or here draws one; a vanilla ravager beside ours shows
   the gap. One shared decal pass for all entities, not per-mob code.
10. **Cat (mob), then the swamp hut's cat.** There is no cat mob yet. The hut places its witch and cat when the structure generates
    (`McStructureSpawns` already queues the witch; add the cat to the same queue once `minecraft:cat` exists).
11. **Audit: mob models and animations that may rest on memory, not on textures, shots or the wiki.** Rule: write a vanilla
    reference script, shoot vanilla at a matched camera, fit the pivots, tilts and constants to the shots (silhouette fit), recommit.
    Most visible first; tick one off when its commit lands.
    - [x] Wolf (standing, sitting, angry, tame): refitted to vanilla shots with `fov steady` at our FOV 70 (pivots, tilts, tail angles by mood). Left: sitting front legs a little short, a few px of framing, shadows (item 9).
    - [x] Mineshaft cave-crossing bridge reach: log pillars fitted (2026-11-03, `log shaft` scan of 12 vanilla mineshaft areas, radius 5
          chunks): longest run of log under planks 15 (x2, one shaft), then 12, 11; 9-11 common. Reach 15 = longest seen, medium
          confidence (pillar is limited by cave depth, no true cap known). Chains: none found in the scan, so the hung-chain reach
          stays a guess of 10 (unmeasured; check the scan's chain test or find a chain-hung bridge).
    - [x] Witch potion (2026-11-03, witchhold.txt vanilla front/side shots, head width 8 px as scale): flat sprite 6.3 px (6 drawn), centre
          0.6 px right of middle and 1.8 below the chin, tilt 12.5 degrees, mirrored, overlap 0.77 against the vanilla bottle outline.
          Depth z 6 chosen from our own shot (z 4 hid it behind the arm bar). Checked against vanilla front shot (hw5.png): cork under the
          nose, same tilt. Liquid added: potion_overlay x fixed tint 72/120/247 (read off the vanilla shot, 248 px; hw6.png matches). Side view not re-compared.
    - [x] Zombie villager held item (2026-11-03, `zvheld.txt`, vanilla zombie_villager with an iron sword): the model has the zombie's arms, so it takes the zombie's hold; a composite model (villager pictures laid together) now draws its held item as its own extra layer. Our front shot (zv2.png) shows the blade edge-on at the same spot as vanilla's. Spawn rate is the zombie's (still not logged for zombie villagers). Husk, stray, bogged, wither skeleton, piglins are not mobs here yet.
    - [x] Zombie and skeleton kin (2026-11-03, `variants1-5.txt`): husk, stray, bogged, wither skeleton are mobs and models. Measured on the reference: attributes (husk 20 hp / 0.23 / 3 / follow 35 / armor 2; stray 20 / 0.25 / 2 / 16; bogged 16 hp, the rest as stray; wither skeleton 20 / 0.25 / 2 / 16; also piglin 16 / 0.35 / 5 / 16, zombified piglin 20 / 0.23 / 5 / 35 / armor 2, brute 50 / 0.35 / 7 / 12), boxes (husk 0.6 x 1.95, stray and bogged 0.6 x 1.99, wither skeleton 0.7 x 2.4, piglin family 0.6 x 1.95), spawn gear of 100 each at Normal (stray, bogged: bow 100; wither skeleton: stone sword 100; husk: iron sword 2, none else; piglin: golden sword 38, crossbow 42, none 20; zombified piglin: golden sword 93; brute: golden axe 100), daylight burning after 100 ticks (stray and bogged burn, husk and the piglins do not; the wither skeleton's summon vanished in that run, treated as not burning). Model: husk drawn 1.07 times the zombie's size and wither skeleton 1.2 times the skeleton's, both from one row shot's pixel heights (good to ~0.03); stray and bogged wear their overlay picture as the body's boxes grown 0.25 (not fitted), bogged's mushrooms not drawn. Piglin, piglin brute and zombified piglin are mobs and models too (model boxes from the logged one, snout, tusks and ears included; shot pig2.png against the row shot matches, brute's black clothes are its jacket/sleeve/trouser layers; spawn gear as counted above, server-side). Effects (2026-11-03, `effects1-5.txt`: the player's `active_effects` read every 3 to 6 ticks after hits, one to three hits each, sparse): husk hunger 140 ticks at Normal and 280 on Hard (none seen on Easy), wither skeleton wither 200 (Easy to Hard alike), stray arrow slowness 600 ticks, bogged arrow poison 100 ticks; amplifier 0 assumed (the data lines showed no amplifier field). Done on the server (`OnPlayerEffect`, `FMcArrowEntity::Effect`). Not done: wither skeleton melee pose, the piglins' gold-armor rule, bartering, hoglin hunting, crossbow loading pose, nether spawning (zombified piglins and piglins never spawn naturally yet), zombified piglin anger sharing, mushrooms on the bogged.
    - [ ] Walk cycle shared by all legs and arms (`Swing`: 0.6662 rate, 1.4 amplitude, arm amplitude 1.0, `MaxLimbSwing` per mob).
          Measure stride from a vanilla walking-loop filmed at a fixed camera.
    - [ ] Villager / zombie villager / wandering trader: head pivot, nose, folded arms, arm bar; offering arm pitch (0.9 of a right angle).
    - [ ] Pillager / vindicator / evoker / illusioner poses: crossbow arms (-90 degrees, yaw 0.3 / -0.6, charging -0.9), axe arms
          (-2.3 / -1.0 plus a wave at rate 9), spell arms (roll 135 degrees, swing at 13 x 0.6662).
    - [x] Ravager: checked against `log model ravager` and side/front shots at the same distance (scene `rotations`, `-McMobRowAt=-21,-25`, camera 9 away):
          horn tilt 1.0996 (63 degrees) is the logged value, leg pivots match, head and body within about 1 px. Left: stun and roar poses; our jaw hangs
          about 1 px lower than vanilla's closed mouth (logged mouth turn 0.031). Shots need the `rotations` scene: in `-McFlat` an extra mob stands at the camera.
    - [ ] Vex: wing roll (0.7 + sin(40 t) x 0.5), body lean, charging pose, arm raise.
    - [ ] Witch: hat bend stack DONE (logged pivots and turns, matches vanilla shots). Left: nose wobble (logged: a smooth circle, xRot 0.0785 sin(0.0898 t) and
          zRot 0.0436 cos(0.0898 t), t in ticks from an unknown phase; about 0.3 px at the nose tip, so not worth a per-box animation yet), mole,
          drinking arm (needs a vanilla witch caught mid-drink to log).
    - [ ] Horse (and donkey, mule, llama, trader llama): body, neck tilt, head, tail, leg pivots, rear-up; saddle and armour layers.
    - [ ] Iron golem: arm pivot, leg swing cap (`MaxLimbSwing` 0.5), offering-poppy arm, cracks layer.
    - [ ] Enderman (leg swing cap 0.4, arms), skeleton, creeper (swell shake 1 + sin(100 s) s 0.01), spider (leg yaw 0.4, roll 0.4, rate 1.3324).
    - [ ] Pig, cow, sheep, chicken (wing flap rate 25, 0.08 rest roll), goat, fox, bee, rabbit, turtle, slime: boxes from the textures;
          pivots and animation constants as above.
   - **Horse DONE (2026-10-31):** placed from the logged pivots and cube extents (body 10x10x22, legs 4x11x4 with boxes set 1 in, front legs 0.1 forward, tail/neck at 0.5236, whole model 1.1 scale); side shot matches in shape and proportions, McRender tests pass (14/14). Not done for the horse: leg swing and head animations not re-logged (walk log needed), donkey/mule/llama/trader llama/baby horse not re-fitted. Still to audit: vex, golem, skeleton, spider, animals as listed above; witch nose shot-check (animation can't show in a still).
   - **Animals/mobs refit from the log (2026-10-31):** pig, cow, sheep, chicken, iron golem, spider, vex placed from logged pivots and cube extents (skeleton already matched); rotation order is now x, then y, then z (spider outer legs need it). Pig/sheep heads and hind legs were 2 px off, cow horns and chicken head/legs/beak had moved, golem head 2 px back, spider leg spread was 7.5 px (is 3), vex gained its second body box and 0.1571 lean. Still to do: cow muzzle box and sheep wool boxes (not in this log: dump the other model fields), walk/attack animation refits (spider yaw 0.4/roll 0.4/rate 1.3324, chicken wing flap, golem arms) need a walking log, then creeper, enderman, goat, fox, bee, rabbit, turtle, slime, baby models.
   - **Second batch from the log (2026-10-31):** enderman head (1 lower), goat (tipped head box with horns, legs), fox (head, body, tail, legs; the old head was 4 px too high), turtle (body/head/hind legs 2 px back), bee (1 lower, leg and antenna offsets), standing wolf (legs 1.5 either side of centre, head pivot x -1, boxes re-placed) are placed from logged pivots and cubes; creeper, slime and skeleton already matched. Not done: goat ears and beard (UVs unknown), rabbit (the log caught it mid-bob: needs a rest-pose log), sitting wolf, baby models, all walk/attack animation constants, shot checks of enderman/turtle/bee.
   - **Walking logs and the rest (2026-10-31):** walking amplitudes match ours for pig/cow/sheep/wolf/chicken legs (1.4 a point), spider (0.4 yaw, 0.4 roll, rate 1.3324 with pos += amount a tick), golem (1.5, triangle); enderman arms were 0.5, are 0.56 (0.8 of the legs' 0.7); vex wings (turn 1.1 +- 0.28 at 0.8 rad/tick, tipped 0.4712 each way) and arms (0.6283 +- 0.1, 65-tick period) fitted; sitting wolf, rabbit (a new model: 8x6x10 body, flat hind feet), goat ears/beard/leg pictures and the cow muzzle placed from the log (the reference mod now prints each box's picture offset as uvMinU/uvMinV, fractions of the texture). Not done: chicken and bee wing flap while airborne (a falling-chicken log came back all zero: the pose probably is not updated for an entity out of view or in this state), hop animation of the rabbit, baby models, vex charge and attack poses, golem offering/cracks, sheep wool boxes (layer models are not in the dump), horse walk constants, donkey/mule/llama.
   - **Baby models (2026-10-31):** horse, sheep, wolf, goat, fox, bee, rabbit, turtle, pig, cow, chicken have their own baby models from `log model` (every pivot, tilt, box, picture offset), registered as `<kind>_baby` (`bBabyModel`; `SetBaby` swaps the model, textures are the adult names + `_baby`; wolf tame/angry keep `_tame_baby`). Tests 14/14, side shots look right. Not done: baby sheep wool is the logged layer model (`scripts/wool.txt`; `log model` now dumps render-layer models and vertex extents: adult wool inflations 0.6/1.75/0.5 confirmed, baby wool = the baby's own boxes, no inflation), baby wolf has one tail angle and no sitting pose, donkey/mule/llama babies.
   - **Flap, hop, llamas (2026-11-01, `log_audit7`):** chicken wings in the air swing 0.06..2.0 about 1.02 at 1.798 rad/tick (fit to 42 logged ticks of a long fall; rest 0.1203, baby 0.0611), "in the air" = moving up/down more than 0.03 a tick; bee wings 0.4712 cos(2.1 t) with no rest roll; rabbit hop = the logged 13-tick curves (body, head, tail, front legs with their pivot shift, hind legs) started when it leaves the ground (hind legs lumped: the hind-leg child turn is not applied, haunch shapes unchanged). Llama grown model and the llama/trader llama/donkey/mule baby models are the logged ones; donkey and mule grown models are in with the logged render scales (donkey 0.87, mule 0.92; hitboxes 1.3965x1.5 and 1.3965x1.6 from `log dims`, `scripts/scale.txt`), and the server now spawns both (McMob, sounds, animals test). Vex charge (`scripts/vex2.txt`: sword in hand, hit by the player so it targets them) logged over 22 ticks: sword arm 3.6652 forward, 0.2618 in, tipped 0.4205 swaying half the idle amount, body upright (0 vs 0.1571), other arm and wings unchanged; the server sets pose 1 while a vex has prey, the sword itself is not drawn. Not done: chicken/bee/rabbit animations are only checked at rest in shots (no airborne shot possible with frozen mobs), baby wolf sitting pose.
   - **Zombie spawn weapons (2026-11-01, `scripts/zspawn10.txt`):** 2000 zombies per difficulty by plain `/summon` (summons with NBT skip the equipment roll), at night with the clock frozen and dropped items cleared between batches (without that, zombies picked up the killed ones' rotten flesh and gear and every count came out inflated). Armed: Easy 19 (0.95%), Normal 20 (1.0%), Hard 108 (5.4%); all armed held an iron sword, shovel or spear: Easy 3/12/4, Normal 6/11/3, Hard 20/71/17 (pooled 29 swords, 94 shovels, 24 spears of 147). Server: 1% below Hard, 5.4% on Hard, mix 29:94:24. Confidence: rates to about a quarter of their size (Easy and Normal about a third; they read the same so share one rate), mix to a few points. Not logged: zombie villagers, husks and drowned (they use the zombie's rate here), baby zombies. Earlier ratios in 443ed82 (10% Hard, a third swords) came from counts that were inflated by that pickup; replaced.
   - **Drowned (2026-11-03, `scripts/drowned1.txt`, `drowned2.txt`, `held2.txt`):** a `minecraft:drowned` mob type (summon/commands only, no natural spawn), with the zombie's body in its own picture (`zombie/drowned.png`; the outer layer picture it wears over it is not drawn) and a held item. Stats from `attribute get` on a spawned one: 20 health, 0.23 speed, 3 attack, 35 follow range, armor 2, no knockback resistance, step height 1, water movement efficiency 0 (so it does not swim better than a zombie yet), no reinforcements; it burns in daylight (Fire 154 ticks against the zombie's 119 after 100 ticks). Hitbox taken as the zombie's, not measured. Spawn gear from 1000 per difficulty on the reference (night): trident 69 / 57 / 58 (Easy / Normal / Hard), fishing rod 36 / 23 / 42, nautilus shell in the other hand 24 / 37 / 34; no difference by difficulty beyond noise, so the code uses 6.1% trident (good to ~0.45 points), 3.4% fishing rod (~0.35), shell not modelled. The trident is drawn as its flat inventory picture like the spear (its own 3D hold is not fitted); throwing it and its sounds are not done. Trident throwing could NOT be measured (2026-11-03, `tridentthrow.txt`, `tridentthrow2.txt`, `tridentthrow3.txt`, 400 to 500 ticks each): a drowned holding a trident, with a survival player 12 blocks off in the open at night, in a glass pool, and with the player in water by day, never threw one (no trident entity on the client or server, player health stayed 20); on land it walked to 8 blocks and stopped, in the pool it bobbed against the wall. Cause unknown (line of sight through glass explains only the pool run), so no throw, damage or rate numbers exist and the drowned does not throw. Left for a run that works out what makes it throw. Water spawning (2026-11-03): the biome lists (river, oceans) already carry the drowned; `SpawnHostiles` now finds water over the spot when there is no floor and lets only the drowned spawn there, feet and head just under the surface, and only on such a spot (it used to spawn on river banks). The depth band, light rule and rate in water are NOT measured (they use the other monsters' dark rule). Swimming (2026-11-03, `scripts/drownswim.txt`, `log sents`, one drowned and one zombie in a glass pool with a survival player 20 blocks off): the drowned sank about 2 blocks, then swam straight at the player (level with it) at 0.0485 blocks a tick over 100 ticks (one run, speed good to maybe 10%); the zombie never moved horizontally and only sank, 0.025 a tick. Server: `bSwims` mobs in water go straight at their target at that speed, `bSinks` (zombie) mobs no longer paddle up. Not measured: a drowned with no target in water, or chasing from the shore, depth keeping, the zombie villager/husk in water. The vex charging reference (`vexcharge_a/b`) was shot at night with the chat over it and its logged hand matrix showed rest-pose angles, so that run proved nothing. Retaken 2026-11-03 in daylight (`vexcharge3.txt`, survival player with resistance, chat clear): the logged right arm at charge is xRot 3.6652, yRot 0.2618, zRot -0.39 to -0.49, and its logged hand matrix's rotation part equals Rz(zRot)*Ry(yRot)*Rx(xRot)*0.55 to 3 decimals (checked in python), which is the convention our model uses with yaw and roll negated, and our charge angles (3.6652, yaw -0.2618, roll 0.42 +/- 0.05) are those logged numbers. So the charging sword's direction is verified by matrix, not by a picture (the shots at that moment would not frame the vex).
   - **Held items on mob models (2026-11-01, `scripts/held.txt`):** a mob has `HeldItem` (server, replicated to `AMcEntityActor`, `SetHeldItem` on the model); a model with `HeldLayer` draws that item's own picture (`FindItemTexture`) as a flat picture on the right arm (the illagers' axe placement: hand at the arm's end, sprite 12 wide, blade up and forward). Zombie, skeleton and vex have it; skeleton spawns with a bow, vex with an iron sword, `-McMobRowHeld=<item>` puts one in a shot row. Exact placement (2026-11-01, `log model` now prints `[hand LEFT/RIGHT]` matrices): the picture sits where vanilla's item chain puts it, arm matrix, Rx -90, Ry 180, translate (-1/16, 0.125, -0.625) and the handheld display transform (rot 0,-90,55, translate 0,4,0.5, scale 0.85); the vex hand frame is its arm scaled 0.55 plus a measured shift. Measured, not recalled (2026-11-03): `E:\mc-reference\scripts\hold.txt` shoots an armor stand holding an iron_sword in two arm poses from front/side/top and logs its hand matrix (`run/screenshots/ref_hold_*.png`, `log_hold.log`); the sword's outline (detected from the shots, camera set by eye, fov 75 from the stand's height) was fitted with 24 axis-aligned turns x 6 display-turn orders x an offset over all six views. Best IoU 0.46 (next best turn 0.36, so the turn is pinned), display order Ry after Rz; the sprite centre lands at (-1.93, 10.21, -6.34) pixels in the hand frame, good to about 1 pixel (grid and camera error). The vex scale/shift are logged numbers. Our zombie and vex shots (`hz3.png`) agree with the reference side shots by eye (blade up and out about 25 to 30 degrees from vertical, vex sword near-horizontal), no angle was measured. Skeleton bow aim (2026-11-01, `scripts/skel2.txt`, `log model skeleton` while it drew on a survival player): with a target, both arms straight out at -pi/2 pitch, hands turned 0.1 in, swaying 0.05 opposite each other (phase not pinned by the samples), arms near rest otherwise; the server sets pose 1 while a skeleton has a target (`FModel::bBowAim`). The bow is still the flat picture on the arm (vanilla's own display transform is not fitted), and the iron spear a zombie can spawn with is likewise the flat inventory picture, not vanilla's `iron_spear_in_hand` hold. Not done: the charging vex's sword was only compared by eye (hv3 shot timed out, not retaken), piglins, wither skeletons, zombified piglins, drowned (trident), witches and villagers are not on the server or not wired, and the crossbow/axe on pillagers and vindicators still use their own fixed layer.

## 7. Decisions

1. **Version:** behaviour and data reference is 26.3 (decided 2026-10-24).
2. **Its own game** (decided 2026-10-24): Unreal runs everything. No `server.jar` at runtime, no Minecraft
   protocol, no Mojang or Microsoft accounts or services. Multiplayer is Unreal networking only.
3. **Accuracy:** plays the same, rebuilt from the wiki, the jar's data and observation; not seed-exact.
4. **Assets:** read from your own 26.3 install into the gitignored `cache/`. Private use only; this can never
   be shared or released with those assets.
5. **The vanilla server host** (`claude/server-host`, built 2026-10-24) stays on its branch as a reference
   tool for comparisons and is not merged into `main`. The packet generator and protocol work from the old
   plan are dropped.
6. **Repo:** this folder, kept private and apart from `enderbio`. FormaTerra's `realms-engine` (Rust) is a
   reference for the patterns: its threaded mesher, lighting and net thread.

## 8. References

- Unreal Engine 5.8: https://wnhub.io/news/engines/item-51157
- RealtimeMeshComponent (Core is MIT): https://github.com/TriAxis-Games/RealtimeMeshComponent
- Minecraft EULA: https://minecraft.net/en-us/eula
- Minecraft Wiki (behaviour, data formats): https://minecraft.wiki
- Density functions and noise settings: https://minecraft.wiki/w/Density_function
- Data generators: https://wiki.vg/Data_Generators
- Running the data generator: https://minecraft.gamepedia.com/Tutorials/Running_the_Data_Generator
- Entity Model Features: https://modrinth.com/mod/entity-model-features

### Piglin gold rule, bartering, nether spawning (2026-11-03)
- Gold rule: measured once (piglin1.txt): a bare player lost 40 hp in 200 ticks to three piglins, one in a golden helmet lost 0. `FMcEntityManager::PlayerWearsGold` (set in `AMcGameMode` from the worn armour, any `minecraft:golden_*`) makes `minecraft:piglin` skip such a player as a target. Brutes were not measured, so they still hunt everyone. Not done: a piglin already angry (hit) should still attack; one sample only.
- Overworld zombification (~300 ticks, seen once) and bartering (no item entity seen) are NOT implemented; the barter run needs logging from the drop tick with the piglin kept from zombifying.
- Nether spawning: spawns there come from biome spawner data; not measured, nothing added.
- Bartering (2026-11-03, barter1.txt, 50 runs): a piglin takes a gold ingot dropped a block away within ~10 ticks and throws one item ~125 ticks after the drop; 41 of 50 runs left an item (9 none, cause unknown; ours always gives one). Table by run count: crying_obsidian 7, blackstone 5, gravel 5, spectral_arrow 5, nether_brick 4, leather 4, obsidian 3, ender_pearl 3, fire_charge 2, dried_ghast 1, string 1, soul_sand 1. Low confidence (41 samples). `FMcMob::TickBarter`. Piglins do not yet walk to a dropped ingot, nor hold it in hand.
- Nether spawn tally on the reference (nspawn.txt, 2026-11-03; creative player with resistance at the locate-biome spot for each, y 70, `kill` of each type every 300 ticks so these are spawns per interval, 8 intervals = 2400 ticks, first 200 ticks counted apart as "init"). Totals over the 8 intervals; confounded because the tp spot's biome is not the only biome within the 128-block spawn ring, and fortress spawns (blaze, wither skeleton, skeleton) are mixed in:
  - crimson forest: zombified_piglin 268, piglin 192, hoglin 90, skeleton 14, magma_cube 4, enderman 2 (init: strider 58)
  - warped forest: zombified_piglin 209, piglin 174, enderman 102, hoglin 85, wither_skeleton 11, magma_cube 4, blaze 4, ghast 1
  - nether wastes: zombified_piglin 343, piglin 103, hoglin 46, skeleton 31, wither_skeleton 15, magma_cube 13, blaze 13, ghast 4, enderman 1
  - soul sand valley: zombified_piglin 272, skeleton 92, magma_cube 85, piglin 42, blaze 36, wither_skeleton 36, enderman 6, ghast 2
  - basalt deltas (7 intervals): zombified_piglin 270, skeleton 98, magma_cube 71, piglin 52, enderman 6, ghast 3
  The biome files (`natural_mob_spawns`, read by `FMcBiomeRegistry`) already drive our `SpawnHostiles`, so piglin and zombified piglin spawn in the Nether by their weights; ghast, magma cube, strider, hoglin, blaze and enderman have no mob yet (their rolls give nothing). Mixed biomes mean the weights cannot be fitted from these counts alone; the overall rate was ~30 to 70 spawns per 300 ticks around one player. Not done: our own Nether run to compare rates, and the Nether light rule.
- Bartering recheck (barter2.txt, 40 runs, held item read at 30/90/150 ticks): the "9 of 50 gave nothing" of barter1 was the counting, not the game: 39 of 39 readable runs gave an item. The piglin holds the ingot in its OFF hand from ~10 to ~120 ticks, then the thrown item shows briefly in its main hand. New items seen: potion of fire resistance (2), splash fire resistance (1), water bottle (1), quartz (2), more soul sand (2-8), fire charge (7 over both runs). Table in `FMcMob::TickBarter` now holds both runs (~80). Piglins walk to a dropped ingot within 8 blocks (range a guess; only 1 block measured). Not done: drawing the ingot in the off hand.

### Nether mobs, measured so far (2026-11-03; nethermobs1/2.txt, ghast1/2.txt; NoAI summons, attributes at scale 1)
- Attributes (health / speed / attack / follow / armor / kb-resist / step): ghast 10 / 0.7 (flying_speed 0.06) / none / 100 / 0 / 0 / 0.6; magma cube (size 1) 1 / 0.3 / 1 / 16 / 3 / 0 / 0.6; blaze 20 / 0.23 / 6 / 48 / 0 / 0 / 0.6; hoglin 40 / 0.3 / 6 / 16 / 0 / 0.6 (attack knockback 1.0) / 0.6; strider 20 / 0.175 / none / 16 / 0 / 0 / 0.6; enderman (already in) 40 / 0.3 / 7 / 64 / step 1.0.
- Boxes (w x h, eye): ghast 4 x 4 (2.6), magma cube size 1 0.52 x 0.52 (0.325), blaze 0.6 x 1.8 (1.53), hoglin 1.3965 x 1.4 (1.19), strider 0.9 x 1.7 (1.445).
- Ghast model (one frame, ghast1.txt): root scale 4.5; body Box(-8,-8,-8,16,16,16) at (0,17.6,0); nine tentacles (2x2 wide, origin (x, 24.6, z)): (-3.75,-5) len 8, (1.25,-5) 13, (6.25,-5) 9, (-6.25,0) 11, (-1.25,0) 11, (3.75,0) 10, (-3.75,5) 12, (1.25,5) 9, (6.25,5) 12; their xRot swings between 0.2 and 0.6 (about 0.4 +- 0.2), phase not fitted. A ghast with nothing to do wandered at 0.1 to 0.3 blocks a tick, rising and falling freely (500 ticks logged).
- Fireballs (2026-11-03, nether1/nether2.txt, in the Nether dimension on netherrack, survival player; the flat overworld never produced one): ghast fireball and blaze small fireball accelerate by 0.1 along the aim each tick with 0.95 inertia (v = (v + aim*0.1)*0.95); ghast ExplosionPower 1; the blaze small fireball hits for 5 hp and sets the target alight (about 1 hp a second); a blaze fires 3 shots 6 ticks apart then waits, 178 ticks first-to-first (n=2 bursts); ghast gaps 59 and 83 ticks (n=2, low confidence; ours draws 60 to 85). Implemented in `FMcFireball` and `FMcMob::Tick`. GUESSES: hover ranges (ghast 20, blaze 16), fireball hit boxes (1.0 ghast, 0.3125 small); no fire on the target yet.
- Built 2026-11-03 from the logged models: hoglin, magma cube (8 slices plus core), blaze (12 rods), strider (6 bristles), ghast (scale 4.5, 9 tentacles) in `McEntityModel.cpp`; mobs in `McMob.cpp` with the attributes above, so the Nether spawn rolls now give them. Models are NOT yet checked visually (the McMobRow shot showed none of the strider/blaze/ghast row; cause unfound), and commit 29d3376 was made unbuilt after the 19:01 reboot (McEntity tests passed before the last model edits).
- Strider and hoglin runs (2026-11-03, nether3.txt, hogbehave1.txt; one sample each, low confidence): a strider summoned on 2-deep lava stood on the surface (y 69.5, one block above the lava floor, OnGround) and wandered 4 blocks in 300 ticks, 20 hp, no damage; on netherrack it wandered 9.6 blocks in the same time. `/kill` on a size-4 magma cube left no children, so the split cannot be measured that way (needs damage). An overworld hoglin 15 blocks from the player did not approach in 400 ticks (log every 40 ticks gave only its first sample) and a piglin beside it fought it (hoglin 40 to 35 hp, piglin killed within 200 ticks); not fitted.
- End and overworld boxes/attributes (2026-11-03, endmobs1.txt, enddims1.txt, NoAI summons; exact): shulker 30 hp, speed 0.7, no attack attribute, follow 16, box 1 x 1 (eye 0.5); endermite 8 hp, 0.25, attack 2, follow 16, box 0.4 x 0.3 (eye 0.13); ender dragon 200 hp, speed 0.7, follow 16, box 16 x 8 (eye 6.8; model not logged); phantom 0.9 x 0.5, guardian 0.85, elder guardian 1.9975, ravager 1.95 x 2.2, pillager 0.6 x 1.95, witch 0.6 x 1.95, slime 0.52. Endermite and shulker are built (unbuilt code, models from the log); the shulker only stands (bullets, lid, colours not built); the dragon, phantom, guardians are not.
- Enderman (ender1.txt, one sample, noisy): hit for 1 it teleported ~29 blocks at once (ours: +-32, consistent); a player looking at its eyes from 10 blocks did not provoke it in 400 ticks (the script's `tp ... facing` may not turn the client's view, so gaze anger is NOT built); water damage unclear.
- Phantom and guardians (phantom1.txt, NoAI attributes exact; models one frame): phantom 20 hp / 0.7 / attack 2 / follow 16; guardian 30 / 0.5 / 6 / 16; elder guardian 80 / 0.3 / 8 / 16. Models built (unbuilt code) from the log. Behaviour NOT measured: the phantom hunt run (survival, midnight, phantom 15 blocks up) logged only two samples, 0.1 block apart, and the world's water flag was set, so no swoop was seen; ours is a plain flier that bites. Guardian beam, spikes, water rules not built (they swim and never attack). Needs a better run: a dry flat world, the phantom summoned without NoAI in the dark.
- BUILT AND CHECKED 2026-11-03 (after the earlier "unbuilt" commits, which had invalid `5f` literals; `remote.sh test` runs with -SkipBuild, so build with Build.bat first): the full suite passes 120/120, with model size tests for hoglin, magma cube, blaze, strider, ghast, endermite, shulker, phantom, guardian (ghast tentacles hang 3.6 blocks under its hitbox, strider and hoglin stand taller than their boxes, as measured). Pair shots against `pairshots1.txt` (ref_pair_a/b/c.png vs Saved/Remote/pair_a/b2/c.png): strider, blaze, hoglin, magma cube, ghast, endermite and shulker match in size and place; the phantom was lowered 21 units (1.3125 blocks) to lie in its hitbox like the reference's; the shulker lid is 0.02 bigger so its notched edge does not z-fight the base. Differences left: guardian spikes look longer than the reference's resting ones, the strider is drawn with the warm skin (the reference's out of lava is the cold one), the phantom's fire and wing flap are not drawn.
- NOT built: strider lava walking and saddle, baby hoglin and strider, magma cube split/armour/speed rise, hoglin knockback and zombification, hoglin hunting (hogbehave1.txt), the Nether light rule.
- Barter pickup range (barter3.txt, 3 runs per distance, ingot dropped 1.5/3/4/6/8/10/12/16 blocks from an idle piglin, checked at 60 ticks): taken at every distance up to 8, never at 10, 12 or 16 (9 untested). It needs the mobGriefing gamerule on: runs after my ghast scripts (which left it off) saw no pickup at all. Our piglins do not check that rule yet.
