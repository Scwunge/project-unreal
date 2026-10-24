"""Fill cache/ from your own Minecraft install (M0).

Reads, for one version:
  - the launcher profile   .minecraft/versions/<v>/<v>.json
  - the client jar         .minecraft/versions/<v>/<v>.jar      -> cache/client/assets/
  - the assets index       .minecraft/assets/indexes/<id>.json  -> cache/assets/
  - the vanilla server jar cache/server/server.jar             -> protocol number, cache/data/reports/

The server jar is only downloaded when you pass --download-server. Nothing here is committed: cache/ is
gitignored. Run again at any time; finished steps are skipped unless their input changed.

Exit codes: 0 everything done, 2 something is still missing (the script says what), 1 error.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import urllib.request
import zipfile
from datetime import datetime, timezone
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
DEFAULT_VERSION = "26.3"


def log(msg: str) -> None:
    print(msg, flush=True)


def sha1_of(path: Path) -> str:
    h = hashlib.sha1()
    with path.open("rb") as f:
        for block in iter(lambda: f.read(1 << 20), b""):
            h.update(block)
    return h.hexdigest()


def read_stamp(path: Path) -> dict:
    try:
        return json.loads(path.read_text(encoding="utf-8"))
    except (OSError, ValueError):
        return {}


def write_json(path: Path, data: dict) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(data, indent=2) + "\n", encoding="utf-8")


def default_mc_dir() -> Path:
    appdata = os.environ.get("APPDATA")
    if appdata:
        return Path(appdata) / ".minecraft"
    return Path.home() / ".minecraft"


# --- Java -------------------------------------------------------------------------------------------

def java_major(java: Path) -> int | None:
    try:
        out = subprocess.run([str(java), "-version"], capture_output=True, text=True, timeout=30)
    except (OSError, subprocess.TimeoutExpired):
        return None
    m = re.search(r'version "(\d+)', out.stderr + out.stdout)
    return int(m.group(1)) if m else None


def find_java(mc_dir: Path, component: str, major: int, explicit: str | None) -> Path | None:
    candidates: list[Path] = []
    if explicit:
        candidates.append(Path(explicit))
    if os.environ.get("MC_JAVA"):
        candidates.append(Path(os.environ["MC_JAVA"]))
    # The launcher's own runtime, in both places the Windows launchers keep it.
    runtime_roots = [mc_dir / "runtime"]
    local = os.environ.get("LOCALAPPDATA")
    if local:
        runtime_roots += sorted(Path(local, "Packages").glob("Microsoft.4297127D64EC6*/LocalCache/Local/runtime"))
    for root in runtime_roots:
        candidates += sorted(root.glob(f"{component}/*/{component}/bin/java.exe"))
        candidates += sorted(root.glob(f"{component}/*/{component}/bin/java"))
    # Installed JDKs, e.g. C:\Program Files\Microsoft\jdk-25.0.3.9-hotspot.
    for pf in (os.environ.get("ProgramFiles"), os.environ.get("ProgramW6432")):
        if pf:
            candidates += sorted(Path(pf).glob(f"*/jdk-{major}*/bin/java.exe"), reverse=True)
            candidates += sorted(Path(pf).glob(f"*/jre-{major}*/bin/java.exe"), reverse=True)
    for c in candidates:
        if c.is_file() and java_major(c) == major:
            return c
    return None


# --- Steps ------------------------------------------------------------------------------------------

def step_server_jar(profile: dict, cache: Path, download: bool) -> Path | None:
    info = profile["downloads"]["server"]
    jar = cache / "server" / "server.jar"
    if jar.is_file() and sha1_of(jar) == info["sha1"]:
        log(f"server.jar: present, sha1 ok")
        return jar
    if jar.is_file():
        log("server.jar: present but sha1 does not match the profile; it will be replaced")
    if not download:
        log(f"server.jar: MISSING. Run again with --download-server to fetch it from Mojang:")
        log(f"  {info['url']}  ({info['size'] / 1e6:.1f} MB, sha1 {info['sha1']})")
        return None
    jar.parent.mkdir(parents=True, exist_ok=True)
    tmp = jar.with_suffix(".part")
    log(f"server.jar: downloading {info['url']}")
    with urllib.request.urlopen(info["url"], timeout=60) as resp, tmp.open("wb") as f:
        shutil.copyfileobj(resp, f)
    if tmp.stat().st_size != info["size"] or sha1_of(tmp) != info["sha1"]:
        tmp.unlink()
        raise RuntimeError("downloaded server.jar does not match the size and sha1 in the profile")
    tmp.replace(jar)
    log("server.jar: downloaded, sha1 ok")
    return jar


def step_server_version(jar: Path) -> dict:
    with zipfile.ZipFile(jar) as z:
        version = json.loads(z.read("version.json"))
    log(f"protocol: {version.get('protocol_version')} (world version {version.get('world_version')}, "
        f"name {version.get('name')})")
    return version


def step_reports(jar: Path, java: Path | None, cache: Path) -> bool:
    out = cache / "data"
    stamp_path = out / ".stamp.json"
    jar_sha1 = sha1_of(jar)
    if read_stamp(stamp_path).get("server_sha1") == jar_sha1 and (out / "reports" / "blocks.json").is_file():
        log("reports: up to date")
        return True
    if java is None:
        log("reports: SKIPPED, no Java 25 found (pass --java or set MC_JAVA)")
        return False
    if out.exists():
        shutil.rmtree(out)
    log(f"reports: running the data generator with {java}")
    # The bundler unpacks its libraries into the working directory, so run it inside cache/server.
    cmd = [str(java), "-DbundlerMainClass=net.minecraft.data.Main", "-jar", jar.name,
           "--reports", "--output", str(out)]
    result = subprocess.run(cmd, cwd=jar.parent, capture_output=True, text=True)
    (cache / "server" / "datagen.log").write_text(result.stdout + result.stderr, encoding="utf-8")
    if result.returncode != 0 or not (out / "reports" / "blocks.json").is_file():
        raise RuntimeError(f"data generator failed (exit {result.returncode}); see cache/server/datagen.log")
    reports = sorted(p.name for p in (out / "reports").glob("*.json"))
    write_json(stamp_path, {"server_sha1": jar_sha1, "reports": reports})
    log(f"reports: {', '.join(reports)}")
    return True


def step_client_jar(mc_dir: Path, version: str, profile: dict, cache: Path) -> bool:
    jar = mc_dir / "versions" / version / f"{version}.jar"
    if not jar.is_file():
        log(f"client jar: MISSING ({jar}). Launch {version} once from the official launcher.")
        return False
    jar_sha1 = sha1_of(jar)
    expected = profile["downloads"]["client"]["sha1"]
    if jar_sha1 != expected:
        raise RuntimeError(f"{jar.name} sha1 {jar_sha1} does not match the profile ({expected})")
    out = cache / "client"
    stamp_path = out / ".stamp.json"
    if read_stamp(stamp_path).get("client_sha1") == jar_sha1:
        log("client jar: up to date")
        return True
    if out.exists():
        shutil.rmtree(out)
    root = out.resolve()
    count = 0
    with zipfile.ZipFile(jar) as z:
        for entry in z.infolist():
            if entry.is_dir() or not entry.filename.startswith("assets/"):
                continue
            dest = (out / entry.filename).resolve()
            if root not in dest.parents:
                raise RuntimeError(f"unsafe path in client jar: {entry.filename}")
            dest.parent.mkdir(parents=True, exist_ok=True)
            with z.open(entry) as src, dest.open("wb") as dst:
                shutil.copyfileobj(src, dst)
            count += 1
    write_json(stamp_path, {"client_sha1": jar_sha1, "files": count})
    log(f"client jar: extracted {count} files from assets/")
    return True


def step_assets(mc_dir: Path, index_id: str, cache: Path) -> bool:
    index_path = mc_dir / "assets" / "indexes" / f"{index_id}.json"
    if not index_path.is_file():
        log(f"assets: MISSING index {index_path}")
        return False
    index_sha1 = sha1_of(index_path)
    out = cache / "assets"
    stamp_path = out / ".stamp.json"
    if read_stamp(stamp_path).get("index_sha1") == index_sha1:
        log("assets: up to date")
        return True
    objects = json.loads(index_path.read_text(encoding="utf-8"))["objects"]
    root = out.resolve()
    copied = kept = 0
    missing: list[str] = []
    for name, obj in objects.items():
        h = obj["hash"]
        src = mc_dir / "assets" / "objects" / h[:2] / h
        dest = (out / name).resolve()
        if root not in dest.parents:
            raise RuntimeError(f"unsafe path in assets index: {name}")
        if dest.is_file() and dest.stat().st_size == obj["size"]:
            kept += 1
            continue
        if not src.is_file():
            missing.append(name)
            continue
        dest.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(src, dest)
        if sha1_of(dest) != h:
            dest.unlink()
            raise RuntimeError(f"asset {name} does not match its hash {h}")
        copied += 1
    if missing:
        log(f"assets: {len(missing)} objects missing from .minecraft/assets/objects, e.g. {missing[0]}. "
            "Launch the game once so the launcher downloads them.")
        return False
    sounds = sum(1 for n in objects if n.startswith("minecraft/sounds/"))
    write_json(stamp_path, {"index_id": index_id, "index_sha1": index_sha1, "objects": len(objects),
                            "sounds": sounds})
    log(f"assets: {len(objects)} objects ({sounds} sounds), {copied} copied, {kept} already there")
    return True


# --- Main -------------------------------------------------------------------------------------------

def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--version", default=DEFAULT_VERSION)
    ap.add_argument("--mc-dir", type=Path, default=default_mc_dir())
    ap.add_argument("--cache", type=Path, default=REPO / "cache")
    ap.add_argument("--java", help="path to a Java 25 java.exe (default: search the launcher and JDKs)")
    ap.add_argument("--download-server", action="store_true", help="fetch server.jar from Mojang if missing")
    args = ap.parse_args()

    mc_dir: Path = args.mc_dir
    cache: Path = args.cache
    profile_path = mc_dir / "versions" / args.version / f"{args.version}.json"
    if not profile_path.is_file():
        log(f"No launcher profile at {profile_path}. Install {args.version} from the official launcher.")
        return 1
    profile = json.loads(profile_path.read_text(encoding="utf-8"))
    java_info = profile.get("javaVersion", {})
    java = find_java(mc_dir, java_info.get("component", ""), java_info.get("majorVersion", 25), args.java)
    log(f"Minecraft {args.version}, asset index {profile['assetIndex']['id']}, "
        f"Java {java_info.get('majorVersion')}: {java or 'NOT FOUND'}")

    manifest_path = cache / "manifest.json"
    manifest = read_stamp(manifest_path)
    manifest.update({"version": args.version, "asset_index": profile["assetIndex"]["id"],
                     "java": str(java) if java else None})
    done: dict[str, bool] = {}

    try:
        jar = step_server_jar(profile, cache, args.download_server)
        done["server_jar"] = jar is not None
        if jar:
            server_version = step_server_version(jar)
            manifest["protocol_version"] = server_version.get("protocol_version")
            manifest["server_version"] = server_version
            done["reports"] = step_reports(jar, java, cache)
        else:
            done["reports"] = False
        done["client_jar"] = step_client_jar(mc_dir, args.version, profile, cache)
        done["assets"] = step_assets(mc_dir, profile["assetIndex"]["id"], cache)
    except RuntimeError as e:
        log(f"ERROR: {e}")
        return 1

    manifest["steps"] = done
    manifest["updated"] = datetime.now(timezone.utc).isoformat(timespec="seconds")
    write_json(manifest_path, manifest)
    pending = [k for k, v in done.items() if not v]
    log("cache: complete" if not pending else f"cache: still pending: {', '.join(pending)}")
    return 0 if not pending else 2


if __name__ == "__main__":
    sys.exit(main())
