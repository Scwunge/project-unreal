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
    - Nether and End mood (done, 2026-10-31): a shadowless fill light (the Nether's `ambient_light_color` warmed
      towards lava, 4 lux; the End's `sky_light_color`, 1.2 lux), a warm or cool colour grade with more bloom and
      vignette. Tuning args: `-McNetherFill=`, `-McEndFill=`, `-McFogGlow=`. The faint box lines in the Nether
      were the volumetric fog's own emissive (visible froxel edges), so the glow defaults to 0.
    - Physical sky (done, 2026-10-31): the Overworld by day uses SkyAtmosphere (lit by the one directional light),
      volumetric clouds (the engine's simple cloud material, 0.6 to 2.0 km up) and a reflection-only SkyLight (diffuse
      off, so caves stay dark). The vanilla dome and sun/moon squares hide meanwhile. Not done: the sun disc and the
      dark strip at the world edge on the horizon; stars and moon phase on the atmosphere sky.
    - Water (done, 2026-10-31): the lit and HD translucent materials tint by depth (scene depth minus pixel depth)
      and show a drifting foam line where the water is shallow.
    - Storm wind (done, 2026-10-31): the leaf, plant and water sway scales with the wetness (rain level eased in
      over 5 s): about 2.6 times the amplitude and 1.6 times the speed at full rain. Shot: `storm_unreal` (thunder).
    - Quality presets (done, 2026-10-31): Options has Quality Low, Medium, High, Ultra (saved in options.json; `-McQuality=N`
      to test) and an FPS counter (`stat fps`; `-McFps`). Presets set render scale (70, 85, 100, 100 percent with TSR),
      Lumen probe density and reflections, virtual shadow detail, volumetric fog, bloom, DoF and motion-blur quality, only
      while Unreal Mode is on. Shots: `q_low`, `q_ultra`. Measured on the 1070 (720p, the unreal scene, one frame
      before the shot, logged by `-McShot`): Medium 100 fps, High 95 fps. The scene is tiny; measure a forest later.
    - Torch flicker (done, 2026-10-31): block light in the lit and HD materials breathes by about 7 percent, out of step
      from place to place. Shot: `torch_night`. The dark strip at the horizon is fixed (2026-10-31): past the loaded terrain
      the atmosphere's own ground showed a dark band, so the physical sky draws the dome's lower half (to the horizon) in
      the fog colour. Shots `skirt_noon`, `skirt_sunset`. At dusk the band is a paler grey than the glowing horizon behind
      it; blend it with the glow. `-McHide=fog,clouds,stars,moon,post` leaves parts out for finding artifacts.
    - Cave exposure (done, 2026-10-31): the world renderer checks the 24 blocks above the camera (leaves count as open);
      under a roof in the Overworld the exposure rises up to 1.5 stops over a second or two and falls back outdoors.
      Scene `tools/scenes/cave.json`, shot `cave_unreal`. Depth of field is left off on purpose (it softens voxel
      edges); `r.DepthOfFieldQuality` is still set at Ultra if it is ever enabled in the post settings.
    - Vanilla-look check (2026-10-31): a baseline built from dfe04f8 (before the Unreal Mode work) against this branch, Unreal
      Mode off, on fresh worlds: the cave scene differs by 9 pixels (the torch flame), the village scene only where mobs
      stand; terrain, sky and HUD are identical. Daylight Unreal shot `day_noon` shows no exposure regression.
    - Showcase scenes (`tools/scenes/forest.json`, `shore.json`, `village.json`; shots `show_forest` at sunset, `show_village`
      at night): the forest and village read well. The shore (`show_shore2`) does not yet: the sea is a raised block basin,
      so it reads as a dark wall; use a generated ocean for that shot. Shots need a fresh `-McWorld=` name per scene: a
      world keeps the blocks of scenes placed in it before. `-McCam` did not change the view of `-McScene=unreal` (the scene's
      camera wins), so the sun disc facing the sun is still unchecked.
    - Sun and sunset (checked 2026-10-31): facing the sun (`sun_c`, `-McCam=0.5,-59.4,0.5,-90,-8` at `-McTime=500`) the
      atmosphere draws a sun disc with bloom, lens-like glare spots and its glint on the grass; the sunset (`dusk_w`,
      `-McTime=12400`, yaw 90) is orange to dark with the skirt a thin line at the horizon. Unreal Mode hides the vanilla
      cloud slabs (the volumetric clouds replace them; vanilla keeps them). Shore: `show_shore3` (sand, a flat sea level
      with the land, ripples, sunset sky). Shots go on fresh `-McWorld=` names, and the camera for a scene can be
      overridden with `-McCam=` (the sun shots show it works without a scene).
    - Lightning (done, 2026-10-31): a flash lights the scene with a pale blue light from high up for 0.2 s, with shadows
      (`flash_on` with `-McFlashHold -McStrike=20,-10` against `flash_off`, a night thunderstorm). The held shot used 1.5
      times the sun's lux; the shipped value is 1.0 (not re-shot). Seen but not fixed: at night the rain streaks stay
      bright blue (they are unlit particles), so lit rain still needs their material to take the sky factor.
    - Still open: the ray tracing path on a DX12 RTX machine (untested; the 1070 is DX11), forest-scale fps at Medium on
      the 1070, a generated (not block-built) ocean, and the roadmap items not started: lit rain and snow, lightning that
      lights the scene, wind interaction with mobs, Lumen beyond the screen (distance fields), caustics and underwater fog.
    - `-McShot` now waits for the shaders still compiling (up to 3 minutes) before saving: a changed translucent
      material is skipped until compiled, which made new water look missing in shots.
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
  - **Vehicles** (2026-10-29): `McRails` joins the four rails as they are placed (lines, curves for the plain
    rail, slopes) and `McVehicle` has the minecart and boat entities. A cart follows its rail, slows when empty,
    is pulled down slopes, speeds up on a powered rail and stops on an unpowered one; detector rails power while
    one sits on them and an activator rail throws the rider off. A boat floats, glides with friction by the
    surface and turns and thrusts with the rider's keys. Right-click gets in (shift gets out); a hit breaks a
    cart, a boat takes a few; both drop their item (nothing in creative), are saved with the chunk, and are
    placed from the item on a rail or water. `/summon minecraft:minecart|boat` works. Not done: chests, hoppers,
    furnaces and TNT on carts, boat paddles, more than one rider, carts pushing mobs.
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
- **Placed banners** (2026-10-29): a banner block carries a flag entity (`mc:banner`, `FMcBannerEntity`) holding its base,
  wall/standing and layers, drawn as pole, bar and one tinted layer per pattern; placing keeps the item's patterns, breaking
  drops them back. `/give` takes `item[component=json]`.
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
- **Pause menu and options** (2026-10-30), in `McHUDMenus`: Back to Game, Options, Open to LAN and Save and Quit; Esc
  closes a screen first, then opens the pause menu. Options has sliders for field of view, render distance,
  sensitivity and volume (kept in `Saved/options.json`) and the Unreal Mode / HD choice. Open to LAN starts a listen
  server on the running world. **Not tested:** another client joining.
- **Water draws before ice and glass** (2026-10-30): water is its own render pass (`EMcRenderPass::Water`, same
  translucent material) in a mesh of its own per chunk with translucent sort priority -1, so over a frozen lake every
  chunk's ice is blended over the water below it instead of the order following the chunk meshes' distances.
  Still per-mesh: translucent blocks against each other (glass behind glass) sort by chunk distance only.
- **F-keys** (2026-10-30): F1 hides the HUD and hand, F2 saves `Saved/Screenshots/<date>.png`, F3 shows the debug screen (version, fps, XYZ, block, chunk, facing, light, biome, targeted block), F5 cycles first person, behind (camera clipped at 4 blocks by a raycast) and in front. `Config/DefaultInput.ini` clears the engine's own F1-F5 view-mode bindings. **Not done:** F3 combos, F4, the full F3 pie chart/graphs. The FOV is vertical now (`r.UseLegacyMaintainYFOVViewMatrix=1`; Unreal otherwise took 70 as the horizontal angle at 16:9, about 44 degrees vertical, which zoomed the whole game in): the F5 player is about 245 px tall at 720p.
- **Eating and sound audit** (2026-10-30): the held food rises to the face, turns toward the camera and bobs while eaten (`McHand::GetEatTransform`, driven from the use button; no drink items exist yet, so drinking is the same pose once they do). Sounds the game now plays by name: `entity.generic.eat` every 4 ticks and `entity.player.burp` on finishing, `entity.item.pickup`, `entity.experience_orb.pickup`, `entity.player.levelup`, `entity.player.attack.strong/weak/crit`, chest, barrel and shulker box `open`/`close`, `ui.button.click` on every menu button. **Audit** (sounds.json has 1991 events): block and mob sounds are built from the group and mob name (451 events); only about 40 others are played by name. Still silent: splash and swim, fire, lava and water ambience, furnace crackle, note blocks and jukebox, portals, tnt primed, throw and bow shots, item and tool breaking, hurt variants (drown, burn), ender chest, anvil land, enchant, sleeping and the UI sounds other than the click (toasts, bubble pop, stonecutter, loom, cartography).
- **Glass behind glass** (2026-10-30): translucent quads (stained glass, glass, ice, slime) are sorted back to front per section when meshed (`McTranslucent::SortQuadsBackToFront`), and the component re-sorts one chunk per frame once the camera has moved 1.5 blocks, so a pane in front of another blends over it from every side. Test `McTranslucentSortTest`; shots `glass1`, `glass2` (red, blue, lime, clear in a row, seen head-on and from the side).
- **Eating crumbs** (2026-10-30): while food is eaten the local player throws bits of the food's own sprite (random opaque pixels of `textures/item/<name>.png`) from just below the mouth, every 4th tick from the 7th with the munch sound (`UMcParticleComponent::ItemEaten`, a one-colour `Crumb` particle that falls and lands). Shot `crumbs3`.
- **Sound pass 2, part 1** (2026-10-30): the player splashes going into water (`entity.player.splash`, `.high_speed` over a quarter; volume from the speed, at least 0.12), swims (`entity.player.swim` every 1.6 blocks), puts a fire out (`entity.generic.extinguish_fire`), and says `hurt_drown` or `hurt_on_fire` by the last damage kind (`FMcPlayerStats::LastDamageKind`); a tool or weapon that breaks cracks (`entity.item.break`); buying an enchantment plays `block.enchantment_table.use` (`UMcPlayerDataComponent::ServerSound`); the nether portal hums (`block.portal.trigger`) and travel plays `block.portal.travel`. Ambient blocks near the player say things at the game's odds, from the same random pick as the particles: fire, lava pop and ambient, flowing water, nether portal, furnaces and campfires crackling. `-McLogSounds` logs every sound event for runs with the sound off. Not yet: bow, throws, TNT primed, note blocks and jukebox have no gameplay to attach to; toasts wait for advancements.
- **Translucent opacity** (2026-10-30): vanilla blends in sRGB and Unreal in linear, so every translucent material (stained glass, ice, water, slime, honey) uses opacity `pow(alpha, 0.63)` (`McSrgbOpacity` in `tools/make_materials.py`); five measured points are within about 10 of vanilla.
- **Bow** (2026-10-30): hold right mouse with a bow and an arrow (or in creative) to draw; power is `(t*t+2t)/3` for t seconds, capped at 1, no shot under 0.1 (`McCombat::GetBowPower`); the arrow flies at power*3 blocks a tick, hits mobs for `ceil(speed*2)` (a full draw adds up to half again), sticks in blocks and is picked up again unless shot in creative. The bow shows `bow_pulling_0/1/2` at 0/0.65/0.9 s and the view zooms `1-0.15*min(1,t)^2`; sounds `entity.arrow.shoot`, `.hit` and the pickup.
- **Bow pose** (2026-10-30): drawing stands the bow side-on, arrow forward, limbs up, string toward you (`McHand::GetBowStandRotation`), moved (-2, -20, 20), scaled 1.2, yawed 22 and rolled 7 degrees in the camera's space from where it rests (`GetDrawTransform`), (idle scale to 1.2 times it). Fitted by overlaying our shot on the reference's at 5 and 12 ticks of draw (limbs, string column and size within roughly 10 percent; the string is not yet an exact vertical column). Held flat items are a true one-pixel extrusion of the picture (`BuildExtrudedMesh`), no doubled outline. The near plane is 1 unit (the drawn string comes within 2 of the eye). Polish left (Jeff, aeb69aa): the lens between limb and string is narrower than vanilla's, the arrow sits more level than vanilla's slight downward slant, and the upper limb leans less. Not yet measured: whether the bow trembles at full draw (none now).
- **Held item placement** (2026-10-30): all held things now share one placement, measured from the reference client's idle shots (E:\mc-reference scripts/idle_items.txt): anchor (0.72, 0.567, -0.525) blocks forward/right/up of the eye (solved from the corners of the held stone's top face), the display transform's own scales (0.4 block, 0.68 flat; the old 0.5 and 0.37 fudges and the anchor were compensating for each other), and flat items' rotation signs turned over (blade up and to the right). Sword, stick, snowball, bow, stone overlay the reference within a few pixels; the empty hand was not touched.
- **Held rod** (2026-10-30): a held item's firstperson_righthand display is read from its model in the cache, parents followed (a fishing rod has its own, rotation 0,90,25 translation 0,1.6,0.8), instead of one fixed for all flat items. `UMcHandComponent::SetRodCast(true)` shows the cast picture (fishing_rod_cast); `GetRodTipWorld()` gives where the line leaves (the picture's top-right opaque pixel, in world space, following swing and bob) for the fishing branch to attach its line to. Checked against the reference client's idle and cast shots (taken with `click`, 20 ticks after the cast): the held rod stands at the same angle in both, top at about (230,320) and bottom at about (350,640) of 1280x720, and ours matches that within a few pixels (our rod is lit brighter: the reference shot was taken in a darker light). The line runs from the rod tip to the bobber and is drawn by the fishing branch. `-McRodCast` forces the cast picture for shots.
- **Thrown items** (2026-10-30): right click throws a snowball, egg or ender pearl along the look (`ServerThrow`, `FMcThrownEntity`): 1.5 blocks a tick, gravity 0.03, drag 0.99 (0.8 in water). A snowball flinches mobs without damage (a blaze takes 3); an egg makes a chick one time in eight (four in one of thirty-two of those; a chick is a half-size chicken, grown after 20 minutes, not kept in saves as a chick); a pearl takes its thrower to where it lands for 5 fall damage, one throw a second. Sounds from the cache (`entity.*.throw`, `entity.player.teleport`). No hit particles yet (nothing carries particles from server to client), and no endermite chance (no model).
- **Death screen** (2026-10-30): "You died!", "Score: N" (experience points at death, yellow, replicated as `DeathScore`), Respawn and Title Screen buttons (Title Screen leaves the map, saving). **Not done:** hardcore's Spectate World, the cause-of-death line.
- **Beds and block outline** (2026-10-30): the aimed block's outline is a thin black 40% line, the same width on screen at any distance; crack stages darken the block (`McBlockHighlight`). `McBed`: right-click sets the respawn point ("Respawn point set"), sleeps at night 12542 to 23459 or in a thunderstorm (else "You can only sleep at night", or "You may not rest now, there are monsters nearby"); every player in bed for 100 ticks skips to morning and clears the weather; sneak, jump or damage gets up; death respawns beside the bed ("Your home bed was missing or obstructed" if gone); in the Nether and End the bed explodes. Chat lines go to the log (`Chat: ...`). **Not done:** other players see a sleeper standing, no occupied bed state, sleeping players can't be told apart from awake ones in a bed already taken, respawn anchors.
- **First-person hand** (2026-10-30), `McHand`: the right arm (empty hand only) and the held item, drawn with UE
  first-person rendering (camera `FirstPersonScale` 0.1, its own 70 degree vertical field of view). A held item is
  placed from the models' `display.firstperson_righthand` data at one anchor (`HeldAnchor`); swing, walk bob and the
  drop when the item changes are in. Held block 150-250 px wide is a test. **Not done:** maps in hand, off-hand,
  enchantment glint, tools that differ from item/handheld (rods, mace).
