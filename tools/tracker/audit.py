#!/usr/bin/env python3
"""Content audit for the mc-unreal port: a weighted 1:1 percentage built from every vanilla content id.

Python 3 standard library only. It reads:
  - cache/data/reports (registries.json, commands.json, blocks.json and the item components report), which the
    vanilla server's own data generator writes;
  - the data files extracted under cache/client/data/minecraft and the asset file names under
    cache/client/assets/minecraft;
  - the port's C++ source, tools/refscripts/expect/* and PLAN.md, at a git revision;
  - an Unreal automation log (remote.sh's Saved/Remote/run_tests.log by default), matched to a commit.
It never opens the Minecraft jar. The output holds ids, counts and short rule texts only.

Every id is scored on aspects (a block on its model, states, behaviour, drops and sounds; a mob on its model, AI,
drops, sounds and spawning; a command on every branch of its syntax tree). Each aspect is absent (0), present
(0.5) or verified (1); unknown counts as absent. An id's score is the mean of the aspects that apply to it, a
category's score the mean of its ids, and the headline the weighted mean of the categories and of the hand-kept
systems in systems.json, with the weights in weights.json. TRACKER.md explains every rule and its limits.

  python tools/tracker/audit.py --ref m1/blocks-and-chunks
"""

from __future__ import annotations

import argparse
import collections
import datetime as dt
import json
import os
import re
import subprocess
import sys
from pathlib import Path

SCHEMA = 2
HISTORY_CAP = 2000
NS = "minecraft:"
HERE = Path(__file__).resolve().parent

ABSENT, PRESENT, VERIFIED = 0, 1, 2
VALUE = {ABSENT: 0.0, PRESENT: 0.5, VERIFIED: 1.0}
STATE_NAME = {ABSENT: "absent", PRESENT: "present", VERIFIED: "verified"}

# ----------------------------------------------------------------------------------------------------------------
# Where in the source each aspect is looked for. A group is a list of path prefixes under Source/ (game code only,
# never Tests/). "Named in a group" means: a string literal in those files is the id ("creeper" or
# "minecraft:creeper"), or names a tag the id is in, or the id is built from parts the files name (see named()).
# ----------------------------------------------------------------------------------------------------------------
GROUPS = {
    "all": ["Source/"],
    "render": ["Source/McAssets/", "Source/McRender/", "Source/McUI/"],
    "entity_model": ["Source/McRender/"],
    "placement": ["Source/McPlayer/Private/McBlockPlacement.cpp"],
    "block_behaviour": ["Source/McSim/", "Source/McWorld/", "Source/McClient/Private/McBlockUse.cpp",
                        "Source/McClient/Private/McBuckets.cpp", "Source/McPlayer/Private/McBlockBreak.cpp",
                        "Source/McPlayer/Private/McBed.cpp", "Source/McItem/", "Source/McEntity/Private/McTnt.cpp",
                        "Source/McServer/Private/McSpawners.cpp", "Source/McCore/Private/McBlockLight.cpp"],
    "entity_ai": ["Source/McEntity/", "Source/McServer/Private/McEntityHost", "Source/McServer/Private/McSculkMobs.cpp"],
    "spawning": ["Source/McServer/", "Source/McCore/Private/McStructureSpawns.cpp", "Source/McCore/Private/McBiomeRegistry.cpp"],
    "item_use": ["Source/McPlayer/", "Source/McItem/", "Source/McEntity/", "Source/McClient/", "Source/McServer/"],
    "audio": ["Source/McAudio/", "Source/McCore/Private/McSound.cpp"],
    "commands": ["Source/McServer/Private/McCommands.cpp", "Source/McServer/Public/McCommands.h"],
}

# Which test modules can verify which aspect: a passing test file in that module that names the id.
VERIFY_MODULES = {
    "model": {"McAssets", "McRender", "McUI"},
    "states": {"McPlayer", "McSim", "McWorld", "McCore"},
    "behaviour": {"McSim", "McWorld", "McClient", "McPlayer", "McItem", "McEntity", "McServer"},
    "drops": {"McGen", "McPlayer", "McServer", "McWorld"},
    "sounds": {"McAudio", "McCore"},
    "use": {"McPlayer", "McItem", "McEntity", "McClient", "McServer"},
    "obtain": {"McItem", "McGen", "McEntity", "McServer"},
    "ai": {"McEntity", "McServer"},
    "spawning": {"McServer", "McGen"},
}

# Item components that mean "this item does something when used, worn or swung" (from the components report).
ACTIVE_COMPONENTS = {
    "consumable", "food", "tool", "weapon", "equippable", "blocks_attacks", "use_cooldown", "kinetic_weapon",
    "piercing_weapon", "glider", "jukebox_playable", "instrument", "bundle_contents", "charged_projectiles",
    "death_protection", "writable_book_content", "fireworks", "map_decorations", "potion_contents",
    "bucket_entity_data", "debug_stick_state", "recipes", "ominous_bottle_amplifier", "suspicious_stew_effects",
}
# Block properties that only describe shape or orientation; a block with nothing else has no behaviour to score.
STRUCTURAL_PROPS = {"waterlogged", "facing", "axis", "half", "type", "shape", "north", "south", "east", "west",
                    "up", "down", "rotation", "face", "hinge", "attachment", "orientation", "vertical_direction"}

REGISTRY_SIMPLE = [
    ("block_entity_type", "minecraft:block_entity_type", "Block entities"),
    ("mob_effect", "minecraft:mob_effect", "Mob effects"),
    ("potion", "minecraft:potion", "Potions"),
    ("particle_type", "minecraft:particle_type", "Particles"),
    ("menu", "minecraft:menu", "Menus (screens)"),
    ("custom_stat", "minecraft:custom_stat", "Statistics"),
    ("attribute", "minecraft:attribute", "Attributes"),
    ("villager_profession", "minecraft:villager_profession", "Villager professions"),
    ("game_event", "minecraft:game_event", "Game events"),
]

DATA_CATEGORIES = [
    # key, folder, label, loader regex in game source, type check
    ("advancement", "advancement", "Advancements", r'McData::List\(TEXT\("advancement"\)\)', "criteria_triggers"),
    ("recipe", "recipe", "Recipes", r'McData::List\(TEXT\("recipe"\)\)', "type"),
    ("loot_table", "loot_table", "Loot tables", r'McData::PathOf\(\w+,\s*TEXT\("loot_table"\)', None),
    ("enchantment", "enchantment", "Enchantments", r'McCache::Data\(TEXT\("enchantment/\*\.json"\)\)', None),
    ("biome", "worldgen/biome", "Biomes", r'McCache::Data\(TEXT\("worldgen/biome/\*\.json"\)\)', None),
    ("structure", "worldgen/structure", "Structures", r'McCache::Data\(TEXT\("worldgen/structure/\*\.json"\)\)', "type"),
    ("damage_type", "damage_type", "Damage types", None, None),
    ("painting_variant", "painting_variant", "Paintings", None, None),
    ("jukebox_song", "jukebox_song", "Music discs", None, None),
]

