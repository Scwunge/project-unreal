# Port progress tracker

A repeatable count of how much of vanilla Minecraft Java 26.3 the mc-unreal port covers, and a small widget that shows
it live on your own website.

- `audit.py` reads the vanilla content lists and the port's source, and writes `progress.json`.
- The widget fetches `progress.json` and draws it. It re-fetches on a timer, so when the file changes the page does too.
- `update.sh` reruns the audit and copies the file to a folder your website serves.

The output holds ids and counts only. It contains no game assets and no text from the jar. The audit never opens the
Minecraft jar. It reads only the reports and data files that `tools/fetch_cache.py` already put in `cache/`.

## What the numbers mean

Each vanilla content id (a block, an entity, a command, a recipe and so on) is in exactly one of three states.

| State | Meaning |
|---|---|
| **absent** | The audit found no sign of the id in the port. |
| **present** | The port names the id or loads it. The rule depends on the category; see below. |
| **verified** | A passing automation test or a vanilla comparison log names the id. |

### How "present" is decided

| Categories | Rule |
|---|---|
| Entities, block entities, mob effects, potions, particles, menus, statistics, attributes, villager professions, game events, commands, damage types, paintings, music discs | The id appears as a string literal in game source (`Source/**/*.cpp\|h`, not under `Tests/`), as `"creeper"` or `"minecraft:creeper"`. Comments do not count. For commands, the top-level names come from `commands.json`. |
| Blocks, items | `McBlockRegistry.cpp` registers every block from `blocks.json`, and `McItemRegistry.cpp` registers every item from `registries.json`. So an id counts if it registers **and** has a model or texture in the client assets (`blockstates/<id>.json` or `textures/block/<id>.png`; `items/<id>.json` or `textures/item/<id>.png`), or is named in source. The audit checks that each registry still loads from the report. If that load is ever removed, the category falls back to the string-literal rule. |
| Loot tables, enchantments, biomes | The source reads that data folder at runtime (the audit looks for the loader call, for example `McCache::Data(TEXT("worldgen/biome/*.json"))`), so every file in it counts. |
| Recipes, structures, advancements | The source loads the folder, **and** every type the file uses is named in source: the recipe's `type`, the structure's `type`, or each advancement criterion's `trigger`. A recipe of a serializer the port never names stays absent. |

Each category in `progress.json` carries its rule as a `rule` string. The widget shows it under the category's
missing ids.

### How "verified" is decided

You have to pass `--test-log`: an Unreal automation log of a run of the audited revision (`tools/run_tests.ps1`
writes `Saved/Logs/run_tests.log`). **Without a log, nothing is verified.** A test that exists in the source has not
been shown to pass.

With a log:

- **Test files.** Take a test file under `Source/**/Tests/` where every test it declares has
  `Result={Success}` in the log. Every id named as a string literal in that file is verified. If one test in the file
  failed or did not run, none of the file's ids count.
- **Test names.** If a passing test's last name segment, turned into snake_case, equals an id, that id is verified.
  For example, `McClient.McSim.Fire` verifies `fire`.
- **Vanilla comparison logs.** `McClient.McSim.RefScripts` holds the port to `tools/refscripts/expect/rs_*.txt`, and
  `McClient.McSim.RefScriptsLogistics` holds it to `expect/lg_*.txt`. If that test passed, every block named in the
  state columns of those logs is verified.
- Only tests the audited revision declares count. If the log came from a different build, record which one with
  `--test-log-note`; the note is stored in `progress.json` and shown nowhere else.
- If the log has `DataCoverage:` lines for loot tables or recipes that did not load, those ids go back to absent.

### Limits

Read the figures with these in mind.

- **Present is not done.** For most categories, present only means the source mentions the id. For blocks, items,
  loot tables, enchantments and biomes, it means the generic loader reads the id. Neither says the id behaves as it
  does in vanilla. A block counts as present because its model draws, even if its behaviour (a piston pushing, a
  crop growing) is missing.
- **Names built at runtime are missed.** If the source builds an id from parts, for example
  `Wood + TEXT("_boat")`, the audit cannot see it. Boats show as absent entities for this reason. Treat absent as
  "not found", not "certainly missing".
- **A literal can be a coincidence.** A short id such as `"say"` or `"stone"` may appear in source for another
  reason.
- **Verified means a test passed, not that it matches vanilla.** Most automation tests check the port against values
  a session wrote down. Only the ref-log rule compares with a real vanilla run.
- **The totals are weighted by id count.** Blocks, items, recipes and loot tables make up about two thirds of all ids,
  and they are mostly present under the loader rules. The overall bar therefore reads higher than an honest sense of
  "how done is the game". `category_mean_pct` gives every category the same weight. Quote that figure, or the
  per-category rows, rather than the overall percentage alone.
- **Some data is not counted:** sounds, tags, worldgen features, dimension types and others. You can add them in the
  `DATA_CATEGORIES` and `REGISTRY_CATEGORIES` tables at the top of `audit.py`. If a listed data folder is missing from
  the cache, it is reported under `skipped`.

## Files

| File | What it is |
|---|---|
| `audit.py` | The audit. Python 3, standard library only. Run `python tools/tracker/audit.py --help`. |
| `progress.json` | The audit output, committed once as a sample. `update.sh` writes elsewhere by default. |
| `update.sh` | Reruns the audit and copies the output (and, if you ask, the widget) to a folder. |
| `widget/tracker.js` | The widget. Plain JavaScript, no build step, no CDN. |
| `widget/tracker.css` | The widget's stylesheet. `tracker.js` loads it from the same folder. |
| `widget/index.html` | A page holding only the widget, for embedding with an `<iframe>`. |
| `widget/demo.html` | Both embed methods on one page, for trying it locally. |
| `out/` | The default output folder of `update.sh`. Git ignores it. |

