# High-End Texture Pack Guide

A brief for a person or an AI building a pack that can compete with Optimum Realism and Prime's HD Textures. It covers two targets: **Minecraft Java 26.3** and **our Unreal port**.

**How to read the claims**
- **[V]** was checked on 2026-11-04 against a source listed at the end, or against this repository's code.
- **[K]** is working knowledge that wasn't checked. Test it before relying on it.
- **[J]** is a judgement call. Change it if testing disagrees.

**Assumptions about the names:** "Optimum" is taken to mean Optimum Realism. "Prime" is taken to mean Prime's HD Textures by primemods. A separate pack called Prime Resolution exists and isn't covered here.

---

## 1. What you're trying to beat

| | Optimum Realism | Prime's HD Textures |
|---|---|---|
| Style | Photorealistic | Vanilla re-imagined in HD, tagged "Vanilla Like" |
| Resolutions [V] | 64x free; 128x to 512x for members | Free version is 32x; the author's Patreon lists options up to 512x |
| PBR [V] | "690+ PBR textures", LabPBR specular reported | "Complete PBR support" on the 32x page |
| Parallax [V] | POM and depth-write POM | Not mentioned on the page |
| Emissive [V] | Yes | Not mentioned on the page |
| Connected textures [V] | Yes | Not mentioned on the page |
| Custom entity models [V] | Yes | Not mentioned; 3D bars used with permission from another pack |
| Shaders named [V] | Iris and Fabric, or OptiFine; one guide names Nori Shaders | BSL recommended, with Advanced Materials on |
| Licence [V] | All Rights Reserved | All Rights Reserved |

**Neither pack's production method is public, as far as this research found.** Don't state how they were made. Judge them by opening them in the game, side by side, on the same scenes.

**Where a new pack can win [J]**
- **Coverage:** every block, item, entity and GUI element in one style. Realistic packs often leave odd blocks behind, and those stand out.
- **Style consistency:** one lighting, scale and wear standard across every material.
- **Readability:** blocks still recognisable at 10 and 50 blocks away, not just in close-up screenshots.
- **Correct PBR values:** many packs look good under one shader only. Measured values hold up under all of them.
- **Tiling:** no visible grid across a large wall or field. This is the most common failure in photo-based packs.
- **Update speed:** new versions and new blocks supported quickly.

Pick **one direction** before starting: photoreal (against Optimum) or stylised vanilla HD (against Prime). A pack that mixes them loses to both [J].

---

## 2. Legal rules, before any art

- **Don't copy, trace, upscale or extract from Optimum, Prime or any other pack.** Both are All Rights Reserved [V]. Prime's own page credits other authors for what it borrowed, with permission [V]. Copying needs that same permission.
- **Vanilla textures:** the common rule of thumb is that a pack can change them, but shouldn't redistribute them unchanged. That comes from low-quality sources. **Read Mojang's own Usage Guidelines and EULA before releasing or charging.** They weren't retrieved for this guide, and paid tiers especially need checking.
- **Source material:** use your own photos, your own procedural work, or libraries whose licence allows redistribution. Poly Haven and ambientCG are commonly used CC0 sources [K]. Keep a record of source, licence and author for every asset.
- **AI-generated art:** check the tool's terms and keep the prompts and outputs. Whether the output can be protected or freely used varies by tool and country.
- **Don't imitate names or branding** of existing packs.

**For this repository:** never commit pack art or third-party packs, and never push `cache/`, Mojang files or `FaithfulPBR_1024_1.1p.zip`. Keep the pack in its own repository. `resourcepacks/` is only where the game reads it.

---

## 3. Scope: what has to be made

Counts from the local 26.3 assets [V]:

| Folder under `textures/` | PNG files | `.mcmeta` files | Notes |
|---|---|---|---|
| `block` | 1300 | 105 | Main job. Needs `_n` and `_s` maps too |
| `item` | 831 | 0 | Keep resolution modest (section 7) |
| `entity` | 743 | 16 | UV layout is fixed unless you replace the model |
| `gui` | 563 | 53 | Nine-slice data must match the new sizes |
| `particle` | 300 | 1 | |
| `painting` | 52 | 0 | |
| `map` | 42 | 0 | |
| `mob_effect` | 40 | 0 | |
| `trims` | 40 | 36 | Palettes and permutations, read the atlas rules |
| `environment` | 14 | 0 | Sun, moon, clouds, weather |
| `misc` | 13 | 7 | |
| `font` | 5 | 0 | |
| `colormap` | 3 | 0 | 256x256, grass, foliage, dry foliage [V] |