LOADERS = {
    "block_registry": ("Source/McCore/Private/McBlockRegistry.cpp", r'McCache::Report\(TEXT\("blocks\.json"\)\)'),
    "item_registry": ("Source/McItem/Private/McItemRegistry.cpp", r'McCache::Report\(TEXT\("registries\.json"\)\)'),
    "block_loot": (None, r'TEXT\("loot_table/blocks/"\)'),
    "loot": (None, r'McData::PathOf\(\w+,\s*TEXT\("loot_table"\)'),
    "recipes": (None, r'McData::List\(TEXT\("recipe"\)\)'),
    "trades": (None, r'TEXT\("villager_trade/"\)'),
    "natural_spawns": (None, r'natural_mob_spawns'),
}

DATA_COVERAGE_FAILURES = {
    "loot tables that did not load": "loot_table",
    "recipe files that did not load (unknown type or malformed)": "recipe",
}
REF_LOGS = [("tools/refscripts/expect/rs_", "McClient.McSim.RefScripts"),
            ("tools/refscripts/expect/lg_", "McClient.McSim.RefScriptsLogistics")]
TEST_MACRO = re.compile(
    r'(?:IMPLEMENT_(?:SIMPLE|COMPLEX|CUSTOM_SIMPLE|CUSTOM_COMPLEX)_AUTOMATION_TEST|BEGIN_DEFINE_SPEC|DEFINE_SPEC)'
    r'\s*\(\s*\w+\s*,\s*(?:\w+\s*,\s*)?"([^"]+)"')
LEXER = re.compile(
    r'R"([^(\s"]{0,16})\((.*?)\)\1"'  # raw string: group 2
    r'|//[^\n]*'                      # line comment
    r'|/\*.*?\*/'                     # block comment
    r"|'(?:\\.|[^'\\\n])*'"           # character literal
    r'|"((?:\\.|[^"\\\n])*)"',        # string literal: group 3
    re.S)


# ----------------------------------------------------------------------------------------------------------------
# Git and source access
# ----------------------------------------------------------------------------------------------------------------

def git(repo: Path, *args: str, binary: bool = False, stdin: bytes | None = None, check: bool = True):
    out = subprocess.run(["git", "-C", str(repo), *args], input=stdin, capture_output=True, check=check)
    return out.stdout if binary else out.stdout.decode("utf-8", "replace").strip()


class Tree:
    """Source files, the ref logs and PLAN.md, from the working tree or from a git revision."""

    def __init__(self, repo: Path, ref: str | None):
        self.repo, self.ref = repo, ref
        self.files: dict[str, str] = {}
        keep = lambda rel: (rel.startswith("Source/") and rel.endswith((".cpp", ".h"))) \
            or rel.startswith("tools/refscripts/expect/") or rel == "PLAN.md"
        if ref:
            names = [n for n in git(repo, "ls-tree", "-r", "--name-only", ref, "--", "Source", "tools/refscripts",
                                    "PLAN.md").splitlines() if keep(n)]
            data = git(repo, "cat-file", "--batch", binary=True,
                       stdin="".join(f"{ref}:{n}\n" for n in names).encode("utf-8"))
            pos = 0
            for name in names:
                end = data.index(b"\n", pos)
                size = int(data[pos:end].split()[2])
                self.files[name] = data[end + 1:end + 1 + size].decode("utf-8", "replace")
                pos = end + 1 + size + 1
        else:
            for base in ("Source", "tools/refscripts", "PLAN.md"):
                root = repo / base
                paths = [root] if root.is_file() else root.rglob("*") if root.is_dir() else []
                for path in paths:
                    rel = path.relative_to(repo).as_posix()
                    if path.is_file() and keep(rel):
                        self.files[rel] = path.read_text(encoding="utf-8", errors="replace")

    def sources(self, tests: bool | None = None):
        for rel, text in self.files.items():
            if rel.startswith("Source/") and (tests is None or tests == ("/Tests/" in rel)):
                yield rel, text


def string_literals(text: str) -> list[str]:
    """The string literals of a C++ file, skipping comments and character literals (a lexer, not a parser)."""
    out = []
    for m in LEXER.finditer(text):
        if m.group(3) is not None:
            out.append(m.group(3))
        elif m.group(2) is not None:
            out.append(m.group(2))
    return out


def strip_ns(s: str) -> str:
    s = s.strip()
    if s.startswith("#"):
        s = s[1:]
    return s[len(NS):] if s.startswith(NS) else s


def module_of(rel: str) -> str:
    parts = rel.split("/")
    return parts[1] if len(parts) > 1 else ""


def camel_to_snake(name: str) -> str:
    return re.sub(r"(?<=[a-z0-9])(?=[A-Z])", "_", name).lower()


# ----------------------------------------------------------------------------------------------------------------
# Naming: literals, tags and parts
# ----------------------------------------------------------------------------------------------------------------

class Names:
    """What a group of source files names. named(kind, id) -> "literal" | "tag" | "parts" | None."""

    def __init__(self, literals: set[str], family: dict, tags: dict):
        self.literals = {strip_ns(l) for l in literals if l.strip()}
        self.raw = literals
        self.family = family  # kind -> (suffix counts, prefix counts, ids)
        self.tags = tags      # kind -> {tag: members}
        self.suffixes = {l for l in self.literals if l.startswith("_") and len(l) >= 4 and re.fullmatch(r"[a-z0-9_]+", l)}
        self.prefixes = {l for l in self.literals if l.endswith("_") and len(l) >= 4 and re.fullmatch(r"[a-z0-9_]+", l)}
        formats = [l for l in self.literals if "%s" in l and re.fullmatch(r"[a-z0-9_%s]+", l) and len(l) >= 5]
        self.formats = [re.compile(re.escape(f).replace("%s", "[a-z0-9_]+")) for f in formats]
        self._tag_hits: dict[str, set[str]] = {}

    def tagged(self, kind: str) -> set[str]:
        if kind not in self._tag_hits:
            hit = set()
            ids = self.family.get(kind, (None, None, set()))[2]
            for tag, members in self.tags.get(kind, {}).items():
                if tag in self.literals and tag not in ids:
                    hit |= members
            self._tag_hits[kind] = hit
        return self._tag_hits[kind]

    def named(self, kind: str, i: str, parts: bool = True) -> str | None:
        if i in self.literals:
            return "literal"
        if i in self.tagged(kind):
            return "tag"
        if not parts:
            return None
        suffix_counts, prefix_counts, _ = self.family.get(kind, ({}, {}, set()))
        bits = i.split("_")
        for k in range(1, len(bits)):
            suf = "_" + "_".join(bits[k:])
            if suf in self.suffixes and suffix_counts.get(suf, 0) >= 3:
                return "parts"
            pre = "_".join(bits[:k]) + "_"
            if pre in self.prefixes and prefix_counts.get(pre, 0) >= 3:
                return "parts"
        if any(f.fullmatch(i) for f in self.formats):
            return "parts"
        return None


