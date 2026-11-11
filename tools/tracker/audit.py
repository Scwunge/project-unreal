#!/usr/bin/env python3
"""Content audit for the mc-unreal port: scores every vanilla content id as absent, present or verified.

Python 3 standard library only. Reads:
  - cache/data/reports/registries.json and commands.json (the vanilla server's own data generator output),
  - the data folders extracted under cache/client/data/minecraft (json/nbt file names, plus a few "type" fields),
  - the port's C++ source (Source/**/*.cpp|h) and tools/refscripts/expect/*,
  - optionally an Unreal automation log (tools/run_tests.ps1 writes Saved/Logs/run_tests.log).
It never opens the Minecraft jar. The output holds ids and counts only.

Usage:
  python tools/tracker/audit.py                              # audit the working tree this script sits in
  python tools/tracker/audit.py --ref m1/blocks-and-chunks   # audit a committed revision (no checkout needed)
  python tools/tracker/audit.py --test-log path/to/run_tests.log --out some/folder/progress.json

The three states (each id is in exactly one):
  absent   - no sign of it in the port.
  present  - the port names it or loads it (rules per category below; see TRACKER.md for the limits).
  verified - a passing automation test or a vanilla comparison log names it. Needs --test-log; without a log of
             a run, nothing is verified, because a test that exists has not been shown to pass.
"""

from __future__ import annotations

import argparse
import datetime as dt
import json
import os
import re
import subprocess
import sys
from pathlib import Path

SCHEMA = 1
HISTORY_CAP = 2000
DEFAULT_MISSING_CAP = 60

# ----------------------------------------------------------------------------------------------------------------
# Categories. "rule" says how present is decided:
#   named      - the id is a string literal in non-test source ("creeper" or "minecraft:creeper").
#   registered - blocks/items: the registry loads every id from the reports (checked by `loader`), and the id
#                has a model or texture in the client assets, or is named in source.
#   loaded     - a data folder the source enumerates or reads by id (checked by `loader`); every file in it counts,
#                unless `type_check` finds a type the source never names. Falls back to "named" if the loader
#                pattern is no longer found in the source.
# ----------------------------------------------------------------------------------------------------------------

REGISTRY_CATEGORIES = [
    # key, registry, label, rule
    ("block", "minecraft:block", "Blocks", "registered"),
    ("item", "minecraft:item", "Items", "registered"),
    ("entity_type", "minecraft:entity_type", "Entities", "named"),
    ("block_entity_type", "minecraft:block_entity_type", "Block entities", "named"),
    ("mob_effect", "minecraft:mob_effect", "Mob effects", "named"),
    ("potion", "minecraft:potion", "Potions", "named"),
    ("particle_type", "minecraft:particle_type", "Particles", "named"),
    ("menu", "minecraft:menu", "Menus (screens)", "named"),
    ("custom_stat", "minecraft:custom_stat", "Statistics", "named"),
    ("attribute", "minecraft:attribute", "Attributes", "named"),
    ("villager_profession", "minecraft:villager_profession", "Villager professions", "named"),
    ("game_event", "minecraft:game_event", "Game events", "named"),
]

# The registry loaders that make the "registered" rule apply: (path suffix, regex that must match in that file).
REGISTERED_LOADERS = {
    "block": ("Source/McCore/Private/McBlockRegistry.cpp", r'McCache::Report\(TEXT\("blocks\.json"\)\)'),
    "item": ("Source/McItem/Private/McItemRegistry.cpp", r'McCache::Report\(TEXT\("registries\.json"\)\)'),
}
# Where a block's or item's model/texture would come from (client assets, file names only).
ASSET_CHECKS = {
    "block": ["blockstates/{id}.json", "textures/block/{id}.png"],
    "item": ["items/{id}.json", "textures/item/{id}.png"],
}