### progress.json, briefly

```json
{
  "schema": 1,
  "generated": "2026-11-11T19:30:00+00:00",
  "commit": { "hash": "...", "branch": "m1/blocks-and-chunks", "date": "...", "subject": "..." },
  "test_log": { "used": false },
  "ref_logs": [ { "logs": 29, "test": "McClient.McSim.RefScripts", "passed": false } ],
  "totals": { "ids": 9322, "absent": 2181, "present": 7141, "verified": 0 },
  "category_mean_pct": { "present_or_verified": 69.38, "verified": 0.0 },
  "categories": [
    { "key": "entity_type", "label": "Entities", "group": "registry", "ids": 161,
      "absent": 41, "present": 120, "verified": 0, "named_in_source": 120, "verified_by": {},
      "rule": "...", "missing": ["acacia_boat", "..."], "missing_more": 0 }
  ],
  "skipped": [],
  "history": [ { "t": "...", "commit": "31df2eea31", "ids": 9322, "absent": 2181, "present": 7141, "verified": 0 } ]
}
```

- `absent`, `present` and `verified` never overlap, and they add up to `ids`.
- Each run appends one point to `history`, read from the existing output file. If nothing changed since the last run
  (same commit, same counts), the last point's time moves forward instead. The history keeps the latest 2000 points.
- `missing` lists up to 60 absent ids per category. Change this with `--missing-cap`.

## Running the audit

From the repository root:

```bash
python tools/tracker/audit.py --ref m1/blocks-and-chunks
```

`--ref` reads the committed revision through git, so uncommitted edits in a checkout do not leak into the figure, and
you need not check the branch out. Leave it off to audit the working tree. Other options:

- `--test-log PATH` turns on verified (see above).
- `--test-log-note TEXT` records where the log came from.
- `--out PATH` sets the output file. It defaults to `tools/tracker/progress.json`.
- `--cache PATH` sets the cache folder. It defaults to `<repo>/cache`; from a git worktree, it uses the main
  checkout's `cache/`.

A run takes a few seconds and starts no Unreal process.

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
Options, set as attributes on the div:

| Attribute | Default | Meaning |
|---|---|---|
| `data-src` | `progress.json` | URL of the JSON. |
| `data-interval` | `300` | Seconds between re-fetches. `0` fetches once. The widget also re-fetches when the tab becomes visible again. |
| `data-theme` | `auto` | `auto` follows the visitor's light or dark setting; `light` and `dark` force one. |
| `data-title` | `mc-unreal port progress` | The heading. |
| `data-css` | `tracker.css` next to `tracker.js` | URL of the stylesheet. `none` loads none, so only your own CSS applies. |
| `data-missing` | on | `off` hides the lists of missing ids. |

You can embed more than one widget on a page. If you add a div after the page has loaded, call
`McTracker.mount(div)`. After each successful fetch, the div fires a `mc-tracker-update` event with the data in
`event.detail`, in case you want to show a figure elsewhere on the page.

### Method 2: iframe

```html
<iframe id="mc-tracker" src="/tracker/index.html?src=progress.json&theme=auto"
        title="Port progress" style="width:100%;border:0;min-height:400px" loading="lazy"></iframe>
<script>
  // Optional: let the frame size itself to its content.
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

  Then open `http://localhost:8000/widget/demo.html`.

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
| `TEST_LOG`, `TEST_LOG_NOTE` | A test log to turn on verified, and a note on where it came from. |
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

Change `/e/www/tracker` to your output folder. The `logs/` folder is git-ignored. Each run takes a few seconds, uses
git and Python only, and never starts Unreal.

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

## Changing the widget's look

- **Colours and sizes:** set any `--mct-*` variable on the div from your own CSS. These pass into the shadow root.

  ```css
  [data-mc-tracker] {
    --mct-verified: #2f9e44;
    --mct-present: #f08c00;
    --mct-absent: #dee2e6;
    --mct-radius: 0;
    --mct-font: "Inter", sans-serif;
    --mct-max-width: 100%;
  }
  ```

  The variables are `--mct-bg`, `--mct-fg`, `--mct-muted`, `--mct-border`, `--mct-track`, `--mct-verified`,
  `--mct-present`, `--mct-absent`, `--mct-error`, `--mct-code-bg`, `--mct-font`, `--mct-mono`, `--mct-radius` and
  `--mct-max-width`.

  The dark theme sets its own values on `:host([data-theme="dark"])` and in the `prefers-color-scheme` block. To
  override colours in dark mode too, target `[data-mc-tracker][data-theme="dark"]` as well, or force one theme with
  `data-theme`.
- **Bigger changes:** copy `tracker.css`, edit it, and point `data-css` at your copy. The class names (`.card`,
  `.bar`, `.seg-verified`, `.cat-head` and so on) are listed in the file.
- **Text and layout:** the heading is `data-title`. Everything else is built in `render()` in `tracker.js`, which is
  plain DOM code with no framework.
- **Phone width:** below 520 px, each category row stacks its bar under its label. The breakpoint is the last
  `@media` block in `tracker.css`.