def families(ids) -> tuple[dict, dict, set]:
    suf, pre = collections.Counter(), collections.Counter()
    for i in ids:
        bits = i.split("_")
        for k in range(1, len(bits)):
            suf["_" + "_".join(bits[k:])] += 1
            pre["_".join(bits[:k]) + "_"] += 1
    return suf, pre, set(ids)


def load_tags(folder: Path) -> dict[str, set[str]]:
    raw = {}
    if folder.is_dir():
        for path in folder.rglob("*.json"):
            try:
                values = json.loads(path.read_text(encoding="utf-8")).get("values", [])
            except ValueError:
                continue
            raw[path.relative_to(folder).with_suffix("").as_posix()] = [
                (v if isinstance(v, str) else v.get("id", "")) for v in values]
    out: dict[str, set[str]] = {}

    def expand(tag, seen):
        if tag in out:
            return out[tag]
        members = set()
        for v in raw.get(tag, []):
            if v.startswith("#"):
                ref = strip_ns(v)
                if ref not in seen:
                    members |= expand(ref, seen | {ref})
            elif v:
                members.add(strip_ns(v))
        out[tag] = members
        return members

    for tag in raw:
        expand(tag, {tag})
    return out


# ----------------------------------------------------------------------------------------------------------------
# Evidence for "verified"
# ----------------------------------------------------------------------------------------------------------------

def read_test_log(path: Path):
    results: dict[str, bool] = {}
    coverage: dict[str, set[str]] = {}
    done = re.compile(r"Test Completed\. Result=\{([^}]*)\} Name=\{[^}]*\} Path=\{([^}]*)\}")
    cov = re.compile(r"DataCoverage: (.+): (\S+)\s*$")
    with path.open(encoding="utf-8", errors="replace") as handle:
        for line in handle:
            m = done.search(line)
            if m:
                results[m.group(2)] = m.group(1) == "Success"
                continue
            m = cov.search(line)
            if m and m.group(1) in DATA_COVERAGE_FAILURES:
                coverage.setdefault(DATA_COVERAGE_FAILURES[m.group(1)], set()).add(m.group(2))
    return results, coverage


def tests_pass(results: dict[str, bool], declared: list[str]) -> bool:
    if not declared:
        return False
    for test in declared:
        hits = [ok for name, ok in results.items() if name == test or name.startswith(test + " ")]
        if not hits or not all(hits):
            return False
    return True


class Evidence:
    def __init__(self):
        self.by_module: dict[str, set[str]] = collections.defaultdict(set)   # ids named by passing test files
        self.test_literals: list[str] = []                                    # every literal of passing test files
        self.ref_blocks: set[str] = set()
        self.plan_ids: dict[str, set[str]] = {"model": set(), "behaviour": set()}
        self.plan_lines = 0

    def any_test(self, i: str) -> bool:
        return any(i in s for s in self.by_module.values())

    def for_aspect(self, aspect: str, i: str) -> str | None:
        if any(i in self.by_module.get(m, ()) for m in VERIFY_MODULES.get(aspect, ())):
            return "test"
        return None


PLAN_EVIDENCE = re.compile(r"matches vanilla|match(?:es|ed)? the reference|all equal|held to the reference|"
                           r"\b(\d+)/\1\b|vanilla log|reference client", re.I)
PLAIN_WORDS = {"light", "stone", "water", "fire", "glass", "barrier", "structure_void", "player", "marker",
               "interaction", "item", "light_block"}
PLAN_DOUBT = re.compile(r"unbuilt|untested|not yet|parked|\bopen:|guess|not built|not replayed|unexplained", re.I)


def plan_citations(text: str, refnames: set[str], block_ids: set[str], entity_ids: set[str]):
    """Ids cited by PLAN.md sentences that report a vanilla comparison: lines naming a baked ref log (rs_*/lg_* with
    an expect file) or saying "matches vanilla", "all equal", "32/32" and the like, and not saying unbuilt,
    untested, parked, guess or unexplained. Ids are taken from `backticks`, minecraft:ids and words with an
    underscore that are block or entity ids, and from the ids' plain names ("blast furnace", "Witch"; one-word
    names of 5 letters or more). A sentence about shots ("shot", "screenshot", "looks") verifies the model aspect;
    any other verifies behaviour (blocks) or AI (entities). Returns ({"model"|"behaviour": ids}, sentences)."""
    out = {"model": set(), "behaviour": set()}
    lines = 0
    known = block_ids | entity_ids
    spaced = {}
    for i in known:
        if i in PLAIN_WORDS:
            continue  # ordinary English in PLAN.md prose; still counted from `backticks` and minecraft:ids
        if "_" in i:
            spaced[i] = re.compile(r"\b" + re.escape(i.replace("_", " ")) + r"s?\b", re.I)
        elif len(i) >= 5:
            spaced[i] = re.compile(r"\b" + re.escape(i) + r"s?\b", re.I)
    sentences = (s for line in text.splitlines() for s in re.split(r"(?<=[.;])\s+", line))
    for line in sentences:
        cites_ref = any(re.search(r"\b" + re.escape(r) + r"\b", line) for r in refnames)
        if not (cites_ref or PLAN_EVIDENCE.search(line)) or PLAN_DOUBT.search(line):
            continue
        found = set()
        for tok in re.findall(r"`([^`]+)`", line):
            found.add(strip_ns(tok.split("[")[0]))
        for tok in re.findall(r"minecraft:([a-z0-9_/]+)", line):
            found.add(tok)
        for tok in re.findall(r"\b[a-z0-9]+(?:_[a-z0-9]+)+\b", line):
            if not re.match(r"(rs|lg)_", tok):
                found.add(tok)
        found |= {i for i, rx in spaced.items() if rx.search(line)}
        found &= known
        if found:
            visual = not cites_ref and re.search(r"\bshots?\b|screenshot|\blooks?\b", line, re.I)
            out["model" if visual else "behaviour"] |= found
            lines += 1
    return out, lines