That's roughly 4,000 pictures before PBR maps, and about 6,500 once each block picture has `_n` and `_s`. The total counts variants and overlays, so plan by material family, not by file [J].

---

## 4. File format

### Layout [V]
```
my-pack/
  pack.mcmeta
  pack.png
  assets/minecraft/
    textures/block/stone.png
    textures/block/stone_n.png
    textures/block/stone_s.png
    textures/block/water_still.png.mcmeta
    blockstates/  models/  items/  equipment/  atlases/  font/  sounds/  lang/
    optifine/ctm/...            (Java game only)
```

### `pack.mcmeta` [V]
26.3's resource format is **97.1**. Since 25w31a, packs use `min_format` and `max_format` in place of `pack_format`.
```json
{
  "pack": {
    "description": "My Pack 256x",
    "min_format": 97,
    "max_format": 97
  }
}
```
Our loader also reads `pack_format`, `supported_formats`, `filter.block` and `overlays`. The format is numbered with minor versions now, so check that the game lists the pack as compatible, and test both whole numbers and `[97, 1]` [K].

### Animation [V]
- `name.png.mcmeta` holds `animation` with `frametime`, `frames`, `interpolate`, `width`, `height`.
- Frames are the same size, and the image divides evenly by the frame size. Otherwise you get the missing texture.
- High-resolution animations cost a lot of memory. A 512x water with 32 frames is 16 MP of source before mips, so use fewer frames with `interpolate` where it holds up [J].

### LabPBR 1.3: the material standard [V]

**Normal map, `<name>_n.png`**

| Channel | Meaning |
|---|---|
| R | X: 0 is left, 255 is right |
| G | Y: 0 is up, 255 is down (DirectX, "Y-") |
| B | Ambient occlusion, linear: 0 fully occluded, 255 none |
| A | Height, linear: 0 is 25% deep, 255 is surface. **Use 1 as the minimum**, because 0 can break POM in some shaders |

Z isn't stored. It's rebuilt from `sqrt(1 - dot(xy, xy))`.

**Specular map, `<name>_s.png`**

| Channel | Range | Meaning |
|---|---|---|
| R | 0-255 | Perceptual smoothness. `roughness = (1 - smoothness)^2` |
| G | 0-229 | F0, linear (229 is about 90%) |
| G | 230-237 | Predefined metals, below |
| G | 255 | Custom metal, albedo used as F0 |
| B | 0-64 | Porosity. Sand 64, wool 38, wood 12, metal 0 |
| B | 65-255 | Subsurface scattering, 65 is 0%, 255 is 100% |
| A | 0-254 | Emission, 254 is 100%. **255 means no emission**, so an unused alpha must be 255 |

Only R and G are required.

**Predefined metals:** 230 iron, 231 gold, 232 aluminium, 233 chrome, 234 copper, 235 lead, 236 platinum, 237 silver. Values 238-254 have no table entry, and some shaders treat all of 230-255 as 255.

**Starting values [K, check against measured material tables]**

| Material | G (F0) | R (smoothness) | Notes |
|---|---|---|---|
| Water | 5 | Near 255 | Usually handled by the shader |
| Most stone, dirt, wood, cloth | 8-12 | 40-120 | Vary the smoothness across the surface |
| Glass | 10 | 230-255 | |
| Polished stone, glazed terracotta | 10-13 | 160-220 | |
| Diamond | About 43 | 240+ | Facets carry the look |
| Iron, gold, copper | 230, 231, 234 | 90-200 | Add scratches and oxidation in R |
| Oxidised copper patina | 10-20 | 40-90 | Not a metal, so don't mark it as one |

---

## 5. Art rules that decide the quality

**Albedo**
- **No baked light, shadow or highlights** in PBR albedo. Shaders add them, and baked ones show as wrong-way light.
- **Keep brightness in a consistent range** across all materials [J]. Fresh snow shouldn't be near pure white, and charcoal shouldn't be near pure black.
- **Tinted textures stay greyscale:** grass tops, leaves, water, and overlay parts. The game multiplies them by the colormap. Check every biome.
- **Metal albedo holds the metal's colour.** Our port uses albedo as the metal colour for every metal value, so a predefined metal needs correct albedo too.

**The no-shader problem [J]**
Many players never run shaders, and flat PBR albedo looks dull without them. Ship two variants:
- **Shader build:** clean albedo for use with PBR maps.
- **No-shader build:** the same art with mild baked occlusion and shape in the albedo.