- **Shulker box contents** (2026-10-29), in `McShulker` and `McBlockUse`: a shulker box is a 27-slot "Shulker Box"
  container. Breaking one drops it with its items packed into the item (`PackBoxDrops`), and placing it fills
  the box again (`FillBox`); a dispenser placing one keeps the contents. A box cannot open with a block against
  its lid, and shulker boxes cannot go inside one (`bNoShulkerBoxes`).
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
- **Held item lowering and the use swing** (2026-10-30; measured with E:\mc-reference\scripts\equip2.txt, shots at ticks 0 to 8 after each event, and snowball.txt): the held item is lowered and raised again (about 0.12 s each way, two to three ticks) when it is changed for a different item and when the stack of the same item grows (a pickup); it is NOT when the slot is switched to an identical stack, nor when the count drops (placing a block, throwing). The 'item vanishes for ticks 1 to 3 after a throw' of snowball.txt was the use swing itself (the script's `use` pressed the key, which swings), which carries the item toward the middle and down out of the view and back by tick 6: now `McHand::UseSwingMove` (-46 right, -40 up, in units) times the swing stroke for a right-click swing with an item. Only that it is out of view at ticks 1 to 3 and back by 6 is measured, so the two numbers are chosen to put it off screen; the sword's attack swing is untouched. Finishing a meal looked like a lowering too (the apple is away for four ticks), not yet done; a breaking tool did not show (the shot kept the tool).
- **Vignette** (2026-10-30): the cache's own picture, textures/misc/vignette.png (grey 0 in the middle, 0.39 at an edge's middle, 0.82 in a corner), is drawn stretched over the screen as black with its grey times a strength as the opacity, under the HUD (gone with F1): `McGui::GetVignetteStrength` is 0.289 + 0.067 x the sky light factor (noon 0.356, night 0.289). Fitted to the reference client's ref_vig_* HUD-on/HUD-off pairs: noon strength 0.356 with 1.1 percent residual against the texture's shape, night 0.289, lit room at level 8 about 0.33 (noisy). Open: the light at the player's own position (a torch-lit room by night) is not read, only the sky's factor; the dim (level 4) pair was too dark to fit (0.58 +- noise), the level 15 pair was misaligned.