# ----------------------------------------------------------------------------------------------------------------
# The audit
# ----------------------------------------------------------------------------------------------------------------

def find_cache(repo: Path, explicit: str | None) -> Path:
    if explicit:
        return Path(explicit)
    if (repo / "cache" / "data" / "reports").is_dir():
        return repo / "cache"
    return main_checkout(repo) / "cache"


def main_checkout(repo: Path) -> Path:
    return Path(git(repo, "rev-parse", "--path-format=absolute", "--git-common-dir")).parent


def load_json(path: Path):
    try:
        return json.loads(path.read_text(encoding="utf-8"))
    except (OSError, ValueError):
        return None


def walk_items(node, out: set[str]):
    """Every item id a loot table, recipe or trade could hand out (\"name\"/\"id\"/\"item\" string fields)."""
    if isinstance(node, dict):
        for k, v in node.items():
            if k in ("name", "id", "item", "result") and isinstance(v, str) and v.startswith(NS):
                out.add(strip_ns(v))
            else:
                walk_items(v, out)
    elif isinstance(node, list):
        for v in node:
            walk_items(v, out)


def command_paths(node, prefix, out):
    """Every executable (or redirecting) path from a top-level command node: lists of (type, name)."""
    here = prefix + [(node.get("type"), node.get("_name"))]
    if node.get("executable") or "redirect" in node:
        out.append(here)
    for name, child in (node.get("children") or {}).items():
        child = dict(child, _name=name)
        command_paths(child, here, out)


class Aspect:
    def __init__(self, key, label, rule):
        self.key, self.label, self.rule = key, label, rule


