# Port progress tracker

A repeatable answer to "how close is mc-unreal to a 1:1 copy of Minecraft Java 26.3?", and a small widget that
shows it live on your own website.

- `audit.py` scores every vanilla content id and every game system, and writes `progress.json`.
- The widget fetches `progress.json` and draws it. It re-fetches on a timer, so when the file changes the page does too.
- `update.sh` reruns the audit and copies the file to a folder your website serves.

The output holds ids, counts and short rule texts only. It contains no game assets and no text from the jar. The
audit never opens the Minecraft jar. It reads only the reports and data files that `tools/fetch_cache.py` already
put in `cache/`.

## The numbers

### Checks

A **check** is one aspect of one vanilla id, for example a block's drops, a mob's AI, or one branch of a command's
syntax. Each check is in one of three states:

| State | Value | Meaning |
|---|---|---|
| **absent** | 0 | The audit found nothing for it. Unknown also counts as absent. |
| **present** | 0.5 | The code that should do it names the id, or loads the data for it. |
| **verified** | 1 | A passing test, a vanilla comparison log or a PLAN.md vanilla comparison names the id for that aspect. |

The scores build up in three steps:

1. An **id's score** is the mean of the checks that apply to it.
2. A **category's score** is the mean of its ids.
3. The **headline** is the weighted mean of every category and every system (see below).

The headline comes in three figures:

| Figure | What it counts |
|---|---|
| **1:1** | `(verified + 0.5 × present) / checks`, weighted. This is the main number. |
| **Strict** | Verified checks only. The floor: what is proven. |
| **Lenient** | Present or verified checks. The ceiling: what the code at least attempts. |

`left` in `progress.json` gives what remains as counts: checks absent, checks present but not verified, ids not done
(an id is done when every check is verified), and systems missing or not verified.

### Aspects and how each is detected

