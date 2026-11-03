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
    placed from the item on a rail or water. `/summon minecraft:minecart|boat` works.
    Carts pushing (vehicle_log7/8.txt, `McClient.McEntity.CartPushLog`; `log sents` there also prints an `mcref sentx:`
    line each tick with an item's count, a container entity's item total and a TNT cart's fuse, added to
    E:/mc-reference's Script.java, which is not a git repository, so this note is its only record): a cart touching another along its rail
    (within 0.2 aside) keeps 0.2 of its speed plus half the two together, shoved 0.05 apart (0.05/distance past a
    block); a furnace cart, fuelled or not, hands the other its whole speed and keeps 0.95. A cart takes a mob in
    only above 0.1 a tick; faster than that, it and a mob it can't take are shoved 0.05 apart (not a ridden cart),
    and a mob it can't take stops it dead. Chest and hopper carts keep 0.98 + (15 - comparator signal) * 0.001; a
    hopper cart picks up every tick, up to 1.97 above it; a TNT cart goes off 81 ticks after it is lit. There is no
    iron golem, so the logged golem scene is only replayed as a TNT cart into a cow.
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
    a tick wears it under. vehicle_log9.txt: honey leaves 0.4 after the move, like soul sand; frosted ice keeps 0.98,
    mud and soul soil stone's 0.6; empty cells under a hull are left out of the mean.
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
    or from flint and steel, then blows 81 ticks after it was lit (stronger when fast). A hopper cart picks up items
    over it every tick and locks while on a powered activator rail. Tests are in `McVehicleTest`; shot
    `Saved/Shots/carts.png` (`tools/scenes/carts.json`). Limits: the cargo is drawn as a plain box cut from the
    cart's texture; a hopper cart doesn't pull from containers above it (that is McBlockUse's); there is no
    primed TNT entity, so the cart keeps its own fuse.
    Looks fitted to vanilla's (vehicle_look.txt, ref_side_*, `tools/scenes/vehicles_look.json`): a cart at rest lies
    along its rail (it only turned when moving before); the furnace's and TNT's blocks stand 1.125 higher, the
    hopper's block 3.75 lower than those, and the chest 2.875 higher. Not done: vanilla's cart sits about a unit
    lower under its walls, and shows two dark marks along its foot (the floor's edge strip) and a soft shadow blob
    under it (nothing draws entity shadows yet).
    Boats under water and in bubble columns (vehicle_log10/11.txt, a pond seven deep, the world's lowest block is -64 so
    a pond below it is void): a boat with water over its hull sinks, `v' = 0.75 (v - 0.04 + 0.04 / 65)` (summoned at rest
    it falls 0.029538, 0.051692 ... to 0.118152 a tick, and stops on the floor; it used to rise); in a bubble column,
    after its move, each column block its hull overlaps (with water above the block) pushes it twice, each push up 0.06
    to 0.7 or down 0.03 to -0.3 (over magma at the floor it sits at -0.06, in two blocks 0.24 at once), the top block
    (air above) pushes nothing, but the 60th tick the hull has been in it throws the boat up at 0.6 (it flies 0.45, 0.41
    ... and drops back in at the surface less 0.3504, as a dropped boat does), then again 82 ticks after the first.
    `FMcBoatEntity::TickBubbleColumns` is its own rule, not main's `McPhysics::BubbleColumnPush` (m1/blocks-and-chunks,
    not merged here): same per-block push, twice, and a boat-only throw. Not fitted: a boat in a downward column's top
    block, mobs and players riding, a column starting under a moving boat.
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
    block at 0.556 of its size, so the two rules jump at a block. vehicle_log9.txt (cow, sheep, spider and creeper at spin 0):
    0.62 still fits the creeper and zombie, not 0.45 or 0.5; the mob had been drawn facing north at spin 0, the back of
    the cow's and sheep's heads to the south where vanilla's show their faces, and is turned round. Left: vanilla's
    cow and sheep faces look upright where ours are tipped nose up 40, and its spider's legs reach less far.
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