def audit(args) -> dict:
    repo = Path(args.repo).resolve()
    rev = args.ref or "HEAD"
    tree = Tree(repo, args.ref)
    cache = find_cache(repo, args.cache)
    reports = cache / "data" / "reports"
    data_root = cache / "client" / "data" / "minecraft"
    assets = cache / "client" / "assets" / "minecraft"
    if not (reports / "registries.json").is_file():
        sys.exit(f"audit: no registries.json under {reports}; run tools/fetch_cache.py first (or pass --cache).")
    commit_hash = git(repo, "rev-parse", rev)
    commit = {"hash": commit_hash, "branch": args.ref or git(repo, "rev-parse", "--abbrev-ref", "HEAD"),
              "date": git(repo, "show", "-s", "--format=%cI", rev),
              "subject": git(repo, "show", "-s", "--format=%s", rev)[:120]}
    if not args.ref:
        commit["dirty"] = bool(git(repo, "status", "--porcelain", "--", "Source", "tools/refscripts"))

    registries = load_json(reports / "registries.json")
    reg = lambda r: sorted(strip_ns(e) for e in registries.get(r, {}).get("entries", {}))
    block_ids, item_ids, entity_ids = reg("minecraft:block"), reg("minecraft:item"), reg("minecraft:entity_type")
    sound_events = set(reg("minecraft:sound_event"))

    # ---- what the source names, per group --------------------------------------------------------------------
    file_lits = {rel: string_literals(text) for rel, text in tree.sources(tests=False)}
    tags = {"block": load_tags(data_root / "tags" / "block"), "item": load_tags(data_root / "tags" / "item"),
            "entity_type": load_tags(data_root / "tags" / "entity_type")}
    kinds_ids = {"block": block_ids, "item": item_ids, "entity_type": entity_ids}
    fam = {k: families(v) for k, v in kinds_ids.items()}

    group_cache: dict[str, Names] = {}

    def group(name: str) -> Names:
        if name not in group_cache:
            lits = set()
            for rel, ls in file_lits.items():
                if any(rel.startswith(p) for p in GROUPS[name]):
                    lits.update(ls)
            group_cache[name] = Names(lits, fam, tags)
        return group_cache[name]

    def fam_for(kind, ids):
        if kind not in fam:
            fam[kind] = families(ids)

    def loader(key):
        path, pattern = LOADERS[key]
        rx = re.compile(pattern)
        for rel, text in tree.sources(tests=False):
            if (path is None or rel == path) and rx.search(text):
                return rel
        return None

    # ---- test log, matched to a commit -----------------------------------------------------------------------
    results, coverage_fail, log_info = {}, {}, {"used": False}
    log_path = Path(args.test_log) if args.test_log else None
    if log_path is None and not args.no_test_log:
        default = main_checkout(repo) / "Saved" / "Remote" / "run_tests.log"
        log_path = default if default.is_file() else None
    log_commit = None
    if log_path and log_path.is_file():
        results, coverage_fail = read_test_log(log_path)
        mtime = dt.datetime.fromtimestamp(log_path.stat().st_mtime, dt.timezone.utc)
        how = "given"
        if args.test_log_commit:
            log_commit = git(repo, "rev-parse", args.test_log_commit)
        else:
            side = log_path.with_name(log_path.name + ".commit")
            if side.is_file():
                log_commit, how = git(repo, "rev-parse", side.read_text().strip()), "sidecar file"
            else:
                log_commit = git(repo, "rev-list", "-1", f"--before={mtime.isoformat()}", rev) or None
                how = "inferred: newest commit of the audited branch before the log was written"
        log_info = {"used": bool(log_commit), "file": log_path.name, "modified": mtime.isoformat(timespec="seconds"),
                    "commit": (log_commit or "")[:10], "commit_source": how,
                    "tests_passed": sum(1 for ok in results.values() if ok),
                    "tests_failed": sum(1 for ok in results.values() if not ok), "note": args.test_log_note or ""}
        if not log_commit:
            results, coverage_fail = {}, {}
            log_info["note"] = (log_info["note"] + " no commit of the audited branch predates the log; ignored").strip()
    changed_since_log: set[str] = set()
    if log_commit and log_commit != commit_hash:
        changed_since_log = set(git(repo, "diff", "--name-only", log_commit, commit_hash, "--",
                                    "Source", "tools/refscripts").splitlines())
        log_info["files_changed_since"] = len(changed_since_log)

    ev = Evidence()
    declared_all: set[str] = set()
    counted_files = skipped_changed = 0
    for rel, text in tree.sources(tests=True):
        declared = TEST_MACRO.findall(text)
        declared_all.update(declared)
        if not results or not tests_pass(results, declared):
            continue
        if rel in changed_since_log:
            skipped_changed += 1
            continue
        counted_files += 1
        lits = string_literals(text)
        ev.test_literals.extend(lits)
        mod = module_of(rel)
        ev.by_module[mod] |= {strip_ns(l) for l in lits}
        for test in declared:
            ev.by_module[mod].add(camel_to_snake(test.rsplit(".", 1)[-1]))
    if log_info.get("used"):
        log_info.update(test_files_counted=counted_files, test_files_changed_since_log=skipped_changed,
                        tests_matched=sum(1 for t in declared_all if t in results))

    ref_info = []
    for prefix, test in REF_LOGS:
        logs = [rel for rel in tree.files if rel.startswith(prefix)]
        ok = results.get(test) is True and test in declared_all
        usable = [rel for rel in logs if rel not in changed_since_log] if ok else []
        ref_info.append({"logs": len(logs), "test": test, "passed": ok, "logs_counted": len(usable)})
        for rel in usable:
            for line in tree.files[rel].splitlines():
                for state in line.split("|")[2:4]:
                    name = strip_ns(state.split("[", 1)[0])
                    if name and name != "-":
                        ev.ref_blocks.add(name)
    refnames = {Path(rel).stem for rel in tree.files if rel.startswith("tools/refscripts/expect/")}
    ev.plan_ids, ev.plan_lines = plan_citations(tree.files.get("PLAN.md", ""), refnames, set(block_ids), set(entity_ids))

    categories, skipped = [], []

    # ---- scoring helpers ---------------------------------------------------------------------------------------
    def finish(key, label, group_name, aspects, per_id, notes=None):
        """per_id: {id: {aspect: state}} (aspects missing from the dict do not apply to that id)."""
        acount = {a.key: {"label": a.label, "rule": a.rule, "applies": 0, "absent": 0, "present": 0, "verified": 0,
                          "missing": []} for a in aspects}
        scores, strict, lenient = [], [], []
        id_rows = []
        for i, states in per_id.items():
            if not states:
                continue
            vals = [VALUE[s] for s in states.values()]
            sc = sum(vals) / len(vals)
            scores.append(sc)
            strict.append(sum(1.0 for s in states.values() if s == VERIFIED) / len(vals))
            lenient.append(sum(1.0 for s in states.values() if s != ABSENT) / len(vals))
            for a, s in states.items():
                c = acount[a]
                c["applies"] += 1
                c[STATE_NAME[s]] += 1
                if s == ABSENT:
                    c["missing"].append(i)
            id_rows.append((sc, i))
        n = len(scores) or 1
        for c in acount.values():
            c["missing_more"] = max(0, len(c["missing"]) - args.missing_cap)
            c["missing"] = sorted(c["missing"])[:args.missing_cap]
        aspect_list = [dict(key=k, **v) for k, v in acount.items() if v["applies"]]
        totals = {s: sum(a[s] for a in aspect_list) for s in ("applies", "absent", "present", "verified")}
        low = [i for sc, i in sorted(id_rows) if sc < 1.0]
        categories.append({
            "key": key, "label": label, "group": group_name, "ids": len(scores),
            "score": round(sum(scores) / n, 4), "strict": round(sum(strict) / n, 4), "lenient": round(sum(lenient) / n, 4),
            "ids_done": sum(1 for sc, _ in id_rows if sc >= 1.0),
            "ids_untouched": sum(1 for sc, _ in id_rows if sc == 0.0),
            "checks": totals, "aspects": aspect_list,
            "lowest": low[:args.missing_cap], "lowest_more": max(0, len(low) - args.missing_cap),
            "notes": notes or [],
        })

    def state(present: bool, verified: bool) -> int:
        return VERIFIED if verified else PRESENT if present else ABSENT

    # ---- blocks -----------------------------------------------------------------------------------------------
    blocks_report = load_json(reports / "blocks.json") or {}
    block_entities = set(reg("minecraft:block_entity_type"))
    reg_ok = loader("block_registry")
    block_loot = loader("block_loot")
    sound_groups = {e.split(".")[1] for e in sound_events if e.startswith("block.") and e.endswith(".break")}
    rules = re.findall(r'\{\s*EMatch::(\w+),\s*TEXT\("([^"]*)"\),\s*TEXT\("([^"]*)"\)\s*\}',
                       tree.files.get("Source/McCore/Private/McSound.cpp", ""))

    def sound_group(i):
        for kind, text, grp in rules:
            if (kind == "Exact" and i == text) or (kind == "Prefix" and i.startswith(text)) or \
               (kind == "Suffix" and i.endswith(text)) or (kind == "Contains" and text in i):
                return grp
        return None

    A = {
        "model": Aspect("model", "Model or texture",
                        "blockstates/<id>.json or textures/block/<id>.png in the client assets, and the registry loads the block from blocks.json"),
        "states": Aspect("states", "States and placement",
                         "applies to blocks with properties; present if McBlockPlacement.cpp names the block, or every property name is named in placement or block-behaviour code"),
        "behaviour": Aspect("behaviour", "Behaviour",
                            "applies to blocks with a non-shape property (age, power, lit, open...) or a block entity; present if block-behaviour code (McSim, McWorld, block use/break, containers) names the block, a tag it is in, or its id parts"),
        "drops": Aspect("drops", "Drops",
                        "applies when loot_table/blocks/<id>.json exists; present if the block-break code reads block loot tables (and the DataCoverage test did not reject the table)"),
        "sounds": Aspect("sounds", "Sounds",
                         "the port's block sound rules (McSound.cpp) pick a group; absent when they fall back to stone while the sound registry has a block.<part>.* group matching the id"),
    }
    placement, behaviour = group("placement"), group("block_behaviour")
    beh_lits = behaviour.literals
    per = {}
    for i in block_ids:
        info = blocks_report.get(NS + i, {})
        props = set((info.get("properties") or {}).keys())
        has_model = any((assets / p.format(i)).is_file() for p in ("blockstates/{}.json", "textures/block/{}.png"))
        st = {"model": state(bool(reg_ok) and (has_model or bool(group("render").named("block", i))),
                             ev.for_aspect("model", i) is not None or i in ev.plan_ids["model"])}
        if props:
            ok = bool(placement.named("block", i)) or all(p in placement.literals or p in beh_lits for p in props)
            st["states"] = state(ok, ev.for_aspect("states", i) is not None or i in ev.ref_blocks)
        named_beh = behaviour.named("block", i)
        if (props - STRUCTURAL_PROPS) or i in block_entities:
            st["behaviour"] = state(bool(named_beh), ev.for_aspect("behaviour", i) is not None
                                    or i in ev.ref_blocks or i in ev.plan_ids["behaviour"])
        if (data_root / "loot_table" / "blocks" / f"{i}.json").is_file():
            rejected = f"blocks/{i}" in coverage_fail.get("loot_table", set())
            st["drops"] = state(bool(block_loot) and not rejected, ev.for_aspect("drops", i) is not None)
        if i not in ("air", "cave_air", "void_air"):
            grp = sound_group(i)
            bits = i.split("_")
            specific = {"_".join(bits[k:]) for k in range(len(bits))} & sound_groups - {"stone"}
            ok = grp is not None or not specific
            st["sounds"] = state(ok, ev.for_aspect("sounds", i) is not None)
        per[i] = st
    finish("block", "Blocks", "content", list(A.values()), per)

    # ---- items ------------------------------------------------------------------------------------------------
    item_reg_ok = loader("item_registry")
    obtainable: dict[str, set[str]] = collections.defaultdict(set)
    sources_ok = {"recipe": loader("recipes"), "loot": loader("loot"), "trade": loader("trades")}
    for kind, folder in (("recipe", "recipe"), ("loot", "loot_table"), ("trade", "villager_trade")):
        root = data_root / folder
        if root.is_dir():
            for path in root.rglob("*.json"):
                found = set()
                d = load_json(path)
                if kind == "recipe" and isinstance(d, dict):
                    walk_items(d.get("result"), found)
                elif kind == "trade" and isinstance(d, dict):
                    walk_items(d.get("gives"), found)
                else:
                    walk_items(d, found)
                for i in found:
                    obtainable[i].add(kind)
    comp_dir = reports / "minecraft" / "components" / "item"
    use = group("item_use")
    IA = {
        "model": Aspect("model", "Model", "items/<id>.json or textures/item/<id>.png in the client assets, and the registry loads the item"),
        "use": Aspect("use", "Use",
                      "applies to items with an active component (food, tool, weapon, equippable...) or code of their own; present if player/item/entity code names the item, a tag it is in or its id parts, or names every active component it has"),
        "obtain": Aspect("obtain", "How it is obtained",
                         "applies when a recipe result, loot table entry or villager trade gives the item; present if the port loads that kind of data"),
    }
    fam_for("item", item_ids)
    per = {}
    for i in item_ids:
        has_model = any((assets / p.format(i)).is_file() for p in ("items/{}.json", "textures/item/{}.png"))
        st = {"model": state(bool(item_reg_ok) and (has_model or bool(group("render").named("item", i))),
                             ev.for_aspect("model", i) is not None)}
        comps = {strip_ns(c) for c in ((load_json(comp_dir / f"{i}.json") or {}).get("components") or {})}
        active = comps & ACTIVE_COMPONENTS
        named_use = use.named("item", i)
        if active or named_use:
            ok = bool(named_use) or all(c in use.literals for c in active)
            st["use"] = state(ok, ev.for_aspect("use", i) is not None)
        ways = obtainable.get(i, set())
        if ways:
            st["obtain"] = state(any(sources_ok[w] for w in ways), ev.for_aspect("obtain", i) is not None)
        per[i] = st
    finish("item", "Items", "content", list(IA.values()), per)

    # ---- entities ---------------------------------------------------------------------------------------------
    spawn_ids = set()
    biome_dir = data_root / "worldgen" / "biome"
    for path in biome_dir.glob("*.json") if biome_dir.is_dir() else []:
        d = load_json(path) or {}
        spawns = (((d.get("attributes") or {}).get("minecraft:gameplay/natural_mob_spawns") or {})
                  .get("argument") or {}).get("spawns_by_category") or {}
        for entries in spawns.values():
            for e in entries:
                if isinstance(e, dict) and isinstance(e.get("type"), str):
                    spawn_ids.add(strip_ns(e["type"]))
    for path in (data_root / "worldgen" / "structure").glob("*.json"):
        for cat in ((load_json(path) or {}).get("spawn_overrides") or {}).values():
            for e in cat.get("spawns", []) if isinstance(cat, dict) else []:
                if isinstance(e, dict) and isinstance(e.get("type"), str):
                    spawn_ids.add(strip_ns(e["type"]))
    natural = loader("natural_spawns")
    entity_sound_ids = {e.split(".")[1] for e in sound_events if e.startswith("entity.") and e.count(".") >= 2}
    audio_entity_prefixes = {l.split(".")[1] for l in group("audio").literals if l.startswith("entity.") and l.count(".") >= 2}
    loot_ok = loader("loot")
    mob_sound_rows = set(re.findall(r'\{\s*TEXT\("([a-z_]+)"\),\s*(?:true|false)',
                                    tree.files.get("Source/McCore/Private/McSound.cpp", "")))
    EA = {
        "model": Aspect("model", "Model and animation", "the entity renderer (McRender) names the entity, a tag it is in or its id parts"),
        "ai": Aspect("ai", "AI or behaviour", "entity code (McEntity, the server's entity host) names the entity, a tag it is in or its id parts"),
        "drops": Aspect("drops", "Drops", "applies when loot_table/entities/<id>.json exists; present if loot tables load and the entity's AI is present"),
        "sounds": Aspect("sounds", "Sounds", "applies when the sound registry has entity.<id>.* events; present if the mob sound table (McSound.cpp) lists it or audio code names it"),
        "spawning": Aspect("spawning", "Spawning", "applies to entities in a biome's natural spawns or a structure's spawn overrides; present if the port reads natural_mob_spawns and the entity's AI is present, or spawning code names it"),
    }
    fam_for("entity_type", entity_ids)
    model_g, ai_g, spawn_g, audio_g = group("entity_model"), group("entity_ai"), group("spawning"), group("audio")
    per = {}
    for i in entity_ids:
        ai_named = bool(ai_g.named("entity_type", i))
        st = {"model": state(bool(model_g.named("entity_type", i)), ev.for_aspect("model", i) is not None
                             or i in ev.plan_ids["model"]),
              "ai": state(ai_named, ev.for_aspect("ai", i) is not None or i in ev.plan_ids["behaviour"])}
        if (data_root / "loot_table" / "entities" / f"{i}.json").is_file():
            st["drops"] = state(bool(loot_ok) and ai_named, ev.for_aspect("drops", i) is not None)
        if i in entity_sound_ids:
            ok = i in mob_sound_rows or bool(audio_g.named("entity_type", i, parts=False)) or \
                i in audio_entity_prefixes
            st["sounds"] = state(ok, ev.for_aspect("sounds", i) is not None)
        if i in spawn_ids:
            ok = (bool(natural) and ai_named) or bool(spawn_g.named("entity_type", i))
            st["spawning"] = state(ok, ev.for_aspect("spawning", i) is not None)
        per[i] = st
    finish("entity_type", "Entities", "content", list(EA.values()), per)

    # ---- commands: every branch ------------------------------------------------------------------------------
    cmd_tree = load_json(reports / "commands.json")
    if cmd_tree:
        cg = group("commands")
        cmd_words = set(cg.literals)
        for l in cg.literals:
            cmd_words.update(l.lstrip("/").split())
        test_cmds = []
        for l in ev.test_literals:
            toks = l.lstrip("/").split()
            if toks:
                test_cmds.append(toks)
        per, total_paths = {}, 0
        CA = [Aspect("branch", "Syntax branches",
                     "each executable path of the command in commands.json is one check; present if the command and every literal word on the path are named in McCommands; verified if a passing test runs a command line with those words in order")]
        for name, node in sorted((cmd_tree.get("children") or {}).items()):
            if node.get("type") != "literal":
                continue
            paths = []
            command_paths(dict(node, _name=name), [], paths)
            total_paths += len(paths)
            states = {}
            for n, path in enumerate(paths):
                words = [w for t, w in path if t == "literal"]
                ok = name in cg.literals and all(w in cmd_words for w in words)
                verified = any(toks[0] == name and _subseq(words, toks) for toks in test_cmds)
                states[n] = state(ok, verified)
            per[name] = states
        _finish_commands(categories, per, CA[0], args.missing_cap)
    else:
        skipped.append({"key": "command", "reason": "commands.json missing"})

    # ---- simple registries: one aspect (named) -----------------------------------------------------------------
    all_g = group("all")
    for key, regname, label in REGISTRY_SIMPLE:
        ids = reg(regname)
        if not ids:
            skipped.append({"key": key, "reason": f"{regname} not in registries.json"})
            continue
        fam_for(key, ids)
        asp = Aspect("named", "Named in game code", "the id, or its id parts, is a string literal in game source (not tests)")
        per = {i: {"named": state(bool(all_g.named(key, i)), ev.any_test(i))} for i in ids}
        finish(key, label, "content", [asp], per)

    # ---- data folders ------------------------------------------------------------------------------------------
    for key, folder, label, loader_rx, check in DATA_CATEGORIES:
        path = data_root / folder
        ids = sorted(p.relative_to(path).with_suffix("").as_posix() for p in path.rglob("*.json")) if path.is_dir() else []
        if not ids:
            skipped.append({"key": key, "reason": f"data folder {folder}/ missing or empty in the cache"})
            continue
        fam_for(key, ids)
        where = None
        if loader_rx:
            rx = re.compile(loader_rx)
            where = next((rel for rel, text in tree.sources(tests=False) if rx.search(text)), None)
        failed = coverage_fail.get(key, set())
        if where:
            rule = f"loaded from data/{folder}/ at runtime ({Path(where).name})"
            if check:
                rule += ", and every " + ("criterion trigger" if check == "criteria_triggers" else "type") + " it uses is named in game source"
        else:
            rule = "named as a string literal in game source (no runtime loader found for the folder)"
        asp = Aspect("loaded", "Loaded or named", rule)
        per = {}
        for i in ids:
            if where:
                ok = i not in failed
                if ok and check and not all_g.named(key, i, parts=False):
                    d = load_json(path / f"{i}.json") or {}
                    needed = set()
                    if check == "type" and isinstance(d.get("type"), str):
                        needed.add(strip_ns(d["type"]))
                    elif check == "criteria_triggers":
                        for crit in (d.get("criteria") or {}).values():
                            if isinstance(crit, dict) and isinstance(crit.get("trigger"), str):
                                needed.add(strip_ns(crit["trigger"]))
                    ok = all(t in all_g.literals for t in needed)
            else:
                ok = bool(all_g.named(key, i))
            per[i] = {"loaded": state(ok, ev.any_test(i))}
        finish(key, label, "data", [asp], per)

    # ---- systems ----------------------------------------------------------------------------------------------
    systems = []
    sys_doc = load_json(Path(args.systems)) or {"systems": []}
    for s in sys_doc.get("systems", []):
        status = s.get("status", "unknown")
        notes = []
        if status == "verified":
            need = s.get("verify_tests", [])
            passed = need and all(results.get(t) is True for t in need)
            if not passed:
                status = "present"
                notes.append("claimed verified, but " + ("no test is listed" if not need else
                             "its tests did not all pass in a test log matched to this commit") + "; counted as present")
        value = {"verified": 1.0, "present": 0.5}.get(status, 0.0)
        systems.append({"key": s["key"], "label": s.get("label", s["key"]), "status": status, "score": value,
                        "evidence": s.get("evidence", ""), "notes": notes})

    # ---- headline ---------------------------------------------------------------------------------------------
    weights = load_json(Path(args.weights)) or {}
    wc, ws = weights.get("content", {}), weights.get("systems", {})
    parts = [(float(wc.get(c["key"], 1)), c["score"], c["strict"], c["lenient"]) for c in categories]
    parts += [(float(ws.get(s["key"], 1)), s["score"], 1.0 if s["status"] == "verified" else 0.0,
               1.0 if s["status"] in ("present", "verified") else 0.0) for s in systems]
    wsum = sum(w for w, *_ in parts) or 1.0
    pct = lambda idx: round(100.0 * sum(p[0] * p[idx] for p in parts) / wsum, 2)
    content_w = sum(float(wc.get(c["key"], 1)) for c in categories)
    for c in categories:
        c["weight"] = float(wc.get(c["key"], 1))
    for s in systems:
        s["weight"] = float(ws.get(s["key"], 1))
    checks = {k: sum(c["checks"][k] for c in categories) for k in ("applies", "absent", "present", "verified")}
    now = dt.datetime.now(dt.timezone.utc).isoformat(timespec="seconds")
    return {
        "schema": SCHEMA, "generated": now, "commit": commit,
        "headline": {"one_to_one_pct": pct(1), "strict_pct": pct(2), "lenient_pct": pct(3),
                     "formula": "weighted mean over categories and systems of (verified + 0.5 x present) / checks",
                     "content_weight_share_pct": round(100.0 * content_w / wsum, 1)},
        "left": {"checks": checks["applies"], "absent": checks["absent"],
                 "present_not_verified": checks["present"], "verified": checks["verified"],
                 "ids": sum(c["ids"] for c in categories), "ids_done": sum(c["ids_done"] for c in categories),
                 "ids_untouched": sum(c["ids_untouched"] for c in categories),
                 "systems": len(systems), "systems_verified": sum(1 for s in systems if s["status"] == "verified"),
                 "systems_absent_or_unknown": sum(1 for s in systems if s["status"] in ("absent", "unknown"))},
        "evidence": {"test_log": log_info, "ref_logs": ref_info,
                     "plan_citations": {"sentences": ev.plan_lines, "model_ids": len(ev.plan_ids["model"]), "behaviour_ids": len(ev.plan_ids["behaviour"])}},
        "categories": categories, "systems": systems, "skipped": skipped,
    }