DATA_CATEGORIES = [
    # key, folder under cache/client/data/minecraft, extension, label, loader regex (any non-test source), type_check
    ("advancement", "advancement", ".json", "Advancements",
     r'McData::List\(TEXT\("advancement"\)\)', ("criteria_triggers", "minecraft:trigger_type")),
    ("recipe", "recipe", ".json", "Recipes",
     r'McData::List\(TEXT\("recipe"\)\)', ("type", "minecraft:recipe_serializer")),
    ("loot_table", "loot_table", ".json", "Loot tables",
     r'McData::PathOf\(\w+,\s*TEXT\("loot_table"\)', None),
    ("enchantment", "enchantment", ".json", "Enchantments",
     r'McCache::Data\(TEXT\("enchantment/\*\.json"\)\)', None),
    ("biome", "worldgen/biome", ".json", "Biomes",
     r'McCache::Data\(TEXT\("worldgen/biome/\*\.json"\)\)', None),
    ("structure", "worldgen/structure", ".json", "Structures",
     r'McCache::Data\(TEXT\("worldgen/structure/\*\.json"\)\)', ("type", "minecraft:worldgen/structure_type")),
    ("damage_type", "damage_type", ".json", "Damage types", None, None),
    ("painting_variant", "painting_variant", ".json", "Paintings", None, None),
    ("jukebox_song", "jukebox_song", ".json", "Music discs", None, None),
]

# "DataCoverage: <what>: <id>" lines in a test log list data files a loader rejected; those ids go back to absent.
DATA_COVERAGE_FAILURES = {
    "loot tables that did not load": "loot_table",
    "recipe files that did not load (unknown type or malformed)": "recipe",
}

# The vanilla comparison logs and the automation test that must pass for them to count.
REF_LOGS = [
    ("tools/refscripts/expect/rs_", "McClient.McSim.RefScripts"),
    ("tools/refscripts/expect/lg_", "McClient.McSim.RefScriptsLogistics"),
]

TEST_MACRO = re.compile(
    r'(?:IMPLEMENT_(?:SIMPLE|COMPLEX|CUSTOM_SIMPLE|CUSTOM_COMPLEX)_AUTOMATION_TEST|BEGIN_DEFINE_SPEC|DEFINE_SPEC)'
    r'\s*\(\s*\w+\s*,\s*(?:\w+\s*,\s*)?"([^"]+)"')
NAMESPACE = "minecraft:"


# ----------------------------------------------------------------------------------------------------------------
# Reading the repository: the working tree, or a git revision.
# ----------------------------------------------------------------------------------------------------------------

def git(repo: Path, *args: str, binary: bool = False, stdin: bytes | None = None):
    out = subprocess.run(["git", "-C", str(repo), *args], input=stdin, capture_output=True, check=True)
    return out.stdout if binary else out.stdout.decode("utf-8", "replace").strip()


class Tree:
    """The files the audit reads, from disk or from `git cat-file` at a revision."""

    PREFIXES = ("Source/", "tools/refscripts/")

    def __init__(self, repo: Path, ref: str | None):
        self.repo = repo
        self.ref = ref
        self.files: dict[str, str] = {}
        if ref:
            self._load_ref()
        else:
            self._load_disk()

    def _keep(self, rel: str) -> bool:
        if rel.startswith("Source/"):
            return rel.endswith((".cpp", ".h"))
        return rel.startswith("tools/refscripts/expect/")

    def _load_disk(self) -> None:
        for prefix in self.PREFIXES:
            base = self.repo / prefix
            if not base.is_dir():
                continue
            for path in base.rglob("*"):
                rel = path.relative_to(self.repo).as_posix()
                if path.is_file() and self._keep(rel):
                    self.files[rel] = path.read_text(encoding="utf-8", errors="replace")

    def _load_ref(self) -> None:
        names = git(self.repo, "ls-tree", "-r", "--name-only", self.ref, "--", *self.PREFIXES).splitlines()
        names = [n for n in names if self._keep(n)]
        if not names:
            return
        request = "".join(f"{self.ref}:{n}\n" for n in names).encode("utf-8")
        data = git(self.repo, "cat-file", "--batch", binary=True, stdin=request)
        pos = 0
        for name in names:
            header_end = data.index(b"\n", pos)
            header = data[pos:header_end].split()
            size = int(header[2])
            body = data[header_end + 1: header_end + 1 + size]
            pos = header_end + 1 + size + 1
            self.files[name] = body.decode("utf-8", "replace")

    def sources(self, tests: bool | None = None):
        for rel, text in self.files.items():
            if not rel.startswith("Source/"):
                continue
            is_test = "/Tests/" in rel
            if tests is None or tests == is_test:
                yield rel, text


LEXER = re.compile(
    r'R"([^(\s"]{0,16})\((.*?)\)\1"'  # raw string: group 2
    r'|//[^\n]*'                      # line comment
    r'|/\*.*?\*/'                     # block comment
    r"|'(?:\\.|[^'\\\n])*'"           # character literal
    r'|"((?:\\.|[^"\\\n])*)"',        # string literal: group 3
    re.S)