**Tiling and repetition**
- Every full-block texture must tile on **all four sides**, including `_n` and `_s`, height especially. Our port's POM ray can step across the edge, and the arrays wrap [V].
- A block is one metre, and the pattern shows immediately across 20 blocks. Remove anything that catches the eye: one large crack, one dark stone, one knot.
- **Test on a 32x32 wall, from 5, 20 and 60 blocks away, before calling a texture done.**
- In the Java game, reduce repetition with CTM `random`, `repeat` and `overlay`. **Our port doesn't read CTM** [V], so each base texture has to survive alone.

**Scale**
- Fix a real-world scale: 1 block = 1 m. A brick, plank or tile has to match that across the pack.
- Check against the models: doors, trapdoors, stairs and slabs **crop** textures on the 16-unit grid. A plank seam must land on a whole-pixel boundary at every resolution, and the crop must still read well.
- Keep each block's silhouette and main colour, so players can recognise it from a distance and in a dark cave. Ores need clearly readable deposits, and redstone components need their state visible.

**Normal and height**
- Build height first, then derive normals from it, so POM and lighting agree.
- Keep relief **large and soft**: step artefacts come from sharp, deep height. Our POM uses just 14 steps [V].
- Store real depth in height, without normalising each texture to the full range, or a slightly bumpy wall ends up as deep as a cobblestone one [J].
- Bake a little micro-detail into normals only, not height.

**Roughness**
- This does more for realism than extra resolution [J]. Vary it with wear, wetness, dust and polish.
- A flat R value looks like plastic.

**Emission**
- Only light-giving parts. The ore or lava cracks glow, not the whole block.
- Emitted colour comes from albedo [V], so those pixels need the correct, saturated colour.

**Alpha and mips**
- Cutout textures (leaves, plants, glass panes): keep the colour under transparent pixels filled in, or dark or bright halos appear in the mips.
- Stop tools and PNG optimisers from zeroing RGB under alpha. This matters most in `_n` and `_s`, where alpha is data. For example, check that an optimiser's alpha cleaning is off [K].
- Leaf and plant coverage shrinks in mips, so test them at a distance. Trees shouldn't turn thin far away.
- Save `_n` and `_s` as linear data, 8 bits per channel, no colour profile, no premultiplied alpha.
- Use **power-of-two** sizes only.

---

## 6. Production pipeline

Recommended order [J]:

1. **Set the rules first.** Scale, brightness range, wear level, light direction for any baked variant, and naming.
2. **Build the toolchain before the art.** A script should take a master file and output every resolution tier, pack the channels, validate them and zip the result. Doing this by hand across 1,300 blocks doesn't scale.
3. **Make masters at 2048x if practical,** so lower tiers come from downsampling. Author in Substance Designer or Painter, Blender bakes, photogrammetry, or your own photos [K].
4. **Downsample each map correctly.** Albedo in linear light, normals renormalised after filtering, roughness adjusted so distant surfaces don't turn glossy [K].
5. **Do the hero set first:** stone, deepslate, dirt, grass block, sand, oak log and planks, cobblestone, iron block, water, leaves, glass, one ore. If these don't beat the rivals side by side, more textures won't help.
6. **Do families next,** using shared base materials so variants match: all 11+ woods, all stone types, wool, concrete, terracotta, copper oxidation stages, ores per host stone.
7. **Then:** animated blocks, tile entities, plants, items, entities, GUI, particles, environment.
8. **Then Java-only extras:** CTM, random and emissive entities, custom models.

**Validation script.** Fail the build if any of these occur:
- Non-power-of-two picture, or `_n` or `_s` size differing from the albedo
- Normal XY length above 1, or height alpha of 0
- Specular G from 238 to 254 (outside the defined metals, unless deliberately 255)
- Specular alpha of 0 where there should be none. Remember that 255 is "off" and 0 means 0% emission, so check what your shader does [K]
- Animated `.mcmeta` where the height doesn't divide by the frame size
- Texture missing against the current version's file list, or a file that the version doesn't use
- A seam score over the limit on any tiling map (compare opposite edges)
- A source or licence record missing

---

## 7. Target: Minecraft Java 26.3