def _subseq(words, toks):
    """The path's literal words after the command name appear in this order among the command line's words."""
    words = words[1:]
    it = iter(toks[1:])
    return all(any(w == t for t in it) for w in words)


def _finish_commands(categories, per, aspect, cap):
    """Commands: each executable branch is one check; a command's score is the mean of its branches."""
    scores, strict, lenient, rows = [], [], [], []
    totals = {"applies": 0, "absent": 0, "present": 0, "verified": 0}
    missing = []
    for name, states in per.items():
        vals = list(states.values())
        if not vals:
            continue
        sc = sum(VALUE[v] for v in vals) / len(vals)
        scores.append(sc)
        strict.append(sum(1 for v in vals if v == VERIFIED) / len(vals))
        lenient.append(sum(1 for v in vals if v != ABSENT) / len(vals))
        rows.append((sc, name))
        totals["applies"] += len(vals)
        for v in vals:
            totals[STATE_NAME[v]] += 1
        if all(v == ABSENT for v in vals):
            missing.append(name)
    n = len(scores) or 1
    low = [i for sc, i in sorted(rows) if sc < 1.0]
    categories.append({
        "key": "command", "label": "Commands", "group": "content", "ids": len(scores),
        "score": round(sum(scores) / n, 4), "strict": round(sum(strict) / n, 4), "lenient": round(sum(lenient) / n, 4),
        "ids_done": sum(1 for sc, _ in rows if sc >= 1.0), "ids_untouched": sum(1 for sc, _ in rows if sc == 0.0),
        "checks": totals,
        "aspects": [dict(key=aspect.key, label=aspect.label, rule=aspect.rule, **totals,
                         missing=sorted(missing)[:cap], missing_more=max(0, len(missing) - cap))],
        "lowest": low[:cap], "lowest_more": max(0, len(low) - cap),
        "notes": ["the missing list holds commands with no branch present"],
    })