def string_literals(text: str) -> list[str]:
    """The string literals of a C++ file, skipping comments and character literals (a lexer, not a parser)."""
    out = []
    for m in LEXER.finditer(text):
        if m.group(3) is not None:
            out.append(m.group(3))
        elif m.group(2) is not None:
            out.append(m.group(2))
    return out


def literal_ids(literals) -> set[str]:
    ids = set()
    for lit in literals:
        lit = lit.strip()
        if lit.startswith(NAMESPACE):
            lit = lit[len(NAMESPACE):]
        if lit:
            ids.add(lit)
    return ids


def camel_to_snake(name: str) -> str:
    return re.sub(r"(?<=[a-z0-9])(?=[A-Z])", "_", name).lower()


# ----------------------------------------------------------------------------------------------------------------
# Test log
# ----------------------------------------------------------------------------------------------------------------

def read_test_log(path: Path):
    """{test path: passed} from "Test Completed. Result={..} Name={..} Path={..}" lines (last result wins),
    and the DataCoverage failure lines."""
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
    """Every declared test (and every parameterised child "Path Param" of it) ran and none failed."""
    if not declared:
        return False
    for test in declared:
        hits = [ok for name, ok in results.items() if name == test or name.startswith(test + " ")]
        if not hits or not all(hits):
            return False
    return True


# ----------------------------------------------------------------------------------------------------------------
# The audit
# ----------------------------------------------------------------------------------------------------------------

def find_cache(repo: Path, explicit: str | None) -> Path:
    if explicit:
        return Path(explicit)
    if (repo / "cache" / "data" / "reports").is_dir():
        return repo / "cache"
    # A git worktree shares the main checkout's cache folder.
    common = Path(git(repo, "rev-parse", "--path-format=absolute", "--git-common-dir"))
    return common.parent / "cache"


def list_data_ids(folder: Path, ext: str) -> list[str]:
    ids = []
    for path in folder.rglob("*" + ext):
        ids.append(path.relative_to(folder).with_suffix("").as_posix())
    return sorted(ids)


def type_ids(path: Path, how: str) -> set[str]:
    """The type ids a data file depends on: its "type", or every criterion's "trigger"."""
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, ValueError):
        return {"<unreadable>"}
    found = set()
    if how == "type":
        if isinstance(data.get("type"), str):
            found.add(data["type"])
    elif how == "criteria_triggers":
        for crit in (data.get("criteria") or {}).values():
            if isinstance(crit, dict) and isinstance(crit.get("trigger"), str):
                found.add(crit["trigger"])
    return {t[len(NAMESPACE):] if t.startswith(NAMESPACE) else t for t in found}