**What players need**
- **Vanilla alone** doesn't use `_n`, `_s`, POM or emission through these maps [K].
- **Iris** (usually with Sodium) or OptiFine, plus a **LabPBR-capable shader**, for PBR [V for Iris and OptiFine as Optimum's supported setups]. BSL and Nori are named by the rival packs [V]. Also test Complementary, Bliss and Photon [K].
- **Check that each mod has a 26.3 build** before promising support. OptiFine especially tends to lag [K].

**Mods that add pack features**

| Feature | Mod on Fabric | Format |
|---|---|---|
| Connected textures | Continuity [V] | OptiFine/MCPatcher only, not Chisel. Client-side, needs Fabric API |
| Random and emissive mobs | Entity Texture Features [V] | OptiFine format |
| Custom entity models | Entity Model Features [V] | OptiFine CEM, needs ETF. Not compatible with OptiFine |
| Colour and block appearance tweaks | Polytone [K] | Its own format, unchecked |

The available versions of these sources were mixed, so check current builds.

**CTM reference [V]:** files in `assets/minecraft/optifine/ctm/**/*.properties`.
```properties
matchBlocks=stone
method=random
tiles=0-3
weights=8 4 2 1
symmetry=opposite
```
| Method | Tiles |
|---|---|
| `ctm` | 47 (the 47th is unused) |
| `ctm_compact` | 5 |
| `horizontal`, `vertical` | 4 |
| `horizontal+vertical`, `vertical+horizontal` | 7 |
| `top`, `fixed` | 1 |
| `random` | Any |
| `repeat` | `width` x `height` |
| `overlay` | 17 |

Other useful properties are `connect`, `faces`, `biomes` and `heights`. Rules run alphabetically and the first match wins, with tile rules checked before block rules.

**Do these at release [K]**
- **Atlas limits:** the block atlas has to fit the GPU's maximum texture size. At 16384, that fits 1024 tiles of 512x or 256 tiles of 1024x, which is fewer than 26.3's 1300 block pictures. **Test every tier on real hardware before listing it,** including 8 GB and 4 GB cards.
- **Items:** the game builds a held item's 3D shape per pixel, so high-resolution items cost a lot. Keep them to 64x or 128x and upscale only where worthwhile.
- **Depth-write POM** is what Optimum markets [V]. It comes from the shader, so state which shaders support it.
- **Non-square animation grids, trims and `equipment/`:** test them directly. They're easy to miss.

---

## 8. Target: our Unreal port

Checked against this repository's `m1/blocks-and-chunks` on 2026-11-04 [V]. The resource pack session is still working, so re-check before relying on any "missing" item.

**How to run a pack**
```
-McPacks=mypack.zip,other     names from resourcepacks/, last listed on top
-McHd                         HD Textures on, which also turns pack PBR on
-McPackPbr                    keep _n and _s without -McHd
-McPackMaxSize=<px>           default 512, largest block texture side
-McPackBudgetMB=<MB>          default 1536
```
The in-game picker is under Options, Resource Packs, and shows packs from `resourcepacks/`. **Vanilla Mode ignores PBR maps, so test in Unreal Mode with HD Textures on.**

**How maps are read** (`tools/make_materials.py`)

| Input | What the port does |
|---|---|
| Normal R, G | `xy * 2 - 1`, Z rebuilt, then `xy.x * T + xy.y * B` (from `13d6b39`) |
| AO (normal B) | `lerp(1, AO, 0.8)` |
| Height (normal A) | POM on **opaque blocks only**: 14 steps, 0.12 depth |
| Smoothness (spec R) | `roughness = max((1 - R)^2, 0.04)` |
| F0 (spec G) | Specular `G / 0.08`, clamped to 1. So F0 above about 8% saturates, and diamond reads as ordinary specular |
| Metal | **Any G of 230 or more** is metal, with albedo as its colour. The predefined N and K values aren't used |
| Emission (spec A) | `albedo * (A / 254) * 4`; 255 is off |
| Porosity, subsurface (spec B) | **Not read** |

**Differences from the shader target that change how you author**
- **Depth:** LabPBR's height range means 25% depth; ours is 0.12. The same map looks about half as deep here. Don't author to the port's depth at the expense of the Java game. Change `POM_DEPTH` to 0.25 instead, if a side-by-side says so.
- **Mips and size:** every block layer shares **one size**. A pack that brings 1024x textures is reduced to fit the budget.
- **Vanilla layers:** a layer your pack doesn't replace gets 16 px art repeated, with generated maps. A partly finished pack looks uneven.
- **Animated `_n` and `_s`:** first frame only. The albedo animates, the maps don't.
- **Missing map:** generated from the picture, without added detail. Shipping both maps gives far better results.

**Normal direction: fixed in commit `13d6b39`, on the Unreal mode branch until merged.** The port had Y inverted against LabPBR's DirectX convention, confirmed by shots of a raised test square, and it now reads DirectX on both axes. Generated maps were changed to match. Before that commit, pack maps were also skipped for textures cut into 16 px tiles, including the test's folder pack; that's fixed too. Zip packs use the same path, but no zip or stock pack has been shot since. Always author to the DirectX spec.

**Editing a folder pack:** delete `Saved/HdCache/Blocks.bin` after changing its pictures, because the HD cache doesn't notice.

**Memory: arrays are uncompressed BGRA8**
```
bytes per array = side^2 x 4 x 4/3 x layers
```
Using 1300 layers as the upper case, since only textures the baked models name become layers:

| Side | One array | HD total (3 arrays) |
|---|---|---|
| 64 | 28 MB | 85 MB |
| 128 | 114 MB | 341 MB |
| 256 | 455 MB | 1.4 GB |
| 512 | 1.8 GB | 5.5 GB |
| 1024 | 7.3 GB | 22 GB |

HD arrays share the budget in thirds. At the default budget, Faithful PBR 1024 ran its HD arrays at 256 px. **Treat 256x as the port's main tier.** 512x needs `-McPackBudgetMB` near 5500 and a card with room to spare. Texture compression in the port would change this table a lot, and is worth asking for.

**Other assets:** `McHand` shrinks a held item's picture to 64 before making its mesh, so item art above 64 only helps the HUD.

**What the port reads from a pack**
- Block, item, entity, environment, particle, painting, GUI and map pictures
- Colormaps and `.png.mcmeta` animation, with `interpolate`
- GUI nine-slice, tile and stretch scaling
- Font sheets at any multiple of 128, plus font providers
- `atlases/*.json` sources (unstitch, paletted permutations, filter)
- Blockstates and block models
- Item models and `items/*.json`, as far as the game's own conditions go
- Sounds, `sounds.json`, `lang`, splashes
- `overlays`, `filter.block`

**What it doesn't read**
- `optifine/` and `mcpatcher/` data: **no CTM, no random mobs, no emissive suffix, no CEM**
- `_n` and `_s` for **anything except blocks**. Only `McBlockTextures.cpp` loads them, so mobs and items use generated maps
- `shaders/` core and post effects, because Unreal draws the world
- `models/entity`, and `equipment/` for armour and trims
- `_e` and `_r` extra maps
- Pack changes in an open world, which apply at the next start

**Testing notes**
- The port's look-test scene is `-McScene=unreal`, with `-McTime=18000` for torch light. Use `-McShotDelay=12`.
- Pack folders aren't synced to the 1070. Copy them to `C:/mc-runs/<worktree>/resourcepacks/`.
- Screenshots go through `bash /e/mc-remote/remote.sh shot`. Build only with `remote.sh build`.

---

## 9. One pack for both targets

**The goal is one zip per resolution, and each zip drops into either game unchanged.** 256x is the main release. The same zip won't look identical in both, because lighting and supported features differ (sections 7 and 8).

```
source/        masters, kept outside the release
build/
  64x.zip  128x.zip  256x.zip  512x.zip
```
Each zip holds albedo, `_n`, `_s`, models, `.mcmeta` and the Java extras such as `optifine/ctm`. This works because each game skips what it can't use. Don't make a separate port build unless a test below proves it's needed [J].

**Why not one zip with everything:** the port shrinks anything over its budget, so a 512x zip costs download size and loading time there without adding detail at default settings.

**Single-zip test, required for every release.** Load the same file, byte for byte, in both games and confirm:
1. **Real Minecraft 26.3:** the pack is listed as compatible, loads with no pack errors in `latest.log`, and shows PBR under a LabPBR shader through Iris.
2. **Our port:** drop the zip in `resourcepacks/`, select it in the picker, and confirm it isn't marked incompatible. Run with HD Textures on in Unreal Mode, and confirm the log's "with PBR maps" count is above 0.
3. **Compare** a raised test tile lit from a known side in both. If the bumps disagree, the port is wrong unless proven otherwise.

**Known risks for this test (2026-11-04):**
- Nobody has yet tested a single `pack.mcmeta` in both games. The real format is 97.1 and the port's picker checks against 97, so test the format fields first.
- The port's normal and map-loading fixes are in commit `13d6b39`. Test on a build that includes it. They were tested with a folder pack, so the first zip test is still needed.

**Things that help both**
- Tile every base texture well, so nothing depends on CTM
- Keep albedo free of light, and metal albedo correctly coloured
- Fill every LabPBR channel, even though the port doesn't read porosity or subsurface today
- Prefer F0 under 8% except where it matters; the port flattens the rest

---

## 10. Acceptance tests

Compare side by side with both rival packs, using the same world, camera, time and shader, at each tier.

**Scenes**
1. Noon plains with a large grass and dirt area, from low and high up, at 60+ blocks
2. 32x32 walls of stone, brick, planks and deepslate, for repetition
3. Forest at sunset: leaves, tree thinning, tint per biome (jungle, swamp, snow, badlands)
4. Torchlit cave with ores, lava and water: readability and emission
5. Ocean and river at noon and at night
6. Nether (netherrack, basalt, glowstone) and End
7. A built house: stairs, slabs, doors, trapdoors, glass, cropped textures
8. Iron, gold and copper in all oxidation stages, in strong light
9. Rain: wet look, and any puddles in the port
10. A grazing-angle walk along a wall, for POM stepping and edge swimming
11. Mobs, armour, held items, GUI and the inventory at GUI scale 2

**Pass criteria [J]**
- No visible grid on a 32x32 wall at 20 blocks
- Every ore and every redstone state is identifiable at 10 blocks in a dim cave
- A new viewer can name 20 random blocks from a screenshot
- No halos, swimming or stair-stepping under normal movement
- 60 fps at the advertised tier on the advertised GPU, measured over several runs
- Looks correct under at least three shaders, and with none
- No missing textures, no log warnings about the pack, in either target
- A blind comparison prefers it to the rivals in most scenes

---

## 11. Instructions for an AI agent

1. **Don't claim a result you haven't seen.** Render in both targets and look at the pictures. A passing validator doesn't show that art is good.
2. **Don't use memory for format details.** Check the sources below and the current version's files. Formats change between versions.
3. **Never use another pack's files** as inputs, including for AI image generation or as style references to copy.
4. **Start with the toolchain and the hero set,** then stop for human review before scaling up. Taste needs a human in the loop.
5. **Generate textures in bulk only from shared base materials,** so they match. Tile check everything.
6. **Height comes first, then normals.** Don't derive normals from albedo for final art; that's what makes "fake depth" packs look bad.
7. **Keep a record** for each texture: source, licence, tool, and date.
8. **Report honestly:** coverage counts, failed checks, untested shaders and GPUs.
9. **In this repository,** follow the clean-room rule: use no Mojang code or decompiled sources, and keep pack art and vanilla files out of commits and pushes.

---

## 12. Sources

**Pack facts**
- [Optimum Realism on CurseForge](https://www.curseforge.com/minecraft/texture-packs/optimum-realism)
- [Optimum Realism on Modrinth](https://modrinth.com/project/optimum-realism)
- [Optimum Realism installation](https://sites.google.com/view/optimumrealism/installation)
- [Optimum Realism on resourcepack.net](https://resourcepack.net/optimum-realism-resource-pack/)
- [Prime's HD Textures on Modrinth](https://modrinth.com/project/primes-hd-textures)
- [primemods on Patreon](https://patreon.com/primemods)

**Formats**
- [LabPBR Material Standard](https://shaderlabs.org/wiki/LabPBR_Material_Standard)
- [LabPBR Implementation Requirements](https://shaderlabs.org/wiki/LabPBR_Implementation_Requirements)
- [Resource pack, Minecraft Wiki](https://minecraft.wiki/w/Resource_pack)
- [Pack format, Minecraft Wiki](https://minecraft.wiki/w/Pack_format)
- [OptiFine CTM documentation](https://optifine.readthedocs.io/ctm.html)

**Mods**
- [Continuity](https://github.com/harvelsx/continuity)
- [Entity Texture Features](https://github.com/Traben-0/Entity_Texture_Features)
- [Entity Model Features](https://www.curseforge.com/minecraft/mc-mods/entity-model-features)

**Licensing (weak sources: read Mojang's own guidelines instead)**
- [MCprep issue on the EULA and vanilla assets](https://github.com/Moo-Ack-Productions/MCprep/issues/533)

**This repository**
- `PLAN.md`, "Resource packs" section and feature checklist
- `tools/make_materials.py`, HD material code
- `Source/McAssets/Private/McBlockTextures.cpp`, `McHdBlocks.cpp`
- `Source/McCore/Private/McResourcePacks.cpp`