def with_history(result: dict, out: Path) -> dict:
    history = []
    old = load_json(out) if out.is_file() else None
    if isinstance(old, dict):
        history = [h for h in old.get("history", []) if "one_to_one" in h]  # points from schema 1 had no headline
    h, left = result["headline"], result["left"]
    entry = {"t": result["generated"], "commit": result["commit"]["hash"][:10], "one_to_one": h["one_to_one_pct"],
             "strict": h["strict_pct"], "lenient": h["lenient_pct"], "absent": left["absent"],
             "present": left["present_not_verified"], "verified": left["verified"]}
    keys = ("commit", "one_to_one", "strict", "lenient", "absent", "present", "verified")
    if history and all(history[-1].get(k) == entry[k] for k in keys):
        history[-1]["t"] = entry["t"]
    else:
        history.append(entry)
    result["history"] = history[-HISTORY_CAP:]
    return result


def main() -> int:
    p = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    p.add_argument("--repo", default=str(HERE.parent.parent), help="repository root (default: this checkout)")
    p.add_argument("--ref", help="audit this git revision instead of the working tree, e.g. m1/blocks-and-chunks")
    p.add_argument("--cache", help="cache folder (default: <repo>/cache, else the main checkout's cache)")
    p.add_argument("--test-log", help="automation log (default: <main checkout>/Saved/Remote/run_tests.log if it exists)")
    p.add_argument("--test-log-commit", help="the commit the log's run built (default: <log>.commit, else inferred)")
    p.add_argument("--test-log-note", help="free text stored with the log")
    p.add_argument("--no-test-log", action="store_true", help="use no test log (nothing is verified by tests)")
    p.add_argument("--systems", default=str(HERE / "systems.json"))
    p.add_argument("--weights", default=str(HERE / "weights.json"))
    p.add_argument("--out", default=str(HERE / "progress.json"), help="output file (its history is kept)")
    p.add_argument("--missing-cap", type=int, default=40, help="ids listed per missing list")
    args = p.parse_args()

    out = Path(args.out)
    result = with_history(audit(args), out)
    out.parent.mkdir(parents=True, exist_ok=True)
    tmp = out.with_suffix(out.suffix + ".tmp")
    tmp.write_text(json.dumps(result, indent=1) + "\n", encoding="utf-8")
    os.replace(tmp, out)

    h, left, c = result["headline"], result["left"], result["commit"]
    print(f"{c['branch']} {c['hash'][:10]}: 1:1 {h['one_to_one_pct']}%  (strict {h['strict_pct']}%, lenient {h['lenient_pct']}%)")
    print(f"  checks {left['checks']}: absent {left['absent']}, present {left['present_not_verified']}, verified {left['verified']}; "
          f"ids done {left['ids_done']}/{left['ids']}, untouched {left['ids_untouched']}")
    print(f"  {'category':<22}{'ids':>6}{'score':>8}{'strict':>8}{'lenient':>9}{'absent':>8}{'present':>9}{'verified':>9}  weight")
    for cat in result["categories"]:
        k = cat["checks"]
        print(f"  {cat['label']:<22}{cat['ids']:>6}{100 * cat['score']:>7.1f}%{100 * cat['strict']:>7.1f}%{100 * cat['lenient']:>8.1f}%"
              f"{k['absent']:>8}{k['present']:>9}{k['verified']:>9}  {cat['weight']:g}")
    for s in result["systems"]:
        print(f"  system {s['label']:<38} {s['status']:<9} weight {s['weight']:g}")
    tl = result["evidence"]["test_log"]
    print(f"  test log: {tl}")
    print(f"  ref logs: {result['evidence']['ref_logs']}; PLAN.md citations: {result['evidence']['plan_citations']}")
    for s in result["skipped"]:
        print(f"  skipped {s['key']}: {s['reason']}")
    print(f"wrote {out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