### Rain at night, lightning at 1.0x, distance-field Lumen (2026-10-31)
- Rain/snow sheets now take their vertex tone from the sky light (sqrt of `McSky::GetSkyLightFactor`) in Unreal Mode, so night rain reads dim like vanilla instead of bright blue. Shots: rain_night, rain_day, thunder_night (lightning at 1.0x, -McFlashHold).
- Distance-field Lumen on the DX11 1070: not achievable as a cvar switch. Chunk meshes are `UProceduralMeshComponent`, which has no mesh distance fields and no Lumen cards, so software Lumen sees them only through screen traces. Real fix = build chunks as static meshes (or a custom scene proxy with SDF), a large change touching the mesher; left open.

### Underwater look, vanilla check again (2026-10-31)
- Thunder at night: `thunder_noflash` (-McTime=15000, no -McFlashHold) is dark, so the time flag works in storms; the bright `thunder_night` was the held flash lighting the sky.
- Underwater (Unreal Mode only): `McSky::GetCameraUnderwater` (the world renderer sets it when the eye is in a water block, eased in 8/s) closes the fog to about 16 blocks in the sea's blue-green, dims the sun, and the grade goes blue-green and a stop darker. Scene `tools/scenes/underwater.json` (a raised sea; the world floor is y=-64, so a deep pool can't be dug). Shot `uw_day`.
- Caustics (updated): now in the lit block materials (scalar `Underwater`, set each tick by the world renderer): under water, sky-lit surfaces are modulated by a moving net of light, `e *= lerp(1, 0.45+2.2c^2...)`. Visible on the prismarine in `uw_day`; the sand is so bright it clips and shows little. Open: lower the exposure underwater or darken the sand so the net reads on it. The light-function version below stays but is weak.
- Caustics, first try: `M_McCaustics` (a light function made by `make_materials.py`, `MC_ONLY_CAUSTICS=1`) is set on the sun while under water. Compiled and applied, but the pattern is not visible in `uw_day`: the sun is dim there and the sand is washed. Open: brighten/scale the pattern or put it in the lit block material.
- Vanilla check against dfe04f8, Unreal Mode off: cave differs by 152 px (torch flame, x 633-645); village by mobs only (band y 259-363); terrain, sky and HUD identical.