def audit(args) -> dict:
    repo = Path(args.repo).resolve()
    tree = Tree(repo, args.ref)
    cache = find_cache(repo, args.cache)
    reports = cache / "data" / "reports"
    data_root = cache / "client" / "data" / "minecraft"
    assets_root = cache / "client" / "assets" / "minecraft"
    if not (reports / "registries.json").is_file():
        sys.exit(f"audit: no registries.json under {reports}; run tools/fetch_cache.py first (or pass --cache).")

    rev = args.ref or "HEAD"
    commit = {
        "hash": git(repo, "rev-parse", rev),
        "branch": args.ref or git(repo, "rev-parse", "--abbrev-ref", "HEAD"),
        "date": git(repo, "show", "-s", "--format=%cI", rev),
        "subject": git(repo, "show", "-s", "--format=%s", rev)[:120],
    }
    if not args.ref:
        commit["dirty"] = bool(git(repo, "status", "--porcelain", "--", "Source", "tools/refscripts"))

    # What the source names, split into game code and tests.
    named: set[str] = set()
    for _, text in tree.sources(tests=False):
        named |= literal_ids(string_literals(text))

    # Tests: the ids each test file names, and whether all its tests passed.
    results: dict[str, bool] = {}
    coverage_fail: dict[str, set[str]] = {}
    log_info = {"used": False}
    if args.test_log:
        log_path = Path(args.test_log)
        results, coverage_fail = read_test_log(log_path)
        log_info = {
            "used": True,
            "file": log_path.name,
            "modified": dt.datetime.fromtimestamp(log_path.stat().st_mtime, dt.timezone.utc).isoformat(timespec="seconds"),
            "tests_passed": sum(1 for ok in results.values() if ok),
            "tests_failed": sum(1 for ok in results.values() if not ok),
            "note": args.test_log_note or "",
        }
    declared_tests: set[str] = set()
    verified_test_ids: set[str] = set()
    verified_name_ids: set[str] = set()
    for rel, text in tree.sources(tests=True):
        declared = TEST_MACRO.findall(text)
        declared_tests.update(declared)
        if results and tests_pass(results, declared):
            verified_test_ids |= literal_ids(string_literals(text))
            for test in declared:
                verified_name_ids.add(camel_to_snake(test.rsplit(".", 1)[-1]))
    # Only tests the audited revision declares count; a log may come from another build.
    if results:
        log_info["tests_matched"] = sum(1 for t in declared_tests if t in results)

    # Vanilla comparison logs: block names in the states columns ("tick|x,y,z|state|state").
    verified_ref_blocks: set[str] = set()
    ref_info = []
    for prefix, test in REF_LOGS:
        logs = [rel for rel in tree.files if rel.startswith(prefix)]
        passed = results.get(test) is True and test in declared_tests
        ref_info.append({"logs": len(logs), "test": test, "passed": passed})
        if not passed:
            continue
        for rel in logs:
            for line in tree.files[rel].splitlines():
                parts = line.split("|")
                for state in parts[2:4]:
                    name = state.split("[", 1)[0].strip()
                    if name and name != "-":
                        verified_ref_blocks.add(name[len(NAMESPACE):] if name.startswith(NAMESPACE) else name)

    def verified(cat: str, i: str) -> str | None:
        if cat == "block" and i in verified_ref_blocks:
            return "ref_log"
        if i in verified_test_ids:
            return "test"
        if i in verified_name_ids:
            return "test_name"
        return None

    def file_has(suffix: str, pattern: str) -> bool:
        text = tree.files.get(suffix)
        return bool(text and re.search(pattern, text))

    def any_source_has(pattern: str) -> str | None:
        rx = re.compile(pattern)
        for rel, text in tree.sources(tests=False):
            if rx.search(text):
                return rel
        return None

    categories = []
    skipped = []

    def score(key, label, group, ids, present_fn, rule_text):
        buckets = {"absent": [], "present": [], "verified": []}
        how = {"test": 0, "test_name": 0, "ref_log": 0}
        named_count = 0
        for i in ids:
            if i in named:
                named_count += 1
            v = verified(key, i)
            if v:
                buckets["verified"].append(i)
                how[v] += 1
            elif present_fn(i):
                buckets["present"].append(i)
            else:
                buckets["absent"].append(i)
        missing = sorted(buckets["absent"])
        categories.append({
            "key": key, "label": label, "group": group, "ids": len(ids),
            "absent": len(buckets["absent"]), "present": len(buckets["present"]), "verified": len(buckets["verified"]),
            "named_in_source": named_count,
            "verified_by": {k: v for k, v in how.items() if v},
            "rule": rule_text,
            "missing": missing[:args.missing_cap],
            "missing_more": max(0, len(missing) - args.missing_cap),
        })

    # Registries.
    registries = json.loads((reports / "registries.json").read_text(encoding="utf-8"))
    for key, reg, label, rule in REGISTRY_CATEGORIES:
        if reg not in registries:
            skipped.append({"key": key, "reason": f"{reg} not in registries.json"})
            continue
        ids = sorted(e[len(NAMESPACE):] if e.startswith(NAMESPACE) else e for e in registries[reg]["entries"])
        if rule == "registered":
            suffix, pattern = REGISTERED_LOADERS[key]
            if file_has(suffix, pattern):
                checks = ASSET_CHECKS[key]

                def present(i, checks=checks):
                    return i in named or any((assets_root / c.format(id=i)).is_file() for c in checks)
                text = (f"registers from the reports ({Path(suffix).name}) and has "
                        f"{' or '.join(c.replace('{id}', '<id>') for c in checks)} in the client assets, or is named in source")
                score(key, label, "registry", ids, present, text)
                continue
            rule = "named"  # the loader moved; fall back
        score(key, label, "registry", ids, lambda i: i in named, "named as a string literal in game source (not tests)")

    # Commands.
    commands_path = reports / "commands.json"
    if commands_path.is_file():
        tree_json = json.loads(commands_path.read_text(encoding="utf-8"))
        ids = sorted(k for k, v in (tree_json.get("children") or {}).items() if v.get("type") == "literal")
        score("command", "Commands", "commands", ids, lambda i: i in named,
              "top-level command named as a string literal in game source")
    else:
        skipped.append({"key": "command", "reason": "commands.json missing"})

    # Data folders.
    for key, folder, ext, label, loader, check in DATA_CATEGORIES:
        path = data_root / folder
        if not path.is_dir():
            skipped.append({"key": key, "reason": f"data folder {folder}/ not in the cache"})
            continue
        ids = list_data_ids(path, ext)
        if not ids:
            skipped.append({"key": key, "reason": f"data folder {folder}/ has no {ext} files"})
            continue
        where = any_source_has(loader) if loader else None
        if where:
            if check:
                how, _reg = check

                def present(i, path=path, ext=ext, how=how):
                    if i in named:
                        return True
                    needed = type_ids(path / (i + ext), how)
                    return all(t in named for t in needed)
                text = (f"loaded from data/{folder}/ at runtime ({Path(where).name}), and every "
                        f"{'criterion trigger' if how == 'criteria_triggers' else 'type'} it uses is named in source")
            else:
                def present(i):
                    return True
                text = f"loaded from data/{folder}/ at runtime ({Path(where).name})"
            failed = coverage_fail.get(key, set())
            if failed:
                base = present

                def present(i, base=base, failed=failed):
                    return i not in failed and base(i)
                text += "; ids the DataCoverage test reports as not loading are absent"
        else:
            def present(i):
                return i in named
            text = "named as a string literal in game source (no runtime loader found for the folder)"
        score(key, label, "data", ids, present, text)

    totals = {k: sum(c[k] for c in categories) for k in ("ids", "absent", "present", "verified")}
    mean = lambda field: round(100.0 * sum(c[field] / c["ids"] for c in categories) / len(categories), 2)
    now = dt.datetime.now(dt.timezone.utc).isoformat(timespec="seconds")
    return {
        "schema": SCHEMA,
        "generated": now,
        "commit": commit,
        "source_files": sum(1 for _ in tree.sources()),
        "test_log": log_info,
        "ref_logs": ref_info,
        "totals": totals,
        "category_mean_pct": {
            "present_or_verified": round(mean("present") + mean("verified"), 2),
            "verified": mean("verified"),
        },
        "categories": categories,
        "skipped": skipped,
    }