"Named in X" means a string literal in those files (game code only, never `Tests/`) is the id, as `"creeper"` or
`"minecraft:creeper"`. It also counts if a literal names a tag that contains the id, or if the id is built from parts
the files name (see [Ids built at runtime](#ids-built-at-runtime)).

**Blocks** (`blocks.json` and the registry):

| Aspect | Applies to | Present when |
|---|---|---|
| Model or texture | every block | `blockstates/<id>.json` or `textures/block/<id>.png` is in the client assets, and `McBlockRegistry.cpp` loads blocks from `blocks.json` |
| States and placement | blocks with properties | `McBlockPlacement.cpp` names the block, or every property name is named in placement or block-behaviour code |
| Behaviour | blocks with a non-shape property (`age`, `power`, `lit`, `open`…) or a block entity | block-behaviour code names it: McSim, McWorld, block use and break, containers, TNT, spawners |
| Drops | blocks with `loot_table/blocks/<id>.json` | the block-break code reads block loot tables, and the DataCoverage test did not reject the table |
| Sounds | every block but air | the port's block sound rules (the table in `McSound.cpp`, which the audit runs in Python) give a group. It is absent when they fall back to stone while the sound registry has a `block.<part>.*` group that fits the id, as `block.wool.*` fits `white_wool`. |

**Items:**

| Aspect | Applies to | Present when |
|---|---|---|
| Model | every item | `items/<id>.json` or `textures/item/<id>.png` is in the client assets, and the item registry loads |
| Use | items with an active component in the components report (food, consumable, tool, weapon, equippable, glider…), or that player, item, entity, client or server code names | that code names the item, or names every active component it has |
| How it is obtained | items that some recipe result, loot table entry or villager trade gives | the port loads that kind of data |

**Entities:**

| Aspect | Applies to | Present when |
|---|---|---|
| Model and animation | every entity type | the entity renderer (McRender) names it |
| AI or behaviour | every entity type | entity code (McEntity, the server's entity host) names it |
| Drops | entities with `loot_table/entities/<id>.json` | loot tables load **and** the entity's AI is present (a mob that never exists drops nothing) |
| Sounds | entities with `entity.<id>.*` sound events | the mob sound table in `McSound.cpp` lists it, or audio code names it |
| Spawning | entities in a biome's `natural_mob_spawns` or a structure's `spawn_overrides` | the port reads `natural_mob_spawns` and the entity's AI is present, or spawning code names it |

**Commands:** every executable branch in `commands.json` is one check (1,494 in 26.3). A branch is present if
`McCommands` names the command and every literal word on the branch (`time` `set`, `gamerule` …). It is verified if
a passing test runs a command line containing those words in order, such as `"/time set night"`. A command's score is
the mean of its branches.

**Other registries** (block entities, mob effects, potions, particles, menus, statistics, attributes, villager
professions, game events) have one check: named in game source.

**Data folders** (advancements, recipes, loot tables, enchantments, biomes, structures, damage types, paintings,
music discs) have one check:

- Present if the source reads that folder at runtime. The audit looks for the loader call, for example
  `McCache::Data(TEXT("worldgen/biome/*.json"))`.
- Recipes, structures and advancements must also have their `type`, or every criterion `trigger`, named in source.
  A recipe of a serializer the port never names stays absent.
- Folders without a loader fall back to "named in source".
- Loot tables and recipes that the DataCoverage test lists as not loading are absent.

Each category and aspect in `progress.json` carries its rule as text, and the widget shows it in the drill-down.

### Verified: where the evidence comes from

1. **The test log.**
   - Which log: `--test-log`, or by default the one remote.sh writes, `<main checkout>/Saved/Remote/run_tests.log`.
   - Which commit it ran: `--test-log-commit`, else a `run_tests.log.commit` file next to the log holding the
     hash, else **inferred** as the newest commit of the audited branch older than the log file. `progress.json` says
     which way it was found (`evidence.test_log.commit_source`).
   - Which tests count: a test file counts if every test it declares passed and the file is unchanged between the
     log's commit and the audited one. Its ids then verify the aspects that its module can test. For example,
     McAudio and McCore tests verify sounds; McEntity and McServer tests verify AI; McAssets, McRender and McUI tests
     verify models. `VERIFY_MODULES` in `audit.py` holds the full table.
   - A passing test whose last name segment, in snake_case, is an id also counts.
2. **Vanilla comparison logs.** `McClient.McSim.RefScripts` holds the port to `tools/refscripts/expect/rs_*.txt`,
   and `McClient.McSim.RefScriptsLogistics` holds it to `expect/lg_*.txt`. If those tests passed, every block in the
   logs' state columns has its states and behaviour verified.
3. **PLAN.md.** A sentence counts if it cites a baked ref log (`rs_*`/`lg_*` with an expect file) or says "matches
   vanilla", "all equal", "held to the reference", "32/32" and the like. It does not count if it also says unbuilt,
   untested, parked, guess, open or unexplained.
   - The ids it cites come from `backticks`, `minecraft:` ids, snake_case words and plain names ("blast furnace").
     A short stoplist skips ordinary words like "light" and "stone".
   - A sentence about shots verifies the model aspect. Any other verifies behaviour (blocks) or AI (entities).

### Systems

`systems.json` lists what id counts cannot see: rendering, lighting, fluids, redstone, worldgen, saves, multiplayer,
UI screens, options, audio, statistics, the recipe book and so on.

- Each system has a status (`absent`, `unknown`, `present` or `verified`) and an evidence line.
- The list is kept by hand. It was seeded from PLAN.md and the test names on 2026-11-11.
- A system marked `verified` must list `verify_tests`. The audit counts it as present unless all of those tests
  passed in a test log matched to the audited commit.
- Change a status only with a reason in `evidence`.

### Weights

`weights.json` gives each category and system a weight. A key that is left out weighs 1.

The defaults are a judgement call about how much of playing the game each part is. For example: blocks 12,
entities 12, items 9, redstone 4, paintings 0.5. Systems make up about half of the total weight (`content_weight_share_pct` gives the content share). Edit the weights
freely; the history will show the jump on the next run.

### Limits

Read the figures with these in mind.

- **Present is not done.** Present only means the right code mentions the id, or loads its data. It does not say
  the id behaves exactly as in vanilla. That is why present scores half.
- **Literals are a proxy.** A short id such as `"say"` may appear for another reason. An id built in a way the audit
  cannot see reads as absent. Treat absent as "not found", not "certainly missing".
- **Applicability is a heuristic.** Vanilla behaviour lives in Java classes that the clean-room rule forbids reading.
  So "has behaviour" is guessed from data: non-shape properties, block entities, active components, sound events,
  spawn lists. Some blocks with behaviour but only shape properties are not scored on it.
- **Verified means a test passed, not always that it matches vanilla.** Most automation tests check the port against
  values a session measured or wrote down. Only the ref-log rule replays a vanilla run tick for tick. An inferred
  test-log commit can also be wrong if the log came from a dirty checkout.
- **The weights and systems are opinions.** The id checks are mechanical; these two files are not. Quote strict and
  lenient beside the 1:1 figure.
- **Not counted:** sound events, tags, worldgen features, dimension types and more. You can add categories in
  `audit.py`.

### Ids built at runtime

Some ids are built in code. For example, boats are `Wood + TEXT("_boat")`, where the wood name comes from data.
The audit therefore also counts an id as named when either of these holds:

- A suffix literal such as `"_boat"` or `"_chest_boat"` that 3 or more ids of that kind end with is named in the
  same files. The same applies to a prefix literal such as `"stripped_"`.
- A format literal such as `"%s_boat"` matches the id.

Tag literals count too: `TEXT("minecraft:saplings")` names every sapling.

## Files

| File | What it is |
|---|---|
| `audit.py` | The audit. Python 3, standard library only. Run `python tools/tracker/audit.py --help`. |
| `systems.json` | The hand-kept systems list. |
| `weights.json` | The weights of the headline. |
| `progress.json` | The audit output, committed as a sample. `update.sh` writes elsewhere by default. |
| `update.sh` | Reruns the audit and copies the output (and, if you ask, the widget) to a folder. |
| `widget/tracker.js` | The widget. Plain JavaScript, no build step, no CDN. |
| `widget/tracker.css` | The widget's stylesheet. `tracker.js` loads it from the same folder. |
| `widget/index.html` | A page holding only the widget, for embedding with an `<iframe>`. |
| `widget/demo.html` | Both embed methods on one page, for trying it locally. |
| `out/` | The default output folder of `update.sh`. Git ignores it. |

### progress.json, briefly

```json
{
  "schema": 2,
  "generated": "2026-11-11T21:00:00+00:00",
  "commit": { "hash": "...", "branch": "m1/blocks-and-chunks", "date": "...", "subject": "..." },
  "headline": { "one_to_one_pct": 51.4, "strict_pct": 14.4, "lenient_pct": 88.4, "formula": "...", "content_weight_share_pct": 50.7 },
  "left": { "checks": 17730, "absent": 3272, "present_not_verified": 12856, "verified": 1602,
            "ids": 9322, "ids_done": 452, "ids_untouched": 2131, "systems": 32, "systems_verified": 2, "systems_absent_or_unknown": 2 },
  "evidence": { "test_log": { "used": true, "commit": "0ce58805b2", "commit_source": "inferred: ...", "tests_passed": 267 },
                "ref_logs": [ ... ], "plan_citations": { "sentences": 12, "model_ids": 1, "behaviour_ids": 25 } },
  "categories": [
    { "key": "block", "label": "Blocks", "group": "content", "ids": 1286, "weight": 12,
      "score": 0.528, "strict": 0.075, "lenient": 0.98, "ids_done": 14, "ids_untouched": 0,
      "checks": { "applies": 5009, "absent": 107, "present": 4503, "verified": 399 },
      "aspects": [ { "key": "sounds", "label": "Sounds", "rule": "...", "applies": 1283, "absent": 59,
                     "present": 1153, "verified": 71, "missing": ["amethyst_cluster", "..."], "missing_more": 19 } ],
      "lowest": ["..."], "lowest_more": 0, "notes": [] }
  ],
  "systems": [ { "key": "redstone", "label": "Redstone and pistons", "status": "verified", "score": 1.0, "weight": 4,
                 "evidence": "...", "notes": [] } ],
  "skipped": [],
  "history": [ { "t": "...", "commit": "31df2eea31", "one_to_one": 51.36, "strict": 14.35, "lenient": 88.38,
                 "absent": 3272, "present": 12856, "verified": 1602 } ]
}
```

- Each run appends one point to `history`, read from the existing output file. If nothing changed since the last run
  (same commit, same figures), the last point's time moves forward instead.
- The history keeps the latest 2000 points.
- Points from the first, schema 1 version of the file are dropped.
- Missing-id lists hold up to 40 ids each. Change this with `--missing-cap`.

## Running the audit

From the repository root:

```bash
python tools/tracker/audit.py --ref m1/blocks-and-chunks
```

`--ref` reads the committed revision through git, so uncommitted edits in a checkout do not leak into the figure, and
you need not check the branch out. Leave it off to audit the working tree.

A run takes about 10 seconds, uses git and Python only, and starts no Unreal process. It prints the headline and a
per-category table.

| Option | Meaning |
|---|---|
| `--test-log PATH` | The automation log. The default is `<main checkout>/Saved/Remote/run_tests.log` if it exists. |
| `--test-log-commit REV` | The commit that log's run built. Use this when you know it. Otherwise the audit reads `<log>.commit`, or infers the commit from the log's time. |
| `--no-test-log` | Use no log. Tests then verify nothing. |
| `--systems`, `--weights` | Other systems or weights files. |
| `--out PATH` | The output file. It defaults to `tools/tracker/progress.json`. |
| `--cache PATH` | The cache folder. It defaults to `<repo>/cache`; from a git worktree, it uses the main checkout's `cache/`. |

To make the commit match exact, have the test runner write the hash next to the log once it knows which commit it
built. For example, add this line to remote.sh after the run:

```bash
git -C "$CHECKOUT" rev-parse HEAD > "$CHECKOUT/Saved/Remote/run_tests.log.commit"
```

## Installing it on your website

You need two things on your host:

1. `progress.json`.
2. The widget files: `tracker.js` and `tracker.css`, plus `index.html` if you use the iframe.

Copy the widget files once, for example:

```bash
COPY_WIDGET=1 tools/tracker/update.sh /path/to/site/tracker
```

That puts all four files in `/path/to/site/tracker/`.

### Method 1: script tag and div (recommended)

Put this where the widget should appear:

```html
<div data-mc-tracker data-src="/tracker/progress.json"></div>
<script src="/tracker/tracker.js" defer></script>
```

The widget draws inside a shadow root, so your site's CSS cannot break it, and its CSS cannot leak into your page.
It shows:

- the 1:1 figure big, with strict and lenient beside it;
- a bar, the "what's left" counts, and a trend line;
- a row per category, which opens to show each aspect's bar, its rule and its absent ids;
- the systems list.

Options, set as attributes on the div:

| Attribute | Default | Meaning |
|---|---|---|
| `data-src` | `progress.json` | URL of the JSON. |
| `data-interval` | `300` | Seconds between re-fetches. `0` fetches once. The widget also re-fetches when the tab becomes visible again. |
| `data-theme` | `auto` | `auto` follows the visitor's light or dark setting; `light` and `dark` force one. |
| `data-title` | `mc-unreal: how close to 1:1` | The heading. |
| `data-css` | `tracker.css` next to `tracker.js` | URL of the stylesheet. `none` loads none, so only your own CSS applies. |
| `data-missing` | on | `off` hides the lists of missing ids. |
| `data-systems` | on | `off` hides the systems list. |

You can embed more than one widget on a page. If you add a div after the page has loaded, call
`McTracker.mount(div)`. After each successful fetch, the div fires a `mc-tracker-update` event with the data in
`event.detail`. You can use it to show the 1:1 figure elsewhere on your page, for example in a header.

### Method 2: iframe

```html
<iframe id="mc-tracker" src="/tracker/index.html?src=progress.json&theme=auto"
        title="Port progress" style="width:100%;border:0;min-height:520px" loading="lazy"></iframe>
<script>
  // Optional: let the frame size itself to its content (it grows when a row is opened).
  addEventListener("message", function (e) {
    if (e.data && e.data.type === "mc-tracker-height")
      document.getElementById("mc-tracker").style.height = e.data.height + "px";
  });
</script>
```

The query parameters are `src`, `theme`, `interval`, `title` and `missing`, with the same meanings as the attributes
above. A relative `src` is resolved against `index.html`, not against your page. If your site builder does not
accept script tags at all, the iframe without the resize script still works; give it a fixed height.

### JSON on the same host, or on another one

- **Same host:** serve `progress.json` from the same domain as the page, and nothing else is needed.
- **Another host:** this applies, for example, if the JSON is on a file host or a home server while the page is on
  your main site. The browser only lets the widget read the JSON if that host sends a CORS header. Some examples:

  - nginx:
    ```nginx
    location = /tracker/progress.json {
        add_header Access-Control-Allow-Origin "https://your-site.example" always;
        add_header Cache-Control "no-cache";
    }
    ```
  - Apache (`.htaccess` next to the file):
    ```apache
    <Files "progress.json">
      Header set Access-Control-Allow-Origin "https://your-site.example"
      Header set Cache-Control "no-cache"
    </Files>
    ```
  - Netlify (`_headers`):
    ```text
    /tracker/progress.json
      Access-Control-Allow-Origin: https://your-site.example
      Cache-Control: no-cache
    ```
  - GitHub Pages and most static hosts already send `Access-Control-Allow-Origin: *`.

  Use your site's own origin instead of `*` if you want only your page to read the file.

  With the iframe method, CORS only matters between `index.html` and the JSON. So if you host `index.html`, the JS
  and the JSON together on one host, any page can frame them without CORS.

- **Caching:** the widget asks for the file with `cache: "no-store"` and a `?_=<time>` query, so it always gets a fresh
  copy. Still, set `Cache-Control: no-cache` on the JSON if a CDN sits in front of it.
- `file://` pages cannot fetch. To try the widget locally, serve the folder:

  ```bash
  python -m http.server 8000 --directory tools/tracker
  ```

  Then open `http://localhost:8000/widget/demo.html` (after `update.sh` has written `out/progress.json`, or point
  `data-src` at `../progress.json`).

## Keeping it updated

`update.sh` reruns the audit on `m1/blocks-and-chunks` and writes `progress.json` into a folder. It uploads nothing.
Point it at a folder your web server serves, or one that a sync tool (an FTP client, rclone, a Dropbox/OneDrive
folder, a git-backed Pages repo) pushes for you.

```bash
tools/tracker/update.sh /path/to/site/tracker
```

It reads these environment variables:

| Variable | Meaning |
|---|---|
| `OUT_DIR` | The output folder. The first argument, if given, wins over it. The default is `tools/tracker/out`. |
| `AUDIT_REF` | The revision to audit. Defaults to `m1/blocks-and-chunks`; set it to an empty string to audit the working tree. |
| `TEST_LOG`, `TEST_LOG_COMMIT`, `TEST_LOG_NOTE` | Override the test log, its commit, and a note stored with it. By default the audit finds remote.sh's log by itself. |
| `COPY_WIDGET=1` | Also copy the widget files. |
| `PYTHON` | The Python to run. |

Keep the same output folder from run to run, because the history lives in that folder's `progress.json`.

### On a schedule: Windows Task Scheduler

1. Open **Task Scheduler** and choose **Create Task…**.
2. On the **General** tab, name the task "mc-unreal tracker" and select **Run whether user is logged on or not**.
3. On the **Triggers** tab, choose **New…** and set **Daily**, repeating every 1 hour (or whatever you like).
4. On the **Actions** tab, choose **New…**, then:
   - Program: `C:\Program Files\Git\bin\bash.exe`
   - Arguments: `-lc "/e/mc-unreal/tools/tracker/update.sh /e/www/tracker >> /e/mc-unreal/logs/tracker.log 2>&1"`
   - Start in: `E:\mc-unreal`
5. On the **Conditions** tab, you may untick **Start the task only if the computer is on AC power**.

Change `/e/www/tracker` to your output folder. The `logs/` folder is git-ignored. Each run takes about 10 seconds,
uses git and Python only, and never starts Unreal, so it is safe while someone is playing.

### Or on every merge into m1: a git hook

Git runs `post-merge` after a `git merge` or `git pull` in that checkout, and `post-commit` after commits made there,
including merge commits finished by hand after a conflict. Create `.git/hooks/post-merge` in the main checkout
(`E:\mc-unreal\.git\hooks\`; the hooks folder is shared by all worktrees):

```bash
#!/usr/bin/env bash
# Refresh the progress tracker after merges into m1/blocks-and-chunks.
[ "$(git rev-parse --abbrev-ref HEAD)" = "m1/blocks-and-chunks" ] || exit 0
OUT_DIR=/e/www/tracker "$(git rev-parse --show-toplevel)/tools/tracker/update.sh" >/dev/null 2>&1 &
```

Copy the same file to `.git/hooks/post-commit` if merges are often finished with `git commit`. The trailing `&` keeps
the merge from waiting on the audit. Hooks are not versioned, so set this up on each PC that merges into m1.

If the merges happen in a worktree that is on another branch (for example, a session merging with
`git merge` and then `git update-ref`), no hook fires for m1. Use the scheduled task there.

A finished test run changes the verified figures too, so a scheduled task catches both kinds of change.

## Changing the widget's look

- **Colours and sizes:** set any `--mct-*` variable on the div from your own CSS. These pass into the shadow root.

  ```css
  [data-mc-tracker] {
    --mct-score: #f08c00;      /* the 1:1 bar and trend line */
    --mct-verified: #2f9e44;   /* verified, and the strict part of a bar */
    --mct-lenient: #ffe8cc;    /* the lenient part of a bar */
    --mct-radius: 0;
    --mct-font: "Inter", sans-serif;
    --mct-max-width: 100%;
  }
  ```

  The variables are `--mct-bg`, `--mct-fg`, `--mct-muted`, `--mct-border`, `--mct-track`, `--mct-score`,
  `--mct-lenient`, `--mct-verified`, `--mct-present`, `--mct-absent`, `--mct-error`, `--mct-code-bg`, `--mct-font`,
  `--mct-mono`, `--mct-radius` and `--mct-max-width`.

  The dark theme sets its own values on `:host([data-theme="dark"])` and in the `prefers-color-scheme` block. To
  override colours in dark mode too, target `[data-mc-tracker][data-theme="dark"]` as well, or force one theme with
  `data-theme`.
- **Bigger changes:** copy `tracker.css`, edit it, and point `data-css` at your copy. The class names (`.hero`,
  `.big-num`, `.score-bar`, `.left-grid`, `.cat-head`, `.aspect`, `.chip-verified` and so on) are listed in the file.
- **Text and layout:** the heading is `data-title`. Everything else is built in `render()` in `tracker.js`, which is
  plain DOM code with no framework.
- **Phone width:** below 560 px, the counts grid drops to two columns and each row stacks its bar under its label.
  The breakpoint is the last `@media` block in `tracker.css`.