- Unreal-off pixel check redone (2026-11-01, baseline dfe04f8 rebuilt, 20 s shot delay): cave 136 px (torch flame only), village 1543 px (mob band only); terrain, sky and HUD identical. Underwater exposure is now -1.8 EV (`uw_day`: sand no longer clips; the caustic net reads on the prismarine, faintly on the sand).
- Water refraction (2026-11-01): the lit translucent materials give water an index of refraction of 1.2 (other see-through blocks 1.0), so the ripples' normals bend what lies under the surface. Shot `refr_a`/`refr_b` (shore, day): the water renders correctly and the ripples read, but at a shallow angle the bend is subtle, and the sky is barely mirrored in the water: reflections come from Lumen's screen trace plus the reflection-only skylight, which seems too weak on a translucent surface. Open: a stronger sky reflection for water (a fresnel-weighted sky colour in the emissive), then volumetric fog shafts.
- Water sky reflection and refraction A/B (2026-11-01, scene `shore_down`, -McTime=5000, dev args `-McWaterIor=` and `-McWaterSky=`): the water's emissive adds a fresnel-weighted sky colour (horizon to zenith by the reflected direction, scaled by the sky light), default strength 1. Shots `w_ior10`/`w_ior12` (IOR 1.0 vs 1.2, sky off): the ripple pattern differs, but the shots run on different game clocks so the wave phase differs too; not a clean proof that refraction bends the floor. `w_sky1` is hardly different from sky off at this steep angle (fresnel is low); `w_sky3` washes the water to a white sheet, so keep the default. The water is milky over a one-block-deep sand floor even with the sky off: the specular and the sand through it; a deeper scene would show the blue. Open: pin the wave clock for A/B shots, test with a deep sea at a grazing angle.
- Water reflection and refraction at a grazing angle (2026-11-01, scene `lake_low`: camera just above a 2-deep lake, `lk_*`, `lk2_*`, `lk3_*` shots). Sky reflection: at strength 1-2 it milked the water pale near the horizon, so the sky colour is bluer now and the default strength 0.5 (`lk2_dflt`: a pale sheen toward the horizon over the blue, bricks still mirrored by Lumen). Refraction: with the index only wired to the Refraction pin nothing moved (IOR 1.0 vs 1.8, `lk2_i10`/`lk2_i18`, identical); the material also needs `refraction_method = RM_INDEX_OF_REFRACTION`, set in `make_materials.py`. With it, IOR 1.8 visibly changes the submerged bricks against 1.0 (`lk3_i18` vs `lk3_i10`). Default is now 1.33 (water's own); not shot at 1.33 itself. Wave phase still differs between runs, so ripples differ.
- Light shafts (2026-11-01): the volumetric fog is thicker (extinction 0.04 + 0.03 x Shafts), leans forward (scattering distribution 0.2 to 0.8) and the sun scatters 1 + 2 x Shafts times as strongly; default `Shafts` 1.8, `-McShafts=` tunes it (0 is the old haze). Scene `forest_sun` (inside the forest facing east at `-McTime=500`). Shots `sh_t0` (0), `sh_t1` (1: hardly visible), `sh_t3` (3: clear rays, milky), `sh_tdef` (1.8, chosen): soft warm shafts through the leaves and trunk gaps. Open-ground view into the low sun (`sh_s1`) just goes milky at 1: shafts need trees in the way. Not yet shot: shafts into caves; Unreal-off pixel check not re-run (Unreal-only paths, but the vanilla check is due on the next run).

### Light shafts into caves + Unreal-off check (2026-11-01)
- Cave scene, -McShafts=0 vs default: identical look (enclosed room, no sun path, not milky). Shafts only show with an open sky path (forest_sun).
- Vanilla (Unreal off) pixel check vs dfe04f8 after the shafts change: cave 98 px (torch flame, x 629-646 y 150-175); village 3463 px confined to rows 229-343 (wandering mobs); terrain, sky, HUD identical.

### Block lights budget (2026-11-01)
- `UMcBlockLightComponent` existed (nearest light-giving blocks get point lights); the budget now follows the quality preset: 8 / 16 / 24 / 32 lights and 0 / 3 / 6 / 8 shadowed at Low to Ultra. `-McBlockLights=N` sets it for tests (0 = no lamps). Unreal off: the component hides all lights, unchanged.
- Shots: `bl_off` / `bl_on` (village, night): the lamps add a warm pool on walls and path. `ls_off` / `ls_8` / `ls_24` (`tools/scenes/lamp_street.json`, 40 lights, Quality 2).
- 1070 fps over the last 3 s before the shot (new `-McShot` log line; read from `C:/mc-runs/<name>/Saved/Logs/McClient.log`): lamps off 75.6, 8 lights 80.7, 24 lights 84.2: within noise (other sessions share the machine), so the budget costs no visible frames at this scale.

### Volumetric clouds from the vanilla layout: PARKED (2026-11-01)
- Built: `M_McVolumeClouds` (Volume domain, additive; `tools/make_materials.py`, `MC_ONLY_CLOUDS=1`) reads the pattern from a runtime texture of `clouds.png` (`UMcCloudComponent::Mask`, one texel per 12 block cell), carried west by `DriftX`, puffy profile by world Z, `Storm` greys and thickens; the sky component feeds it (`McSky::SetCloudLayer`) and places the cloud layer at the vanilla height (`-McCloudDensity=` scales it).
- It is off by default (`-McVolumeClouds` turns it on): on the 1070 the whole frame goes black with it, even with density 0, with the engine's own layer height, and even with a constants-only graph (zero extinction, white albedo, no custom nodes), so the graph is not the cause. The screenshot frame took about 3 s each time (0.3 fps), so it may be the volume material compiling in the frame; or a missing piece of the pipeline that the engine's `m_SimpleVolumetricCloud` has (its pins go through ShadingPathSwitch nodes and a VolumetricAdvancedMaterialInput). Python has no CloudLayer node. Next try: duplicate the engine's material and swap in the mask for its cloud-type texture, rather than building from scratch; or one run that cycles variants per frame.
- Default stays the engine's simple clouds (`cl_simple`, shot at level view).

### Rain puddles (2026-11-01)
- Lit and HD materials: rain (Wetness) now fills puddles on flat, sky-open faces from a world-space blotchy field (a few metres across; they grow as the rain goes on): darker, near-mirror (roughness 0.02, specular 0.9), a fresnel-weighted grey sky sheen in the emissive, and rain rings (a drop every 1.1 s per 40 cm cell) tilting the normal. The old uniform wet look (darker, glossier) stays everywhere else. `-McPuddles=0` turns them off for A/B (`pd_off`), default 1.
- Shots (`-McScene=village -McWeather=rain -McTime=6000`, fresh world): `pe_on` (low angle: puddles on grass and the path), `pd_on` / `pd_off` (top-down: only a darker patch without the sheen, which is why the sky sheen was added). Unreal off is untouched (vanilla materials, same as before).

- **Cloud face shading** (2026-11-03, reference shots `ref_cs_*`, opaque faces read from `ref_cs_y0` at yaw 0: top (238,244,255), north/south face (199,206,216) = 0.84 of the top, east/west face (219,225,235) = 0.92; `ref_cs_bottom*` underside red 166 = 0.70): `UMcCloudComponent::Rebuild` shades are now bottom 0.70 (unchanged), north/south 0.84 (was 0.8), east/west 0.92 (was 0.9). Not yet checked by a paired shot: my one `-McCam` shot (`cl_shade`) did not get above the cloud layer. The reference's underside is slightly bluer than a plain multiply (G 177, B 197 of top 244, 255), not modelled.
- **Nether and End fog in Unreal Mode** (2026-11-03, shots `nn_enh`/`ne_enh` before, `nn_fix2`/`ne_fix2` after, `-McDimension=`): the volumetric fog (made for the Overworld's shafts) darkened the Nether's and the End's distance to black, and the unlit fog colour and End sky picture were dimmer than the vanilla look through the enhanced exposure. Now the volumetric fog is off in those dimensions (`UMcSkyComponent`, switched back by mode), their fog luminance and the End sky's tint are 2.4 times (`EnhancedFogBoost`, enhanced only, so the vanilla look is unchanged), and the End's fill light is half white, half `#ac60cd` (it turned the olive End stone magenta). The Nether now reads like the vanilla fog (dark red haze to the horizon); the End stone is a little grey-pink next to vanilla's olive. Spawn spots differ between runs, so the shots are not the same view. `cloud_top` scene added (a platform at 228 looking down at the clouds) but the haze flattens the face contrasts there, so the cloud shades are still not verified by a shot.
- **Cloud shade shot: PARKED** (2026-11-03): `95b9c0d`'s shades (bottom 0.70, north/south 0.84, east/west 0.92, from the reference opaque faces) have no paired shot of ours. The `cloud_top` scene (`tools/scenes/cloud_top.json`, a platform at y 204 above the 192 layer) gives a near-level camera whatever pitch it names, so only fogged horizon clouds show; `-McInput=look:0,50` with `remote.sh shot` returned no shot (the shot wrapper wants the `-McShot` path, and the input script's own `shot:` writes elsewhere). Next try: `-McInput="look:0,50;wait:1;shot:cs;end"` run by hand on the 1070 and read from `Saved/Screenshots`, then measure top vs north face on an un-fogged cloud.
- **Puddles toned down** (2026-11-03, `rn_a` before, `rn_c` after, scene `forest`, rain, Unreal Mode): the puddles read as big blown-out white patches on the grass; coverage threshold at full wet 0.5 -> 0.62, puddle specular 0.9 -> 0.55, grey-sky sheen 0.7 -> 0.3 (`make_materials.py`, lit and HD twins regenerated). Now they are paler, patchier sheens. The rain streaks are the vanilla texture's blue in both looks (`rn_v`), so left alone. One shot at `-McShotDelay=40` (`rn_b`) caught the world with placeholder checker textures and black blocks, which a 20 s delay did not: the wait for shaders is not catching everything after a material change, worth a look.
- **Caustics visible** (2026-11-03, scene `underwater`, Unreal Mode, noon; before `fc_underwater_a`/`fc_uw_noon`, after `fc_uw_f` with `-McCreative`): the sun's light function was a faint mottling that the underwater grade and fog hid, so the sea floor read as a flat teal sheet. `M_McCaustics` is now three ridged sine layers (lines where each wave crosses zero, width from the power 7) as a net of bright lines over a dim base, 0.5 to 2.4 times the sun; the net drifts with `Time`. It reads clearly now on the sand. Note a survival player in this scene drowns before a shot delayed 9 s or more (the shot also waits for shaders), so use `-McCreative` for underwater shots.
- **Shore foam: tried, reverted** (2026-11-03, `foam_b` vs the earlier `pkg_water`/`w_*` shots on `shore_down`): vanilla beaches are flat steps, so the scene depth behind the water jumps from 0 to a block at the waterline and the depth-fade foam (40 cm) only ever draws on the last pixel of the edge. Widening it to 110 cm with a drifting lace pattern milked the whole one-block-deep shallows pale grey (worse than none). Real shore foam needs to know where the water's own edge is: a per-vertex flag from the chunk mesher (a water top face with a non-water, non-solid-above neighbour on a side, or the neighbour column's height) feeding a distance-to-edge in the material. Not started; it touches the mesher (a shared file), so it waits.
- **Rain and snow daylight** (2026-11-03, Unreal Mode, noon; rain in `forest`: before `rn_c`, after `rn_d`; snow on `-McFlatPreset=minecraft:snowy_kingdom` (new dev arg): before `sn_a`, after `sn_b`, vanilla `sn_v`): in rain the sun kept 20% of its light and the sky light only reflects, so a noon storm was as dark as dusk next to the vanilla look. The sun keeps 50% in rain now (`Clear = 1 - 0.5 * Rain`), the Mie haze still greys the sky. The snow field is a bright overcast white and the flakes are low-contrast on it (as in vanilla, white on white). Next for snow: flakes catching light, a snow-covered-leaves and grass look are not started.
- **Dusk and dawn in Unreal Mode** (2026-11-03, scene `shore`, `-McCreative`; before `ss_12300` (sky black, ground nearly black) and `ss_23100`, after `ss2_12100`, `ss2_23100`, vanilla `ss_v12300`): the sun's light cut out at 4% under the horizon and the moon (0.6 lux) took over, so the sky went black and the ground dark while the vanilla look still has a blue dusk. Now the sun's window reaches 20% under the horizon in Unreal Mode (the atmosphere keeps its glow; the vanilla look's window is unchanged) and a shadowless blue fill (`TwilightFill`, 2.5 lux, from the side away from the sun, peak at about 3% under the horizon, rain-dimmed) lights the ground. The sunset now shows orange clouds, a lit beach and a darker blue-grey sky overhead. Block lights at night (`lt_a`, scene `lamp_street`, 15000) already read well; the cave scene shot did not return, so caves are not re-checked.
- **Vanilla check after the 2026-11-03 Unreal Mode work** (partial): the vanilla `cave` scene now (`vc_new_cave`, `-McCreative`) against the earlier `vn_cave` (at 0d991b4): every pixel above row 510 is identical; the differences (rows 510-719) are the hotbar and hearts, creative against survival. Village, forest, shore, Nether and End were not compared: a baseline build for a proper diff ran the E: drive out of space (0 bytes free, now cleaned to 2.3 GB; see the disk note to Jeff), and `vc_new_*` for those did not all return. Note the cloud shades (`95b9c0d`) change the vanilla clouds on purpose, and the Nether/End fog and End sky tint paths run in the vanilla look with unchanged values.

## Vanilla pixel check (HEAD vs 0d991b4, Unreal off, 1070)
cave: 252 px differ (17x16 box, a particle). end: identical. village/forest/shore: differences only in the cloud/horizon band
(the intentional cloud face shades). nether: mean diff 0.56/255, max 70, lower half only; looks like chunk light-update timing
(block tints differ in the warped patch), not a code path I changed for vanilla. Not re-run; worth one repeat if it matters.

## Snow at night (Unreal Mode, shot snown_night3)
Scene-free snowy flat (-McFlatPreset=minecraft:snowy_kingdom, no -McScene: scenes overwrite the biome so you get rain). Flakes show at night
against a dim blue ground; they are unlit so they read a little bright. Acceptable; a dimmer night flake tint is a possible follow-up.
Done: flake tone is now max(0.1, skylight^0.85) instead of sqrt(skylight) (McWeatherComponent.cpp), so night flakes are dim; shot snown_night4 (before: snown_night3). Day unchanged (factor 1).
Nether vanilla re-check: two HEAD shots of the same Nether scene differ from each other (max 45, 4146 px >8, mean 0.11), so the earlier HEAD-vs-baseline Nether difference (mean 0.56) is
run-to-run noise in the lower half (light updates / world state), not a vanilla regression.
Cloud shade shot retried 2026-11-04 (local lane, `-McInput=look:0,40;wait:3` with `-McScene=cloud_top`): the shot came back level at the horizon, so the look input still isn't applied before the shot (the scene camera pitch is also ignored). Stays parked until the shot path honours pitch.