def with_history(result: dict, out: Path) -> dict:
    history = []
    if out.is_file():
        try:
            history = json.loads(out.read_text(encoding="utf-8")).get("history", [])
        except ValueError:
            history = []
    t = result["totals"]
    entry = {"t": result["generated"], "commit": result["commit"]["hash"][:10],
             "ids": t["ids"], "absent": t["absent"], "present": t["present"], "verified": t["verified"]}
    same = lambda a, b: all(a.get(k) == b.get(k) for k in ("commit", "ids", "absent", "present", "verified"))
    if history and same(history[-1], entry):
        history[-1]["t"] = entry["t"]  # nothing changed: move the last point's time instead of adding a copy
    else:
        history.append(entry)
    result["history"] = history[-HISTORY_CAP:]
    return result


def main() -> int:
    here = Path(__file__).resolve().parent
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--repo", default=str(here.parent.parent), help="repository root (default: this checkout)")
    parser.add_argument("--ref", help="audit this git revision instead of the working tree, e.g. m1/blocks-and-chunks")
    parser.add_argument("--cache", help="cache folder (default: <repo>/cache, else the main checkout's cache)")
    parser.add_argument("--test-log", help="Unreal automation log of a run of this revision (run_tests.log)")
    parser.add_argument("--test-log-note", help="free text stored with the log, e.g. which build produced it")
    parser.add_argument("--out", default=str(here / "progress.json"), help="output file (its history is kept)")
    parser.add_argument("--missing-cap", type=int, default=DEFAULT_MISSING_CAP, help="missing ids listed per category")
    args = parser.parse_args()

    out = Path(args.out)
    result = with_history(audit(args), out)
    out.parent.mkdir(parents=True, exist_ok=True)
    tmp = out.with_suffix(out.suffix + ".tmp")
    tmp.write_text(json.dumps(result, indent=1) + "\n", encoding="utf-8")
    os.replace(tmp, out)

    t = result["totals"]
    print(f"{result['commit']['branch']} {result['commit']['hash'][:10]}: {t['ids']} ids, "
          f"{t['present'] + t['verified']} present or verified ({t['verified']} verified), {t['absent']} absent")
    for c in result["categories"]:
        print(f"  {c['label']:<22} {c['ids']:>5}  absent {c['absent']:>5}  present {c['present']:>5}  verified {c['verified']:>4}")
    for s in result["skipped"]:
        print(f"  skipped {s['key']}: {s['reason']}")
    print(f"wrote {out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
