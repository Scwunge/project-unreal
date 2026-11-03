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
  - **Vehicles** (2026-10-29): `McRails` joins the four rails as they are placed (lines, curves for the plain
    rail, slopes) and `McVehicle` has the minecart and boat entities. A cart follows its rail, slows when empty,
    is pulled down slopes, speeds up on a powered rail and stops on an unpowered one; detector rails power while
    one sits on them and an activator rail throws the rider off. A boat floats, glides with friction by the
    surface and turns and thrusts with the rider's keys. Right-click gets in (shift gets out); a hit breaks a
    cart, a boat takes a few; both drop their item (nothing in creative), are saved with the chunk, and are
    placed from the item on a rail or water. `/summon minecraft:minecart|boat` works. Not done:
    carts pushing mobs.
    Physics refitted 2026-11-01 from vanilla's server, logged each tick (`log sents`; vehicle_log.txt, powered_log.txt,
    paddle_log.txt), and replayed number for number by the `McClient.McEntity.VehicleLog` test. A cart is braked by
    an unpowered powered rail (half, or stopped under 0.03), moves (at most 0.4 a tick; its speed itself has no cap),
    then keeps 0.96 of its speed plus 0.05 for each block dropped (less for each climbed); a powered rail adds 0.06
    after that, or kicks a cart at rest off a block at 0.02. A boat keeps 0.9 on water, 0.98 on ice, 0.6 on stone
    before it moves, falls with no drag, floats with vy' = 0.75 (vy - 0.04 + 0.04 depth / 0.65 height), and its turn
    keeps 0.9. Second round (vehicle_log2/3/5.txt): a ridden cart keeps 0.997 and moves 0.75 of its speed; a fuelled
    furnace cart nobody pushed coasts at 0.9408; one fed coal moves its full 0.2 a tick from the next tick (vanilla's
    own speed reads up to 3.46 meanwhile, held back by the move cap); a boat dropped into water lands at the surface
    less 0.3504 with its fall speed gone; a boat on land keeps the mean friction of the blocks under its hull (blue
    ice 0.989, ice and packed ice 0.98, stone 0.6; a hull over one row of packed ice between stone 0.72667).
    vehicle_log2-4.txt's air fill was over the 32768 block limit and failed (every boat there kept 0.45); clear air
    in fills under it (Script.java now stops a run on one). vehicle_log6.txt: on slime a boat keeps 0.8 before the
    move and 0.16 of that after; soul sand leaves 0.4 after the move (counted from half a block under the boat, so
    while dropping onto it too); a furnace cart out of fuel is left going at 2, held to its 0.2 a tick cap until 0.9408
    a tick wears it under. Not measured: honey and other blocks, empty cells under a hull.
  - **Boats by wood and chest boats** (2026-10-29): all ten woods (oak, spruce, birch, jungle, acacia, dark
    oak, mangrove, cherry, pale oak and the bamboo raft) place from their items and drop themselves, with a chest
    variant each (`minecraft:cherry_chest_boat`). `FMcVehicle::Create` and `CreateFromItem` are the one way to make
    a vehicle from an entity name or an item; `/summon minecraft:cherry_boat` works too. A vehicle can carry an
    inventory (`GetInventory`, saved with it, thrown out when it breaks): a chest boat has 27 slots and opens as a
    chest when you click it sneaking (an ordinary click gets in). Shot: `Saved/Shots/boats.png`
    (`-McScene=boats`, boats summoned with `-McChat`, see `tools/scenes/boats.json`). The bamboo raft has its own
    flat deck model. Water inside a hull is not masked (the pond shows through the boat); that needs a depth
    pass and is left.
  - **Minecart variants** (2026-10-29): chest (27 slots), furnace, hopper (5 slots) and TNT carts
    (`minecraft:chest_minecart` and so on, placed by item or `/summon`). A chest or hopper cart opens as its
    window when clicked sneaking. A furnace cart burns coal or charcoal (3600 ticks an item) and drives itself at
    0.2 blocks a tick, pushing in the direction it was last shoved. A TNT cart ignites on a powered activator rail
    or from flint and steel, then blows after an 80 tick fuse (stronger when fast). A hopper cart picks up items
    over it and locks while on a powered activator rail. Tests are in `McVehicleTest`; shot
    `Saved/Shots/carts.png` (`tools/scenes/carts.json`). Limits: the cargo is drawn as a plain box cut from the
    cart's texture; a hopper cart doesn't pull from containers above it (that is McBlockUse's); there is no
    primed TNT entity, so the cart keeps its own fuse.
  - **Carts take and push mobs** (2026-10-30): a plain cart going along takes in the first mob it runs into (not an iron golem),
    which sits in it until an activator rail throws it out (and none get in for half a second after); any cart shoves the
    other mobs it touches aside, harder the faster it goes (`FMcMinecartEntity::PushMobs`). Shot:
    `Saved/Shots/cart_mobs.png` (`-McScene=cart_mobs`, a cart rolled down a slope into a cow and a pig).
  - **Water out of hulls** (2026-10-30): a boat (not a raft) has a mask over the space inside its sides up to their tops
    (`FModel::MaskLayer`), drawn only into custom depth with **custom stencil 77** (`McEntityModel::HullStencil`; nothing
    else may use 77). The translucent block materials (`tools/make_materials.py`, `HULL_STENCIL`) leave out water (kind 1)
    farther than the mask where the stencil is 77, so water in front of the boat, and a waterfall pouring into it, still
    show. Needs `r.CustomDepth=3` (`Config/DefaultEngine.ini`). Whoever changes the translucent materials re-runs the
    generator rather than merging the .uassets. Shots: `Saved/Shots/hull_water_above.png` and `hull_water_side.png`
    (`-McScene=hull_water_above` / `hull_water_side`: a boat with a pig, one half under a waterfall, one on land).
    A boat under flowing water (a waterfall) is held down and sinks slowly instead of being lifted up the fall
    (`ESurface::UnderFlowing`).
  - **Flowing fluids' pictures** (2026-10-30, for main): falling water drew as a solid blue-violet box because
    `water_flow` and `lava_flow` were never in the texture array (it held only what block models name, and the fluids'
    models give only a particle), so the fluid mesher got layer 0, the missing texture, tinted. `FMcBlockTextures` now
    always loads the four fluid pictures. A side shows the top left quarter of the 32-pixel flow frames, so that
    quarter, unscaled, is a layer of its own (`water_flow#side`, `lava_flow#side`, animated with the frames) and
    McFluidMesher's sides use it with UVs 0..1 (`FFluidPalette::WaterFlowSide`/`LavaFlowSide`): 16 texels a block, as in
    vanilla. Limit: a flowing top still reads the whole frame scaled to 16 (8 texels a block), since its turned square
    reaches outside any one quarter. Shot: `Saved/Shots/waterfall_close.png` (`-McScene=hull_water_side
    -McCam=12.6,-58.0,6.5,90,-5`).
  - **Beds** (2026-10-30): in 26.3 beds are ordinary block models (`template_bed_head`/`_foot`, a picture per colour and
    face, and the new `straw_bed`), so the block baker draws them; no block entity. Checked in `Saved/Shots/beds.png`
    (`-McScene=beds`: the 16 colours, a straw bed, red beds facing each way). The 64-pixel `straw_bed` is drawn in tiles
    (Big pictures in tiles, below).
  - **Big pictures in tiles** (2026-10-30): the texture array is 16 pixels, so a block picture that is a whole number
    of 16-pixel tiles and bigger than one (signs, hanging signs and shelves 32, `straw_bed` 64; not animated ones) is
    cut into tiles, each a layer named `<picture>#tile:x,y:w,h` (`McTextureTiles`, which reads only the PNG's header).
    The baker splits every quad drawn with such a picture along the tiles' edges, one quad a tile with UVs 0..1 over it,
    so each pixel is kept; the HUD's block icons map tiles back onto the whole picture. Test `McAssets.TextureTiles`
    checks the UVs carry on across a cut (a 32-pixel face split two and four ways, and turned) and that each loaded
    tile has the PNG's pixels.
  - **Shulker boxes** (2026-10-30): drawn by `UMcChestComponent` with entity models `mc:shulker_box/shulker[_colour]`
    from the 64-pixel mob nets: a base (8 high) and a lid (12 high, over the base), centred in the block and turned by
    `facing` (placement faces the clicked face, as vanilla). Opening raises the lid 8 pixels while turning it 270°,
    eased like the chest's lid. Test `McRender.ShulkerBoxModels`; shot `Saved/Shots/shulkers.png`
    (`-McScene=shulkers`: the 17 colours, each facing, and a sign and straw bed in tiles).
  - **Shulker box lids and collision** (2026-10-31): `FMcShulkerLids` (McEntity), fitted to vanilla's logged per-tick
    state (mc-reference `shulker_log.txt`, `log shulker`): progress goes 0.1 a tick to 1 while anyone has the box open
    and back when no one does; the collision is the block grown by half the progress the way it faces (an override in
    `FMcBlockWorld`, read by `McPhysics::GatherBoxes`); while opening, what overlaps the block shifted by the progress
    (0.01 further) moves 0.11 a tick along the facing (a player on top rose 1.1, one in front of an east-facing box
    went 0.88). A box stays shut when a block's collision is in the half block in front: stone, a bottom slab, carpet
    and a chest stopped it, a top slab and a torch did not. The server counts each box's openers every tick
    (`UMcBlockUseSubsystem::TickShulkers`) and multicasts changes; every machine steps the lids, the server pushes mobs
    and items, each player's machine its own player; others' open boxes now show their lids. Test
    `McEntity.ShulkerLids`. A chunk going to a player is followed by the boxes open in it (the chunk stream's
    `ClientOpenBoxes`), whose lids start all the way up (`FMcShulkerLids::SetOpen`), so a player joining while a box is
    open sees it open. (Vanilla with two players wasn't measured; whether its joiner sees the lid up is assumed.)
  - **Bells** (2026-10-30): the block models are only the frame (bar, posts, chain), so the body is an entity model
    `mc:bell` from `entity/bell/bell_body.png` (a cup 6 across, a rim 8 across), stood in each bell by
    `UMcChestComponent`. Using a bell rings it (`block.bell.use`) when struck from a side it can swing away from: a
    floor bell's front or back, a wall bell's sides along the wall, a ceiling bell's any side (`McBell::GetStruckSide`,
    from where the player's eye is). The server tells every screen (`AMcWorldActor::MulticastBellRung`) and the body
    swings away from the blow about its top for 50 ticks, dying away (`McBell::GetSwing`). Placement: floor and ceiling
    bells face the player, wall bells face the wall, double wall with a block on the far side too. Test
    `McRender.Bells`; shots `Saved/Shots/bells.png` and `bells_swung.png` (`-McScene=bells`, `-McBellsSwung`). Not
    done: ringing by redstone, and villagers and raids hearing it.
  - **Books on lecterns and enchanting tables** (2026-10-30): an entity model `mc:book` cut from
    `entity/enchantment/enchanting_table_book.png` as the picture is laid out (two covers and a spine, a block of pages
    in each half, two loose pages), posed by how far open it is and how far over each loose page is. `UMcChestComponent`
    lays one open on the slanted top of each lectern with `has_book=true` (`GetLecternBook`: spine up the slope, facing
    the reader), and floats one over each enchanting table: bobbing, turning slowly while nobody is near, and within 3
    blocks of this machine's player turning to them and opening, its loose pages waving. Test `McRender.Books`; shots
    `Saved/Shots/books.png` and `books_wide.png` (`-McScene=books`).
  - **Lecterns** (2026-10-30): using an empty lectern with a book and quill or a written book puts one on (a `Lectern`
    block entity: the book and the page it lies open at, saved after the existing data; `item.book.put`; creative keeps
    its own). Using one with a book opens it to read (window kind `Lectern`, `FMcLecternMenu`: the pages as plain text,
    `McBook::GetPages`, and the page). The screen (`AMcHUD::DrawLectern`/`PollLectern`) is the game's book: the page
    picture from `gui/book.png`, "Page n of m", the text wrapped to 114 font pixels, 14 lines, the page arrows and Page
    Up/Down, and Done and Take Book under it. A page turn is a menu button the server answers: everyone reading it turns
    with it, `item.book.page_turn` sounds and the lectern gives a full pulse for 2 ticks (`powered`, strong into the
    block below); Take Book puts the book in the reader's inventory (or at their feet) and leaves the lectern bare.
    Breaking it drops the book. A comparator reads floor(1 + 14 (page - 1) / (pages - 1)), 15 for a one-page book
    (`McBook::GetComparatorSignal`). Scenes put books on with `"lecterns"`. Test `McSim.Lectern`; shots
    `Saved/Shots/lectern.png` and `lectern_book.png` (`-McScene=lectern`, `-McUse=2,-60,2`). Not done: reading a book
    held in the hand, writing in a book and quill, the text's colours and formatting (pages are plain), and hoppers
    taking the book.
  - **Mob heads** (2026-10-30): skeleton, wither skeleton, zombie, creeper, piglin, player (Steve's picture) and dragon
    heads and their wall forms are entity models `mc:head/...` cut from each mob's own picture (an 8 cube, with the hat
    layer for the zombie and player; the piglin's 10 wide with snout, tusks and ears; the dragon's a 16 cube with
    scales, nostrils and two jaws, at three quarters), stood by `UMcChestComponent` (`GetHeadModel`): on the floor in
    sixteen steps of `rotation`, on a wall facing out with the back against it, half way up. Placement turns floor heads
    to the player and puts heads on walls when a side is clicked (none under a block). Test `McRender.Heads`; shot
    `Saved/Shots/heads.png` (`-McScene=heads`). Not done: player heads' owners' skins (all Steve), the dragon's and
    piglin's moving jaw and ears when powered, wearing a head, and heads in the inventory drawn as models. Where the
    dragon's scales and nostrils sit is read from their sizes, not checked against the game.
  - **Re-shoots against vanilla** (2026-10-30): every block-entity scene (`tools/scenes/*.json`) is built in vanilla
    26.3 from the same JSON (`tools/scene_to_mcfunction.py --script`, run with mc-reference's `ref.sh`) and shot from
    the same camera: a spectator with FOV effects off (`fov steady`), or, with `"standing": true`, a creative player
    on the floor with the HUD hidden (`hud off`), since an enchanting table's book ignores spectators. Side by sides in
    `Saved/Pairs/`. Found and fixed: a floor head's `rotation` 0 faces north (half a turn from a sign's), the dragon
    head's lower jaw hangs 0.2 radians open, the enchanting table's open book lies back with its spine 11 degrees over
    level, covers 10 degrees short of flat and its middle 0.91 up, and a lectern's book folds 28 degrees a cover.
    Pots, bells, signs, beds and shulker boxes matched. Differences left to others: vanilla's vignette (part of its HUD:
    every HUD-on vanilla shot is darker at the edges) and clouds; horses have no model (magenta boxes).
  - **Polish list** (block entities): the enchanting book's page blocks lie flatter than its covers in vanilla and
    meet over the spine, where ours fold with the covers (measure from ref_book_level and ref_book_above).
    A worn dragon head stands 5% narrower and 15% shorter (top to lip) than vanilla's from the front
    (sc_wear_dragon_head against ref_wh_dragon), so not a plain scale: likely its snout's offset or pitch, to be
    measured side on. Zombie, pumpkin and bare heads match to the pixel.
    Conduit: the eye is drawn at 0.6 and the wind at 1.25 blocks, two-sided and unshaded, tumbling with the cage
    (fitted to ref_scene_conduits and ref_conduits_b/c with `-McScene=conduits`). Still guesses: the cage's turn and
    tilt and the eye's bob. The logged rotation field goes 0.0375 a tick while awake and stops while asleep
    (conduit_log), but how the renderer turns that into the cage's angle needs frame shots. The wind takes turns,
    22 frames of 3 ticks each: `wind.png` as rings, then `wind_vertical.png` on a box stood on end, a run of each,
    `wind.png`'s starting 20 ticks into a 66-tick count (conduit_frames.txt: one shot each 3 ticks, rings in
    ref_cf_07 to 23 and 50 to 59, upright streaks in 33 to 45). Our frame clock is the conduit's age, not vanilla's
    tick count, so the phase only matches for a conduit placed at a run's start.
    For whoever owns the water mesher: a waterlogged conduit's cell draws no water (in vanilla it's the purple
    box), so the cell shows the floor through it.
    Spawner: vanilla's spawners don't count spectators as players near (spawner_mob.txt: with only a spectator about the
    wait held at 20 and the spin at 0); we have no spectator mode yet.
    For main, with the vignette (both full-screen HUD layers): a worn carved pumpkin's `camera_overlay`
    (`misc/pumpkinblur.png`, from the item's `minecraft:equippable` data) over the first-person view.
  - **Redstone hooks for the redstone session to wire** (McRedstone is theirs): `FMcSimulation::OnBellRung` and
    McRedstone's `Bell` kind (below) are already in; further block-entity redstone input gets a hook here first.
  - **Conduit Power, measured** (2026-10-30): mc-reference's `log conduit x y z ticks` prints the player's effect and
    the conduit's state each tick (scripts `conduit_log.txt`, `conduit_rain.txt`). A conduit checks itself every 40
    ticks (and just after it is set): awake, it gives Conduit Power for 260 ticks to players within 16 blocks per whole
    7 of frame (given at 31 with 16 frame blocks, not at 33) who are in water or out in the rain; it wakes and sleeps
    only on those checks, and hunts (the eye opens) with all 42. The effect keeps the player's breath full and digs as
    Haste I (minecraft.wiki). Conduits in a chunk start their checks as it loads.
    Hunting (`log mobs`, script `conduit_hunt.txt`): each check with a target hits it for 4, the picking check
    included; with none it picks at random among wet hostile mobs whose box reaches the conduit's block grown by 8,
    and keeps it while it lives, is wet and stands within 8 blocks of the middle (McConduitHunt). Dry and passive
    mobs are left alone.
  - **Ticking block entities start as their chunk loads** (2026-10-31): hoppers (found by their block, so empty ones
    too, which before only started when placed) and part-done furnaces and brewing stands (which waited for the next
    5 second sweep) start when the chunk comes in, and so do mob spawners (below).
  - **Mob spawners tick** (2026-10-31): `FMcSpawners` in the entity host, fitted to vanilla's logged per-tick state
    (mc-reference `spawner_log.txt`, zombie spawner): a first wait of 20 ticks, then a cycle the tick after the wait
    reaches 0 and a new wait of 200 to 799; the wait stands still with no player within 16 blocks (it held at 347 with
    the player 17.4 off). A cycle makes 4 tries, each at a random point within 4 blocks across and on the spawner's
    layer or one above or below; a try that does not fit is lost (vanilla's cycles gave 2 to 4). 6 of the mob within
    the 9x9x9 box stop a cycle; hostile mobs need their own dark rule, and none come on peaceful (wiki). Spawning goes
    through the host's `SpawnMob`.
  - **Spawner polish** (2026-10-31): the mob turns inside the spawner (McChestComponent, its block entity's mob, sent to
    clients with the chunk): 20 times a second while this machine's player is within 16 blocks, the client's wait
    counts down to 0 and the mob turns 1000/(wait+200) degrees; each cycle (the server's `MulticastSpawnerCycled`) puts
    the wait back to 200 (spawner_log.txt, every tick; it stood still with the player 17 off). The game rule
    `spawnerBlocksWork` (vanilla's `spawner_blocks_work`; we had it listed as spawnerBlocksEnabled) off holds every
    spawner's wait where it is, so nothing comes out, while the mob inside still turns (spawner_rule.txt: the server's
    wait held at 20 for 700 ticks and went on when the rule came back). Scene `-McScene=spawners` (`-McSpawnerSpin=0`
    holds the mobs still, `-McSpawnerPose` tries another pose) for vanilla's spawner_mob.txt shots.
    The mob faces east at spin 0 and turns 10 times the spin toward the south (logged spins 0 and 256.2643 against
    ref_spawner_mob_pig and ref_spawner_pig_south/east/top), so it goes round about 1.7 times a second between cycles.
    Pose: size 0.5, nose up 40 degrees, fitted on the pig only. At spin 0 vanilla's pig looks a little more compact
    than ours from the south (a few degrees of heading, or the tilt's axis). Zombie and chicken, shot close at spin 0
    (spawner_mob2.txt, ref_spawner_mob_zombie/chicken against ours from `-McCam=3.5|6.5,-59.5,62.5,180,0`): the zombie
    agrees (both mostly behind the cage's middle, a little green at the top), the chicken doesn't: vanilla's shows only
    a sliver of beak at the top middle, ours its wings out at 45 degrees and its wattle well left. Likely the size
    should go by the mob's own size rather than a flat 0.5, or our display chicken flaps; it needs the displayed
    entity's scale and pose logged (log model on the spawner's display mob) before changing it.
    Logged (spawner_display.txt, mc-reference's new `log display`): the display chicken stands at rest (no flap, wings
    at no roll, yaw 0), so our chicken's wings no longer rest folded 0.08 out (chicken_fall.txt agrees). Its scale
    isn't a field; from ref_display_chicken it is about 0.55, the pig's, so a mob is no longer blown up to fill the
    cage: 0.556 of its size, shrunk further only past a block. Ours at that size hides behind the cage's middle as
    vanilla's does, but at the logged spin 110.5378 vanilla's faces the eye (beak top middle) and ours faces west:
    the 10 times spin heading, fitted on the pig at two spins, doesn't hold there; it needs more spins logged.
    Logged (vehicle_log6.txt, shots from straight above at four spins): vanilla's mob faces south at spin 0 and turns ten
    times the spin toward the east (ours turned the other way; the spin-0 "head east" reading was wrong). A mob over a
    block is drawn with its bigger side 0.62 of a block (the zombie at 0.318, by eye against 0.556 to 1.0); one up to a
    block at 0.556 of its size, so the two rules jump at a block (not measured between).
  - **Bells rung by redstone** (2026-10-30): power coming on rings a bell the same tick (sound and swing), as if
    struck on its facing side, floor and wall bells alike; power going off only clears `powered` (vanilla frames,
    mc-reference scripts `bell_redstone.txt`, `bell_wall.txt`, `bell_wall_side.txt`). McRedstone's `Bell` kind;
    the simulation's `OnBellRung` goes to the world actor's `MulticastBellRung`. Ceiling bells assumed the same.
  - **Enchanting book motion, measured** (2026-10-30): mc-reference's `log table x y z ticks` prints a table's book
    state each tick (scripts `table_book_log.txt`, `table_book_run2.txt`: 2500 ticks), and frame runs shoot it every 2
    (`table_book_frames.txt`) or 14 ticks (`table_book_bob.txt`). The state rules fit every logged tick:
    `FMcTableBook` steps 20 times a second. Near (feet within 3 blocks of the table's middle: opens at 2.9 level, not
    3.0) it aims at the player, opens 0.1 a tick and its page target jumps by -3..3 (measured counts) every tick while
    under half open, else one tick in 45; away it shuts 0.1 a tick and its aim creeps 0.02 a tick. Its turn closes
    0.4 of the gap a tick; the flip springs (0.4 of the gap, capped 0.2, nine tenths of the change taken). Each of the
    two loose pages turns over in the second part of its half-offset flip (fitted to 11 mid-turn frames). Open, each
    cover lies 37 degrees off flat and the spine's middle 0.735 up (our model's gold corners fitted to both vanilla
    views together, to 2 pixels; the earlier 10 degrees and 0.91 drew them a fifth too wide). Left: vanilla's page
    blocks lie flatter than its covers and meet over the spine, where ours fold with the covers; a book
    opening part way folds its covers as our model's shut does (gold corner fits). The bob is two waves of the table's
    age, 0.010 blocks at 0.1 a tick and 0.026 at 0.02 (fitted to 90 frames to 0.1 pixel).
  - **Decorated pots and conduits** (2026-10-30): a pot is an entity model `mc:decorated_pot` (a 14 by 16 body with
    an 8 by 4 neck: its top, bottom and neck from `decorated_pot_base.png`, each side the middle of a side picture),
    turned by `facing`. A conduit is its 6 cube shell (`mc:conduit/shell`) while asleep and, awake, the 8 cube cage
    turning slowly with the eye inside looking at the player (`mc:conduit/cage`). Awake is `McConduit::IsActive`:
    waterlogged, water in all 26 blocks round it, and at least 16 of the 42 frame places (three rings five across)
    holding prismarine, prismarine bricks, dark prismarine or sea lanterns; the view looks again twice a second. Test
    `McRender.PotsAndConduits`; shot `Saved/Shots/pots.png` (`-McScene=pots`). Not done: its wobble and what it
    holds; the conduit's wind, its closed eye when hunting, Conduit Power and attacking hostile mobs (none of which
    the server does yet).
  - **Pot sherds** (2026-10-31): a pot keeps its four sides, back, left, right and front (`McPot`, in McItem; the
    item's `minecraft:pot_decorations`, left off when all are bricks). Crafting puts the diamond's top, left, right and
    bottom on them; placing the pot gives the block a `Pot` block entity with them (sent to clients with the chunk, as
    signs are), and breaking it drops the pot with them, or, hit with a tool of `minecraft:breaks_decorated_pots`, the
    loot table's cracked case and the four sides as items. Each side of the model is its own layer, drawn with the
    plain side or its sherd's `<name>_pottery_pattern`: facing south, the front south, the right east, the back north,
    the left west (vanilla, mc-reference `scripts/heads_pots.txt`, `ref_pots_front`). In the inventory the pot is drawn
    in 3D with its sherds, by the item model's gui display (turned 45, tipped 30, at 0.6), as the heads are. Tests
    `McItem` (crafting with sherds), `McRender.PotsAndConduits`; shot `-McScene=sherds`.
    Dropped, a pot is its own model with its sherds at a quarter size, bobbing and turning as a dropped item's picture
    does (the item entity's id carries the sides, `minecraft:decorated_pot=<back>,<left>,<right>,<front>`); size and
    height match vanilla's (mc-reference `conduits.txt`, `ref_conduit_pots`, against `-McScene=conduits
    -McCam=13.5,-59.2,5.0,180,25`; `-McPotDrop=<scale>,<lift>` tries others). Dropped stacks merge only with the same
    components now (a sherd pot no longer joins a plain one). Scenes take `"items"` (dropped, never picked up), for
    both sides (`tools/scene_to_mcfunction.py`). Not done: the held pot, which belongs to the held-item drawing
    (`McHand`, on main's branch), and other blocks dropped in 3D (all dropped items here are flat pictures).
  - **Fishing** (2026-10-30): using a fishing rod casts a bobber (`FMcFishingBobber` in McEntity, `McFishing`) from the
    eyes along the look, at 1.1 blocks a tick looking level and harder the steeper (measured from vanilla casts at
    eight pitches, mc-reference `scripts/bobber_cast*.txt`); it flies under gravity 0.03 and drag 0.92 a tick (measured), hooks the first mob, player or item it touches,
    lies on ground or sticks to a wall, and in water floats on a spring at the surface. In water the wait for a bite is
    100 to 600 ticks, 100 shorter at each end per level of Lure; under open sky in rain a tick in four counts double,
    and without sky half the ticks don't count. Then a fish swims in for 20 to 80 ticks (its wake drawn on the water)
    and bites for 20 to 40 (the bobber dips, a splash). Using the rod again reels in (`McPlayerFishing.cpp`): during a
    bite the catch is rolled from the data's `gameplay/fishing` loot table (`McLoot::RollFishing`: luck of the sea
    adds each entry's quality, the treasure needs open water, a 5 by 4 by 5 box of only water and air, and the jungle
    bamboo needs a jungle) and flung to the angler with 1 to 6 experience; a hooked mob or item is pulled toward them.
    The rod wears 1 for a catch, 2 on the ground, 3 for an item, 5 for a mob. The bobber goes when the rod is put away,
    or is 32 blocks off; one lying on the ground stays (a vanilla bobber on stone was still there after 1600 ticks,
    though the wiki says 1200). Drawn as the hook picture facing the camera, with a
    black line hanging slack to the rod tip in the angler's hand (`UMcEntityModelComponent::SetFishingLine`); the hotbar shows
    the cast rod. Sounds: `entity.fishing_bobber.throw`, `.splash`, `.retrieve`. Test `McPlayer.Fishing`; shot
    `Saved/Shots/fishing.png` (`-McScene=fishing -McCast=6`). Not done: the rod and line in a third-person hand (the
    player model holds nothing), Lure's and luck's effect on mobs' fishing, and fishing advancements; not yet measured: the
    caught item's throw and the float's springiness and dip (set by eye).
  - **Elytra** (2026-10-30): worn in the chest slot (the data's equippable slot), a jump in the air starts a glide
    (`McPlayerElytra.cpp`), which ends on landing, in water or lava, on a ladder or vine, or when the wings are down to
    their last point (they never break: 432 points, 1 a second while gliding, none from blows). Gliding the body is a
    0.6 cube with the eyes 0.4 up, falls no further than a dive of 0.5 a tick allows for fall damage, and flying into a
    wall hurts by 10 times the sideways speed lost less 3. The flight (`McElytra`) is this port's own model, its numbers
    fitted to vanilla glides measured from positions (mc-reference `scripts/glide_steady.txt`, `glide_turn.txt`,
    `glide_speed.txt`): level flight about 24 m/s after 10 s, sinking about 3 m/s and gliding about 10 to 1 (wiki), 20
    down about 37 m/s, a vertical dive averaging 60 m/s over 10 s, a 90 degree turn shedding speed, and climbs that
    stall (ours lose speed ahead faster than vanilla's in a steep climb). A firework rocket used while gliding is spent
    and pulls toward 33.5 blocks a second along the look, halving the gap each tick (measured), for 10 (flight + 1)
    ticks plus up to 11 (wiki; `entity.firework_rocket.launch`). Other players see wings on the back
    (`McPlayerModel::BuildWings`: two 10 by 20 by 2 boxes from `equipment/wings/elytra.png`, hung from behind the
    shoulders and fanned 15 degrees, matching the vanilla husk in `ref_elytra_back.png`), and a glider lying along the
    look, head bent back, wings spread. Test `McPlayer.Elytra`; shots `Saved/Shots/elytra_back.png` and
    `elytra_glide.png` (`-McGlide`, `-McSelfShot` with `-McSelfShotYaw` and `-McSelfShotRise`). Not done: the rocket
    entity and its trail, rockets' explosion damage, the wind sound, the camera roll, wings on armour stands and mobs,
    and the server checking the glide.
  - **Totem of Undying** (2026-10-30): a blow that would kill (anything but the void, which `FMcPlayerStats::LastHurtKind`
    tells) uses up a totem in the main hand, or else the off hand (vanilla measured with one in each: the main hand's
    goes, `ref_totem_hands.png`; `McTotem::TryUse`, called from the server's tick just before the death check in
    `McPlayerTotem.cpp`): the player lives on with 1 health, every effect taken away, then Regeneration II for 45
    seconds, Fire Resistance for 40 and Absorption II for 5 (wiki), and `item.totem.use` plays. Every machine then shows
    it (`MulticastTotem`): green and lime-yellow sparks thrown out from the body for 15 ticks, drawn with the
    `glitter_7`..`glitter_0` sprites over their lives (`particles/totem_of_undying.json`; `UMcParticleComponent::TotemBurst`,
    its own mesh section), and on the holder's screen the totem's picture grows in the middle from a tenth of the
    screen's height to just over half in 4 ticks, turning half round, and hangs there until 20 ticks (`McTotem::GetFlight`,
    drawn by the HUD under the hotbar). Every timing, size and colour is measured from vanilla shots 2 ticks apart
    (mc-reference `scripts/totem_ticks.txt`, `ref_tk_*.png`). Test `McPlayer.Totem`; shot `Saved/Shots/totem.png`
    (`-McTotem=seconds` puts one in the off hand and deals a killing blow). Not done: the totem drawn as a thick 3D item
    and tilted as vanilla's, the effects' own particles (vanilla's coloured rings round the player from about tick 20),
    the Postmortal advancement and the death message's statistic.
  - **Sign text** (2026-10-30): signs keep their words in a `Sign` block entity (`FMcSignSide` for each side: four
    lines, a dye, glowing; and waxed), saved after the existing data of that entity only, so older saves load with blank
    signs (`McSign` in McWorld has the kinds, facing, board places and text colours). The chunk stream sends signs' entities
    with their chunks (`McChunkCodec` version 3; containers' contents stay on the server) and each change after
    (`ClientBlockEntity`, from `FMcBlockWorld::OnBlockEntityChanged`). Placing a sign opens the editor (`FMcSignMenu`,
    the window kind `Sign`); right-clicking one opens the side the player is in front of, unless it is waxed. The screen
    (`AMcHUD::DrawSign`/`PollSign`) shows the board with four lines, each stopped at the board's width in the font's
    pixels (90, a hanging sign's 60: `McFont`, shared from McAssets), a cursor that moves with the arrows, Home and End,
    and Done or Esc to send; the server cuts each line to the width again. A dye recolours text, a glow ink sac makes it
    glow and an ink sac stops that, honeycomb waxes it (each used up outside creative, with its sound; a waxed sign
    plays `block.sign.waxed_interact_fail`). `UMcSignComponent` draws the text on standing, wall, hanging and wall
    hanging signs, both sides, at 1/96 of a block a font pixel (hanging 0.9/64): plain text at four tenths of its dye,
    glowing text full bright with its eight-way outline (cream round black, seen from any distance; the others' only
    within 16 blocks). Placement now turns signs: standing in sixteen steps towards the player, wall signs on side faces,
    hanging signs under a block (or on top with something above) and wall hanging signs across the face clicked.
    Scenes take a `signs` list (`tools/scenes/signs.json`, which `scene_to_mcfunction.py` turns into `data merge`).
    Shot: `Saved/Shots/signs.png` (`-McScene=signs`). Limits: formatting codes and other fonts (unicode) are not drawn;
    the text is lit as a block entity, not by the block's own light.
  - **Boat paddles** (2026-10-30): every boat and raft has two paddles, cut from its own picture, pivoting on the gunwales (a raft's
    on its deck edges, clear of a chest). They rest slanting out over the sides and row while a rider pushes: both going forward, the left turning right and the right turning left,
    neither going back (`FMcBoatEntity::GetPaddleBits`, sent in the entity's count). Shots: `-McScene=paddles` (`-McPaddling` rows them without a rider).
    Refitted 2026-11-01 from paddle_log.txt (mc-reference's new `hold <key> <ticks>` rows a boat; `log model` gives the
    paddles' angles each tick): a 16-tick stroke, the pose and the hull's paddle mount ported from the logged angles
    (`PaddleLeft` in McEntityModel). The raft's (vehicle_log3.txt, log model bamboo_raft) row the same stroke from a
    pixel lower: 9 out, 10 up, 3 forward. The raft deck is two boxes, the lower one 28 by 16, from 8.1 up.
  - **Two riders** (2026-10-30): a boat carries two players (`FMcVehicle::GetCapacity`; a cart one). The first in is the driver and
    steers; the second sits behind (seats a fifth of a block forward and six tenths back when two are aboard). If the driver
    gets out the other drives. A pawn is a seat by its key (`SetRider(..., Key)`), so a full vehicle turns a third away.
  - **Boats take mobs** (2026-10-30): a boat takes in any mob narrower than itself that it touches while it has a free seat
    (a plain boat two, a chest boat one: the chest has the back seat). The mob sits (`FMcMob::RidingId`, not saved) doing
    nothing until the boat goes, sunk four tenths of its height into the hull; animals sit across it. Players count against the seats, so a boat with two pigs turns a
    player away. Shot: `Saved/Shots/boat_mobs.png` (`-McScene=boat_mobs`, side-on: a boat with a pig and a chicken, a chest boat with
    a sheep, summoned with `-McChat`).
  - **Chests** (2026-10-30): placed chests are drawn from their entity pictures (`UMcChestComponent`, which finds them in the
    loaded chunks and stands an `mc:chest/...` model in each): chest, trapped, ender and the four copper chests (waxed too),
    single and double, turned to their facing, Christmas pictures on 24 to 26 December. The lid opens over ten ticks while
    this player has the chest open, both halves of a double together. The chest picture's net is the other way up from a
    mob's, so the boxes are turned half round (`AddChest`, which the chest minecart uses too). Shots: `-McScene=chests`
    (`-McChestsOpen` opens every lid), `Saved/Shots/chests_shut.png`, `chests_open.png`. Limits: another player's open
    chest doesn't open its lid here (who has a chest open isn't sent), and the chest is lit as an entity, not by the
    block's light. A half's picture follows where its other half is: `_left` is the half whose other half is on its +x
    (as turned), which `McChestLink` calls `right`. Not done: the other block entities whose block models are only a
    particle (beds, shulker boxes, skulls and heads, decorated pots, conduits, copper golem statues, end portals and
    gateways), a bell's body, and the books on lecterns and enchanting tables (sign text is done: see above).
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
- **Bow pose** (2026-10-30): drawing stands the bow side-on, arrow forward, limbs up, string toward you (`McHand::GetBowStandRotation`), moved (-2, -20, 20), scaled 1.2, yawed 22 and rolled 7 degrees in the camera's space from where it rests (`GetDrawTransform`), (idle scale to 1.2 times it). Fitted by overlaying our shot on the reference's at 5 and 12 ticks of draw (limbs, string column and size within roughly 10 percent; the string is not yet an exact vertical column). Held flat items are a true one-pixel extrusion of the picture (`BuildExtrudedMesh`), no doubled outline. The near plane is 1 unit (the drawn string comes within 2 of the eye). Polish left (Jeff, aeb69aa): the lens between limb and string is narrower than vanilla's, the arrow sits more level than vanilla's slight downward slant, and the upper limb leans less. Not yet measured: whether the bow trembles at full draw (none now). **Arrow launch** (2026-10-31, measured: scripts/spread.txt with the new `log arrows` verb, 50 full draws at yaw 0 pitch 0, each arrow's first two ticks): the arrow starts on the eye's line (x and z exactly) 0.1 block under the eye (`McCombat::GetBowLaunchDrop`; ours was 0.3 ahead of the eye), its speed is 3.000 blocks a tick (sd 0.022), and its direction has a spread of 0.0090 sideways and 0.0082 up (sd of the launch direction's slope; kurtosis of 50 samples 1.7 to 2.0, a Gaussian would be 3: more samples would tell whether it is a Gaussian or flatter), added per component to the look before the speed scales it, 0.0085 each in `GetBowAim`. The air then slows an arrow by 1 percent a tick and drops it 0.05 a tick (measured as vz2/vz1 0.990000, gravity -0.0500; already what the arrow does). Not measured: whether the spread depends on the draw (only full draws), the shot sound, and the crit trail. **Hurt tilt** (2026-10-31, measured: scripts/hurt.txt, hurt2.txt with `shot` lines that print the hurt time, a wall of columns, a hit from the front, back, east and west by an armor stand): the view rolls after a hit for the hurt time (counted down from 10; the first value the client shows is 8). The roll, taken from the lean of the columns (structure tensor of the top of the picture, so a measure that reads about 1.2 times the true angle), is 18.9, 10.6, 5.8, 3.0, 1.2, 0.2 degrees at hurt times 8 to 3, a power law 41.1 * (t/10)^4.08 (`McCombat::GetHurtTilt`, scaled so that our shot at hurtTime 7 reads 11.6 as the reference's east and west hits do); from the east +11.6 and from the west -11.6 at hurt time 7, from the front or the back about 0 (+-1): the roll goes with the side, `GetHurtSide`, the sine of the angle assumed between the four measured directions, +1 for a hit without a place (the reference's damage without a source rolled the same way as one from the east). The roll is `AddAdditiveOffset` on the camera (it multiplies on each call: cleared each frame; the engine's roll is clockwise, ours is turned to match); `-McHurtHold=N` shows the roll of N ticks, for shots. Hurt time 10 and 9 were not seen at the end of any client tick, so the roll is capped at the value of 8. Not measured: oblique hits, the roll's dependence on damage, and the damage tilt option. Remote players get the side by `ClientHurtSide`; untested over a network. **Bow tremble at full draw** (measured: scripts/fulldraw.txt, fulldraw2.txt, 42 shots on a 1-tick grid): no shake of the world; the bow's pixels move along a line (x 0.87, y 1.39 px of amplitude at 720p) with a period of 7.8 ticks (0.1285 cycles a tick, residual 0.27 px), from the full second of draw on (`GetBowTremble`; the phase is not known, nor whether it builds up earlier). **Arrow sounds** (2026-10-31, measured: scripts/sounds_bow2.txt with a new `log sounds` verb, 70 shots at a wall 12 blocks away; scripts/sounds_bow.txt a first 15): volume is 1 for the shot and the hit. Shot pitch, 40 full draws: min 1.1262, max 1.3273, mean 1.2120; 30 draws held 8 ticks: min 0.8149, max 1.0006, mean 0.9016. Hit pitch, 70 hits: min 1.0927, max 1.3300, mean 1.2003. A uniform range fits each set (KS against the range widened by one spacing: p 0.23, 0.53, 0.33), and so does a uniform spread of the reciprocal (p 0.75, 0.95, 0.93), so 70 samples do not tell the two shapes apart: the simplest, a plain uniform range, is used (`GetBowShootPitch` 0.62 + 0.21 U + power/2, `FMcArrowEntity::GetHitPitch` 1.09 + 0.24 U), and the shape is NOT fitted. The power term is two points only (the midrange moves 0.319 between power 1 and about 0.32 to 0.37; 0.5 per power is a rounding). Heard only within 16 blocks: a hit at 20 blocks is not started by the client. Not measured: the bow draw itself has no sound; other draws. **Draw zoom** (2026-10-31, measured: ref_zm_* from scripts/zoom.txt, a wall of one-block columns 9.5 blocks away on a platform, survival on the ground, no `fov steady` because that removes the zoom; focal length fitted from four silhouette edges, residual under 1 px): the focal length against the unzoomed 513 px is x1.000, 1.006, 1.033, 1.085, 1.174, 1.227 and 1.227 at 2, 5, 10, 15, 20, 25 and 30 ticks of draw, which as a field-of-view angle (70 degrees) is 1 - 0.15 t^2 with t the seconds of draw up to 1 (the shots land about 2 ticks into the draw; 0.033, 0.164, 0.412, 0.799 and 1.006 of 0.15 against t^2 of 0.0225, 0.16, 0.42, 0.81 and 1.0). `McCombat::GetBowFovScale`, applied to the camera in `McPlayerPawn` while the bow is drawn and put back on release. Confirmed in our game (zmA0 and zmA20 with a column wall): the column edges at no draw are 451, 559, 721, 829 px and at a capped draw 408, 541, 739, 872, identical to the reference's at 0 and 25 ticks. Not measured: the zoom on the way back (release is instant here), the spyglass, sprint and speed FOV effects. Rechecked with `fov steady` (2026-10-31, ref_bowfs_* from scripts/bowfs2.txt on a clean sky platform; the first rerun in the shared 1070 world had the wrong slot selected and is void): the bow's size and position at 5 and 12 ticks match the earlier shots (bow pixels within 4 percent), so the fit was not an FOV artefact (the hand is not scaled by the world FOV); ours at 12 ticks (bw12) still sits within about 10 percent as before. The idle and 24-tick shots differ between runs by draw timing only.
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
    - [ ] Mineshaft cave-crossing bridge reach (10 blocks) is UNFITTED, a guess: measure the longest bridged gap in a vanilla
          mineshaft over a cave or ravine (`log states` or heights row along the corridor) and fit to it.
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
   - **Held items on mob models (2026-11-01, `scripts/held.txt`):** a mob has `HeldItem` (server, replicated to `AMcEntityActor`, `SetHeldItem` on the model); a model with `HeldLayer` draws that item's own picture (`FindItemTexture`) as a flat picture on the right arm (the illagers' axe placement: hand at the arm's end, sprite 12 wide, blade up and forward). Zombie, skeleton and vex have it; skeleton spawns with a bow, vex with an iron sword, `-McMobRowHeld=<item>` puts one in a shot row. Exact placement (2026-11-01, `log model` now prints `[hand LEFT/RIGHT]` matrices): the picture sits where vanilla's item chain puts it, arm matrix, Rx -90, Ry 180, translate (-1/16, 0.125, -0.625) and the handheld display transform (rot 0,-90,55, translate 0,4,0.5, scale 0.85); the vex hand frame is its arm scaled 0.55 plus a measured shift. Only the arm matrix and the vex scale/shift are logged numbers; the Rx/Ry/translate extras are RECALLED (clean-room problem) and still to be measured: `E:\mc-reference\scripts\hold.txt` shoots an armor stand holding an iron_sword in two arm poses from front/side/top and logs its hand matrix (`run/screenshots/ref_hold_*.png`, `log_hold.log`); the silhouette fit of rotation (24 axis-aligned) and offset against those shots was started and not finished, if it cannot pin them down the hold must be marked unfitted. Our zombie and vex shots (`hz3.png`) agree with the reference side shots by eye (blade up and out about 25 to 30 degrees from vertical, vex sword near-horizontal), no angle was measured. Skeleton bow aim (2026-11-01, `scripts/skel2.txt`, `log model skeleton` while it drew on a survival player): with a target, both arms straight out at -pi/2 pitch, hands turned 0.1 in, swaying 0.05 opposite each other (phase not pinned by the samples), arms near rest otherwise; the server sets pose 1 while a skeleton has a target (`FModel::bBowAim`). The bow is still the flat picture on the arm (vanilla's own display transform is not fitted), and the iron spear a zombie can spawn with is likewise the flat inventory picture, not vanilla's `iron_spear_in_hand` hold. Not done: the charging vex's sword was only compared by eye (hv3 shot timed out, not retaken), piglins, wither skeletons, zombified piglins, drowned (trident), witches and villagers are not on the server or not wired, and the crossbow/axe on pillagers and vindicators still use their own fixed layer.

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
- **Held item lowering and the use swing** (2026-10-30; measured with E:\mc-reference\scripts\equip2.txt, shots at ticks 0 to 8 after each event, and snowball.txt): the held item is lowered and raised again (about 0.12 s each way, two to three ticks) when it is changed for a different item and when the stack of the same item grows (a pickup); it is NOT when the slot is switched to an identical stack, nor when the count drops (placing a block, throwing). The 'item vanishes for ticks 1 to 3 after a throw' of snowball.txt was the use swing itself (the script's `use` pressed the key, which swings), which carries the item toward the middle and down out of the view and back by tick 6: now `McHand::UseSwingMove` (-46 right, -40 up, in units) times the swing stroke for a right-click swing with an item. Only that it is out of view at ticks 1 to 3 and back by 6 is measured, so the two numbers are chosen to put it off screen; the sword's attack swing is untouched. TODO: the swing frames cannot pin UseSwingMove beyond off-screen at ticks 1-3 with a sliver at the bottom centre; a snowball held at several known offsets would. Finishing a meal looked like a lowering too (the apple is away for four ticks), not yet done; a breaking tool did not show (the shot kept the tool).
- **Vignette** (2026-10-30, redone 2026-10-31): the cache's own picture, textures/misc/vignette.png (grey 0 in the middle, 0.39 at an edge's middle, 0.82 in a corner), is drawn stretched over the screen as black with its grey times a strength as the opacity, under the HUD (gone with F1). The strength is NOT a function of the sky alone: it eases (about 0.87 percent of the way a tick, roughly 115 ticks to 63 percent) towards a target that falls with the light at the eyes' cell, `McGui::GetVignetteTarget`: 1.0 at light 0, 0.94 at 2, 0.90 at 4, 0.83 at 6, 0.75 at 8, 0.62 at 10, 0.57 at 11, 0.38 at 13, 0.19 at 14 and 0 at 15 (noon outdoors, measured 0.000). Measured (ref_v5 to v7: a hall whose far wall is lit from 22 blocks, a light block in the eye cell, each level held 500 ticks; strength = fitted opacity against the picture's grey, on/off HUD pairs, residual under 0.5 percent at the edges). The earlier 0.356 noon and 0.289 night were the ease in flight. The HUD takes the larger of the block light and the sky light times the sky factor at the eye cell (night outdoors 15 x 0.24 = 3.6, target 0.91, measured 0.893), worked out twice a second. Levels 3, 6, 9 and 12 of the table are filled in between their neighbours, not measured; the start value after joining is the target (the reference's is not known). Heads-up: the vignette light reads `McSky::GetSkyLightFactor()`; redstone's 78c2e23 (claude/redstone-1fae88, not merged) changes the sky-darkness constant 0.2 to ~0.505, which moves dawn and dusk (noon and midnight fits hold). When the branches meet, take one vanilla dusk shot to confirm the vignette.
- **Where I stopped (side session of 2026-10-31, paused by the user)**: everything measured is committed (last f4477e3: hurt tilt and bow tremble). Open: (1) cloud face shading: `E:\mc-reference\scripts\cloudshade.txt` (tops from above, four oblique yaws, bottoms from below, `fov steady`) ran, and the shots are in `E:/mc-reference/run/screenshots/ref_cs_*.png` (top, y0, y90, y180, y270, bottom, bottomobl), not yet measured; measure each face's brightness against the top, then set the shades in `UMcCloudComponent::Rebuild` (now 1.0 top, 0.7 bottom, 0.9 east/west, 0.8 north/south, unfitted); cloud phase is also unmatched. (2) Re-take our hurt-7 shot with the new amplitude (`-McFlat -McWorld=<new> -McHurtHold=7`, the nine-column wall chat, `-McInput=look:0,0;wait:1;shot:<n>;end -McInputDelay=2`) and check the roll reads about 11.6 by the structure-tensor measure (hurt0 baseline -0.44). (3) Still owed: TNT, note blocks, jukebox/discs, toasts, campfire smoke, compass/clock/maps, advancements, Nether/End lighting in Unreal Mode, eat-finish lowering, tool-breaking re-equip, bow polish (lens width, arrow slant, upper limb lean), verifying the shot-wait cherry-pick (7771ee3), vignette at dusk after the redstone sky constant merges.
- **Clouds checked against the reference client** (2026-10-31; ref_zen_*, ref_zn_*, ref_ch_*, ref_dr_* straight-up shots, ours cz_c/cz_g from the 1070): `McCloudComponent` already matched, so no code changed. Cell size 12 blocks and the height 192.33 (from the dimension type) hold: the reference cell is 60 px at an eye at y 101.6 and 21.6 px at -58.4 with the same focal length, which solves to a height of 191.3; the drift is two cells in 800 ticks (0.03 blocks a tick) towards -x, the pattern repeating every 256 cells. The straight-up pattern of both clients matches clouds.png at 93 percent with the same mirroring once ours faces south (yaw 90 in -McInput), so the texture is laid the same way. A first estimate of a height of 286 and a 77 degree vertical FOV was the spectator/flying FOV widening of a run without `fov steady`, not the clouds. Not matched: the phase (which cell is over a place at which game time) is not read from the reference, so the pattern is the same but not at the same spot; the colour and shading of the faces: see the next note. **Cloud faces** (2026-10-31, vanilla ref_cs_* from scripts/cloudshade.txt, ours cs_u_* and cs_v_*): the lighter shaded sides were the faces being drawn inside out: swapping the axes into the engine's mirrors the corner order, so the near faces were culled and the far ones drawn (from below the top showed, from above the bottom). The triangle order in `UMcCloudComponent::Rebuild` is flipped. The shade table (top 1.0, east/west 0.9, north/south 0.8, bottom 0.7) was already right: with the clouds 0.8 opaque, the reference reads top (238,244,254) over its sky (184,210,254), north/south (198,206,216), east/west (219,225,235), bottom (166,176,196) over (128,172,254), and ours now reads bottom (169,176,196), sides (223,227,234), top (244,248,253) over a lighter sky. The phase of the pattern is still not matched.
- **TODO held decorated pot** (2026-10-31): drawn flat here; the reference draws the 3D pot model. Fit the held pose against vehicles' pot mesh after that branch is merged (not copied): reference shot ref_pot_idle.png (scripts/pot_held.txt, a plain pot on a sky platform; the sherd variant's pot_decorations syntax needs fixing for this version).
- **Primed TNT** (2026-11-01; measured with E:/mc-reference/scripts/tnt.txt, tnt2.txt, tnt3.txt through `log ents tnt`, logs log_tnt*.log): fuse 80 ticks; per tick gravity 0.04 added, then the move, then drag 0.98, and on the ground (0.7, -0.5, 0.7) on top; lit from a block it starts at the block's centre with 0.02 sideways (random angle) and 0.2 up; blast power 4 at 0.0625 up from its feet (box 0.98). Player damage 1+56*(I^2+I)/2 unrounded at normal difficulty (7 blocks 4.94, 6.265 8.39, 5.1665 14.43), knockback I*unit(centre to eye at 1.62) added to the velocity (`McExplosion::GetKnockback`, `ServerBlast`; armour reduces: `EDamage::Explosion`). Blast sound pitch at volume 4: a triangle, centre 0.699, half-width 0.131 (537 blasts, tnt3/4/5 logs, 0.5776..0.8298). A blast that reaches a TNT block lights it with a chain fuse of 10..28 uniform (47 pairs in tnt5.txt; the 432-TNT run tnt4 lagged and its lifetimes are not trustworthy). In tnt5 only about a third of the TNT next to a blasting one lit (17 of 48 per round); ours lights every one the blast destroys, so check what vanilla spares (`log box` after a pair);  chain TNT made during a blast gets no push from that blast. Open: crater size against the reference (9 stone for TNT on a stone slab), no priming sound heard from redstone ignition, flint-and-steel sound unmeasured, mob blast knockback still from the box middle, primed TNT not saved, no scale pulse. **For the redstone session**: call `FMcSimulation::LightTnt(Pos)` (fires `OnLightTnt`, wired in McWorldActor to `FMcPrimedTnt::Ignite`) when power on a TNT block rises or a neighbour changes to powered; fire next to TNT, flint and steel / fire charge, dispensers and blasts already call it. `FMcPrimedTnt::Ignite(Manager, Cell, Fuse)` does the rest. `/summon tnt` works.
- **Note blocks** (2026-11-01; reference runs scripts note1 to note4 with `log sounds`, which now also prints the sound's range; wiki "Note Block" for the particle colours, the use cycle and the instrument names): `McNoteBlock.h`, `FMcSimulation::PlayNote/TuneNote`. Instrument by the block below from a table measured block by block (522 non-harp blocks, `McNoteTable.inc`, harp otherwise; stairs, buttons and plates answer as the block they are made of); a head above wins (zombie, skeleton, creeper, dragon, wither skeleton, piglin; a player's head is silent). Pitch 2^((n-12)/12) exactly for notes 0 to 24; volume 3, range 48 for all but the piglin voice (1.98, 31.7); heads ignore the note: pitch 1, the creeper's 0.5 (also at notes 12 and 24). A block above that is not air or a head silences it (carpet, slab, stone all did) but a use still steps the note. Use steps and plays (25th back to 0), a survival punch (`OnPunchBlock`, start of a dig) plays. Sand, gravel and concrete powder are listed as snare from the wiki; the reference gave snare for gravel, heavy core and some powders (different ones each run, falling blocks under a note block are unsteady) and harp for sand. Note particle colours are the wiki's table (`GetNoteColour`); the particle itself is not drawn yet (needs a texture path in the particle component and a client event). **For the redstone session**: when power on a note block rises (and `powered` was false) set `powered=true` and call `FMcSimulation::PlayNote(Pos)`; clear `powered` when the power goes. Nothing else is needed from them; the instrument follows block changes in `FMcSimulation::OnBlockChanged`.
- **Jukebox** (2026-11-01; reference runs scripts jukebox3 to jukebox6 and jb4/jb6 shots through ref.sh with `log sounds`; cache `jukebox_song/<id>.json` for length, comparator output and sound, lang `record.nowPlaying`): `McJukebox.h`, `UMcBlockUseSubsystem` (`UseJukebox`, `SyncJukebox`, `TickJukeboxes`). One-slot container, discs only (`jukebox_playable`), a full jukebox rejects a second disc, use with a disc inside ejects it. Hoppers insert from above and the side and extract from below (measured). The disc stays after the song ends (length from the cache, e.g. cat 3700 ticks). Sound range 64 blocks (volume 4), linear falloff like all sounds; `has_record` follows the slot. A note rises every 20 ticks (measured; 7 ticks life, 0.19 up per tick, friction 0.66, colour from the note table). "Now Playing: C418 - 13" overlay above the hotbar for 3 s, lime for the first 10 ticks then rainbow, fading in the last second (colours and timing fitted from the jb4/jb6 frames; the fade was not cleanly measured). Comparator outputs match the cache for all 22 discs. The note particle is also drawn for note blocks now (`FMcSimulation::OnNoteParticle`). **For the redstone session**: in `ContainerLevel` for `minecraft:jukebox` return `McJukebox::GetComparatorOutput(item in slot 0)` (0 when empty), read from the chunk block entity. McRedstone.cpp untouched. **Missing**: the held-item name label above the hotbar (vanilla shows "Music Disc" in its place on slot change).
- **Jukebox measurements replaced estimates, held-item label** (2026-11-01; reference runs jukebox7 (70 stepped shots, chat cleared by the new `clearchat` verb), jukebox8 (90 shots two ticks apart with game ticks), helditem and helditem2): note particle size 0.1375 (glyph 30 by 48 px at 3 blocks, jukebox face 181 px at 2.5; ours was 75 px tall at 0.215) and rise 0.2 a tick (30, 20, 13, 9, 6, 4 px a tick, friction 0.66, 7 ticks). "Now Playing" lime for 10 ticks, hue falling 1/50 a tick, linear fade over the last 20 of 60 ticks (strength 0.9, 0.8, 0.68, 0.57, 0.46, 0.36, 0.28, 0.21 at ticks 36 to 50): the existing code already matched. Held-item label (`DrawHeldName`): 40 ticks, full for 30, fade over the last 10, text top 45 GUI px above the bottom (creative) or 59 (survival, with the bars), colour by the item's `minecraft:rarity` (rare 55FFFF, epic FF55FF, uncommon FFFF55); shot tops matched the reference (585 and 543 px).
- **TNT chain ignition checked against the blast-ray hit pattern** (2026-11-01; reference run tnt6, three solid 9x9x9 TNT cubes each with a fuse-10 TNT summoned in the hollow middle, `log ents tnt`; the TNT entities first seen on the first blast tick are the blocks its rays destroyed): the first blast lit 447, 508 and 497 of 728 blocks (mean 484, about two thirds; the rest light from the lit ones' own blasts), by distance 3.5 and under all, 0.89 at 4.0, 0.79 at 4.5, 0.46 at 5.0, 0.15 at 5.5, 0.02 at 6.0. Our `McExplosion::Collect` (1352 rays, 0.7 to 1.3 times the power, 0.3 steps) gives 486 on average and the same curve, so nothing needed fixing: "a third lights" is how vanilla behaves too. `McClient.McEntity.ExplosionTntCube` holds the shells and the manager's own blast to the reference. The third cube (x -40) ran looser than the other two (0.74 at 4.0 against 0.97); the test bands take in all three.


- **Primed TNT saved, priming hiss, mob blast knockback** (2026-11-01; reference runs tnt5, tnt8, tnt9). A lit fuse now survives a save: `FMcPrimedTnt::Serialize` writes the fuse with the entity, `FMcEntityHost::RestoreChunk` rebuilds it (`McClient.McServer.SavePrimedTnt`). `log sounds` in tnt5: every TNT lit by redstone (36 of them) hissed `entity.tnt.primed`, none lit by a neighbour's blast did; `Ignite` plays it only for the former. Mobs in tnt8/tnt9 (pigs, cow, chicken, sheep, wolf) were pushed by exactly the blast's knockback, no melee knock on top: direction from the blast to the mob's eye, strength `1 - dist/(2*power)` times exposure. Eye heights measured: pig 0.77, cow 1.31, chicken 0.65, sheep 1.24 (`McExplosion::GetMobEyeHeight`; others 0.9 of the body height until measured). `McClient.McEntity.Explosion` checks the cow's push and the single hiss.

- **Cloud phase** (2026-11-01; reference run cloudph, three straight-up shots 200 ticks apart): the pattern's column is floor((x + 0.03*gameTick)/12), sub-cell phase matched exactly in all three; rows are floor((z + 3.9)/12) (+-0.3), texture rows along z, columns along x, no flips. Ours drifted by the day time (jumped on `/time set`) and had no z offset. Now `AMcWorldActor` keeps a game clock (`GetGameTicks`, replicated once a second like the day time, never touched by `/time` or the daylight rule) and `McClouds::ZOffset` = 3.9 shifts the rows. The pattern's absolute phase still depends on how long the world has run, as in vanilla.
- **Clouds follow game time, not day time** (2026-11-01; reference run cloudtime: same camera, shots at game ticks 536111, 536158, 536205 with `time set 3000` and `time set 9000` in between): the white-pixel masks of the three shots agree to 98% after a sideways shift of 2-3 px per 47 ticks (0.03 blocks a tick at the cloud height predicts 2.9), in spite of the time of day jumping by 3000 and 6000 ticks. A day-time drift would have moved the pattern by 90 and 180 blocks.

## M5 redstone session: where I stopped (2026-10-31)
- Done and held to vanilla traces: hoppers, droppers, falling blocks, water, lava, fire (see commits up to 872262b).
- WIP, built but not tested or seen: falling-block entity. `FMcFallingBlockEntity` (McEntityManager.h) is mirrored from `FMcSimulation::GetFallingBlocks` by `AMcWorldActor::SyncFallingBlocks`; `UMcEntityModelComponent` draws a cube with the block's side texture (`BuildBlockMesh`), eased between ticks. Landing-unplaced blocks drop as items (`OnFallingBlockDropped`). A shot (`-McChat="/setblock 1 -30 3 minecraft:sand"`) was queued and stopped before it ran. Cube uses one texture on all faces (no separate top).
- Next, in order: check the shot and run McSim/McServer suites; pressure plates, tripwire, target blocks (entities; no reference verb yet); crops/melon edge rows; detector rain/thunder values; Bell hook (`git show 9f78f77 -- Source/McSim`, `OnBellRung`).

### Update (2026-10-31 night)
- Done: pressure plates and target blocks (8c544d5; target strength by hit spot is a guess, no vanilla verb shoots), crop growth fields (a90c261, rule matches vanilla within noise), rain/thunder ramp 0.01 a tick (708cd7a), tripwire and hooks (7aaf2fa, wiki numbers only, shears/disarm and placement rules not done).
- Open: daylight detector under weather matches vanilla within one 20-tick step except the thunder onset and the recovery after `weather clear`; script and vanilla log kept (tools/refscripts/rs_daylight_weather.txt), baked expect parked as tools/refscripts/expect_pending_rs_daylight_weather.txt. Falling-block entity and entity-fed plates/tripwire never seen in a game.
- Bell: not done here; 9f78f77 (vehicles branch) already rings bells from McRedstone with `OnBellRung`, so take it at merge.
- Build note: `E:\Epic Games\UE_5.8\Engine\Build\BatchFiles\Build.bat McClientEditor Win64 Development -Project=<worktree>\McClient.uproject` from PowerShell; it fails (exit 6) while an editor holds Live Coding.

### Update (2026-11-01): the M5 exit scene
- `tools/refscripts/gen_world.py` writes `rs_world.txt`: one world of 8 regions 16 blocks apart (repeater ring + delay-1/4 repeaters, comparator
  compare/subtract, 6-observer chain, sticky pistons on 1/2/3/16-tick pulses, plain pistons on 1-tick pulses at even/odd ticks + same-tick
  place/remove + 2-tick pulse, quasi-connectivity, daylight detectors high up, cane harvester with observer, kelp in a walled water column with an
  observer), 600 ticks plus 17 settling ticks, every block state logged per tick. Vanilla's log is `expect/rs_world.txt`; `McClient.McSim.RefScripts`
  holds our tick-by-tick states to it (760 changes, all equal).
- Rules fitted from it: observers are not redstone conductors; a piston told of a change while scheduled ticks run acts at the end of that same
  tick (was a tick late behind an observer pulse); a piston let go during its extension never places its head, and a sticky one's pushed block
  lands at once; kelp with kelp above is `kelp_plant`; sky-darkness constant 0.53 (any value 0.516 to 0.555 fits the 25 vanilla inverted-detector
  readings).
- Unexplained, left out of the baked file (`tools/refscripts/ignore/rs_world.txt`): the observer at x=33 fires 1 to 2 ticks into sampling in vanilla only.
- Not in the scene: hopper clock and dropper elevator (the headless replay has no container contents; hoppers and droppers live in McClient),
  random-tick growth of cane and kelp (done by command instead), a detector with a block over it (vanilla reads the light reaching its cell, 14 beside
  open sky; ours reads the global level). Falling-block entity shot still not taken.
- Main's note-block hook (`PlayNote`, 074d37b) to be wired in McRedstone.cpp at merge.
