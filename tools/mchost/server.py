"""Create, start and stop local vanilla servers (M8 hosting).

The game is Mojang's unmodified server.jar, read from cache/server/server.jar. This module only writes the
instance folder (server.properties, eula.txt), runs the jar with Java 25, watches the console and talks to
the management API.

Layout, all gitignored:
  server/<name>/        one instance: server.properties, eula.txt, world/, logs/, host.json
  server/.libraries/    the jar's bundled libraries, unpacked once and shared by every instance
"""

from __future__ import annotations

import collections
import json
import os
import re
import secrets
import socket
import string
import subprocess
import sys
import threading
import time
from dataclasses import dataclass, field
from datetime import datetime, timezone
from pathlib import Path
from typing import Callable

from . import properties
from .rpc import ManagementClient, Schema, offline_uuid

REPO = Path(__file__).resolve().parents[2]
SERVER_JAR = REPO / "cache" / "server" / "server.jar"
SCHEMA_PATH = REPO / "cache" / "data" / "reports" / "json-rpc-api-schema.json"
DEFAULT_ROOT = REPO / "server"
JAVA_MAJOR = 25

GAME_MODES = ("survival", "creative", "adventure", "spectator")
DIFFICULTIES = ("peaceful", "easy", "normal", "hard")

READY_RE = re.compile(r"\]: Done \((\d+[.,]\d+)s\)! For help")
MANAGEMENT_RE = re.compile(r"Json-RPC Management connection listening on (\S+)")
NAME_RE = re.compile(r"^[A-Za-z0-9_.-]{1,64}$")


class HostError(Exception):
    pass


class EulaNotAccepted(HostError):
    pass


# --- Java -------------------------------------------------------------------------------------------

def java_major(java: Path) -> int | None:
    try:
        out = subprocess.run([str(java), "-version"], capture_output=True, text=True, timeout=30)
    except (OSError, subprocess.TimeoutExpired):
        return None
    m = re.search(r'version "(\d+)', out.stderr + out.stdout)
    return int(m.group(1)) if m else None


def find_java(explicit: str | Path | None = None) -> Path:
    """Java 25: --java, then MC_JAVA, then what fetch_cache.py found, then the launcher and JDK folders."""
    candidates: list[Path] = []
    if explicit:
        candidates.append(Path(explicit))
    if os.environ.get("MC_JAVA"):
        candidates.append(Path(os.environ["MC_JAVA"]))
    try:
        manifest = json.loads((REPO / "cache" / "manifest.json").read_text(encoding="utf-8"))
        if manifest.get("java"):
            candidates.append(Path(manifest["java"]))
    except (OSError, ValueError):
        pass
    for c in candidates:
        if c.is_file() and java_major(c) == JAVA_MAJOR:
            return c
    sys.path.insert(0, str(REPO / "tools"))
    try:
        import fetch_cache
        found = fetch_cache.find_java(fetch_cache.default_mc_dir(), "java-runtime-epsilon", JAVA_MAJOR, None)
    finally:
        sys.path.pop(0)
    if found:
        return found
    raise HostError(f"no Java {JAVA_MAJOR} found; pass --java or set MC_JAVA (the java on PATH may be older)")


# --- Instances --------------------------------------------------------------------------------------

def free_port(host: str = "127.0.0.1") -> int:
    with socket.socket() as s:
        s.bind((host, 0))
        return s.getsockname()[1]


def port_in_use(host: str, port: int) -> bool:
    with socket.socket() as s:
        try:
            s.bind((host or "0.0.0.0", port))
        except OSError:
            return True
    return False


def new_secret() -> str:
    # The management server wants exactly 40 letters and digits.
    alphabet = string.ascii_letters + string.digits
    return "".join(secrets.choice(alphabet) for _ in range(40))


@dataclass
class WorldOptions:
    seed: str = ""
    game_mode: str = "survival"
    difficulty: str = "normal"
    host: str = "127.0.0.1"
    port: int = 25565
    online_mode: bool = False
    motd: str = ""
    hardcore: bool = False
    level_type: str = "minecraft:normal"
    allowlist: bool = False
    max_players: int = 8
    view_distance: int = 10
    simulation_distance: int = 10
    extra: dict[str, str] = field(default_factory=dict)

    def validate(self) -> None:
        if self.game_mode not in GAME_MODES:
            raise HostError(f"game mode must be one of {', '.join(GAME_MODES)}")
        if self.difficulty not in DIFFICULTIES:
            raise HostError(f"difficulty must be one of {', '.join(DIFFICULTIES)}")
        if not 0 < self.port < 65536:
            raise HostError("port must be 1-65535")

    def to_properties(self, name: str) -> dict[str, str]:
        b = lambda v: "true" if v else "false"  # noqa: E731
        props = {
            "level-name": "world",
            "level-seed": self.seed,
            "level-type": self.level_type,
            "gamemode": self.game_mode,
            "difficulty": self.difficulty,
            "hardcore": b(self.hardcore),
            "server-ip": self.host,
            "server-port": str(self.port),
            "online-mode": b(self.online_mode),
            # Chat signing needs Microsoft accounts; an offline server can't enforce it.
            "enforce-secure-profile": b(self.online_mode),
            "white-list": b(self.allowlist),
            "enforce-whitelist": b(self.allowlist),
            "max-players": str(self.max_players),
            "view-distance": str(self.view_distance),
            "simulation-distance": str(self.simulation_distance),
            "motd": self.motd or name,
        }
        props.update(self.extra)
        return props


class Instance:
    """A server folder under server/. Creating one never accepts the EULA unless asked to."""

    def __init__(self, path: Path):
        self.path = Path(path)
        self.name = self.path.name

    @property
    def properties_path(self) -> Path:
        return self.path / "server.properties"

    @property
    def meta_path(self) -> Path:
        return self.path / "host.json"

    def exists(self) -> bool:
        return self.meta_path.is_file()

    def properties(self) -> dict[str, str]:
        return properties.read(self.properties_path)

    def meta(self) -> dict:
        try:
            return json.loads(self.meta_path.read_text(encoding="utf-8"))
        except (OSError, ValueError):
            return {}

    def write_meta(self, **changes) -> None:
        meta = self.meta()
        meta.update(changes)
        self.meta_path.write_text(json.dumps(meta, indent=2) + "\n", encoding="utf-8")

    def running_pid(self) -> int | None:
        """The pid of this instance's server if it's still running (possibly started by another process)."""
        meta = self.meta()
        pid = meta.get("pid")
        return pid if pid and pid_alive(pid, meta.get("exe", "java")) else None

    def eula_accepted(self) -> bool:
        return properties.read(self.path / "eula.txt").get("eula", "").strip().lower() == "true"

    def accept_eula(self) -> None:
        """Only call this when the user has said they accept https://aka.ms/MinecraftEULA."""
        stamp = datetime.now(timezone.utc).isoformat(timespec="seconds")
        (self.path / "eula.txt").write_text(
            "#Accepted by the user through tools/mc_host.py (https://aka.ms/MinecraftEULA).\n"
            f"#{stamp}\neula=true\n", encoding="utf-8")

    @classmethod
    def create(cls, root: Path, name: str, options: WorldOptions | None = None,
               accept_eula: bool = False) -> "Instance":
        if not NAME_RE.match(name) or name.startswith("."):
            raise HostError("name: letters, digits, '_', '-' and '.', up to 64, not starting with '.'")
        options = options or WorldOptions()
        options.validate()
        inst = cls(Path(root) / name)
        if inst.path.exists():
            raise HostError(f"{inst.path} already exists")
        inst.path.mkdir(parents=True)
        props = options.to_properties(name)
        props.update({
            "management-server-enabled": "true",
            "management-server-host": "localhost",
            "management-server-port": "0",
            "management-server-secret": new_secret(),
            # Local only; the host and the server are on the same machine.
            "management-server-tls-enabled": "false",
        })
        properties.update(inst.properties_path, props)
        inst.write_meta(name=name, created=datetime.now(timezone.utc).isoformat(timespec="seconds"),
                        server_jar=str(SERVER_JAR))
        if accept_eula:
            inst.accept_eula()
        return inst

    @classmethod
    def list(cls, root: Path) -> list["Instance"]:
        root = Path(root)
        if not root.is_dir():
            return []
        return [cls(p) for p in sorted(root.iterdir()) if p.is_dir() and (p / "host.json").is_file()]

    # Allowlist and ops for offline servers. Tested on 26.3: the management API and the console both resolve
    # names to online UUIDs, which offline players don't have, so these write the vanilla files with offline
    # UUIDs instead. The server reads them at start; Server.allowlist_add also reloads the allowlist live.

    def offline(self) -> bool:
        return self.properties().get("online-mode", "true") != "true"

    def _read_list(self, filename: str) -> list[dict]:
        try:
            return json.loads((self.path / filename).read_text(encoding="utf-8"))
        except FileNotFoundError:
            return []

    def _write_list(self, filename: str, entries: list[dict]) -> None:
        (self.path / filename).write_text(json.dumps(entries, indent=2) + "\n", encoding="utf-8")

    def allowlist(self) -> list[str]:
        return [e["name"] for e in self._read_list("whitelist.json")]

    def allowlist_add(self, *names: str) -> list[str]:
        entries = self._read_list("whitelist.json")
        have = {e["name"].lower() for e in entries}
        for n in names:
            if n.lower() not in have:
                have.add(n.lower())
                entries.append({"uuid": str(offline_uuid(n)), "name": n})
        self._write_list("whitelist.json", entries)
        return [e["name"] for e in entries]

    def allowlist_remove(self, *names: str) -> list[str]:
        drop = {n.lower() for n in names}
        entries = [e for e in self._read_list("whitelist.json") if e["name"].lower() not in drop]
        self._write_list("whitelist.json", entries)
        return [e["name"] for e in entries]

    def operators(self) -> list[dict]:
        return self._read_list("ops.json")

    def ops_add(self, *names: str, level: int = 4) -> list[str]:
        """Only while stopped: a running server keeps ops in memory and overwrites ops.json."""
        entries = [e for e in self._read_list("ops.json") if e["name"].lower() not in {n.lower() for n in names}]
        entries += [{"uuid": str(offline_uuid(n)), "name": n, "level": level, "bypassesPlayerLimit": False}
                    for n in names]
        self._write_list("ops.json", entries)
        return [e["name"] for e in entries]

    def ops_remove(self, *names: str) -> list[str]:
        drop = {n.lower() for n in names}
        entries = [e for e in self._read_list("ops.json") if e["name"].lower() not in drop]
        self._write_list("ops.json", entries)
        return [e["name"] for e in entries]

    def management_endpoint(self) -> tuple[str, int, str, bool]:
        p = self.properties()
        if p.get("management-server-enabled") != "true":
            raise HostError("the management server is turned off in server.properties")
        port = int(p.get("management-server-port") or 0)
        if not port:
            raise HostError("no management port yet; start the server first")
        return (p.get("management-server-host") or "localhost", port, p["management-server-secret"],
                p.get("management-server-tls-enabled") == "true")

    def connect(self, schema: Schema | None = None, timeout: float = 10.0) -> ManagementClient:
        host, port, secret, tls = self.management_endpoint()
        return ManagementClient.connect(host, port, secret, schema or load_schema(), tls=tls, timeout=timeout)


def load_schema() -> Schema | None:
    return Schema.load(SCHEMA_PATH) if SCHEMA_PATH.is_file() else None


# --- Running ----------------------------------------------------------------------------------------

class Server:
    """A running instance: the java process, its console, and a management connection once it's ready."""

    def __init__(self, instance: Instance, java: str | Path | None = None, jar: Path = SERVER_JAR,
                 memory: str = "2G", command: list[str] | None = None, libraries: Path | None = None,
                 history: int = 2000):
        self.instance = instance
        self.jar = Path(jar)
        self.memory = memory
        self._command = command
        self._java = java
        self.libraries = libraries or (instance.path.parent / ".libraries")
        self.console: collections.deque[str] = collections.deque(maxlen=history)
        self.ready = threading.Event()
        self.exited = threading.Event()
        self.startup_seconds: float | None = None
        self.management_address: str | None = None
        self.rpc: ManagementClient | None = None
        self.proc: subprocess.Popen | None = None
        self._listeners: list[Callable[[str], None]] = []
        self._stdin_lock = threading.Lock()

    def on_console(self, fn: Callable[[str], None]) -> None:
        self._listeners.append(fn)

    def command_line(self) -> list[str]:
        if self._command:
            return list(self._command)
        java = find_java(self._java)
        return [str(java), f"-Xmx{self.memory}", f"-DbundlerRepoDir={self.libraries}",
                "-jar", str(self.jar), "--nogui"]

    def start(self) -> None:
        inst = self.instance
        if not inst.exists():
            raise HostError(f"no instance at {inst.path}")
        if not inst.eula_accepted():
            raise EulaNotAccepted(f"{inst.name}: the EULA isn't accepted. Read https://aka.ms/MinecraftEULA, "
                                  f"then run: python tools/mc_host.py eula {inst.name} --accept")
        if not self._command and not self.jar.is_file():
            raise HostError(f"no server jar at {self.jar}; run tools/fetch_cache.py first")
        pid = inst.running_pid()
        if pid:
            raise HostError(f"{inst.name} looks like it's already running (pid {pid})")
        props = inst.properties()
        game_host, game_port = props.get("server-ip", ""), int(props.get("server-port") or 25565)
        if port_in_use(game_host, game_port):
            raise HostError(f"port {game_port} is in use; pick another with --port or stop what's using it")
        if props.get("management-server-enabled") == "true":
            # A fresh port each start, so the host always knows where to connect.
            properties.update(inst.properties_path,
                              {"management-server-port": str(free_port(props.get("management-server-host")
                                                                       or "127.0.0.1"))})
        self.libraries.mkdir(parents=True, exist_ok=True)
        flags = subprocess.CREATE_NEW_PROCESS_GROUP if sys.platform == "win32" else 0
        cmd = self.command_line()
        self.proc = subprocess.Popen(
            cmd, cwd=inst.path, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT, text=True, encoding="utf-8", errors="replace", bufsize=1,
            creationflags=flags)
        inst.write_meta(pid=self.proc.pid, exe=Path(cmd[0]).stem, started=datetime.now(timezone.utc).isoformat(timespec="seconds"))
        threading.Thread(target=self._read_console, name="mchost-console", daemon=True).start()

    def _read_console(self) -> None:
        assert self.proc and self.proc.stdout
        for raw in self.proc.stdout:
            line = raw.rstrip("\r\n")
            self.console.append(line)
            m = READY_RE.search(line)
            if m:
                self.startup_seconds = float(m.group(1).replace(",", "."))
                self.ready.set()
            m = MANAGEMENT_RE.search(line)
            if m:
                self.management_address = m.group(1)
            for fn in list(self._listeners):
                try:
                    fn(line)
                except Exception:  # a listener's bug shouldn't stop the console
                    pass
        self.proc.wait()
        for pipe in (self.proc.stdin, self.proc.stdout):
            try:
                pipe.close()
            except OSError:
                pass
        self.instance.write_meta(pid=None, exit_code=self.proc.returncode)
        self.exited.set()
        self.ready.set()  # wake anyone in wait_ready

    def wait_ready(self, timeout: float = 300.0, connect: bool = True) -> None:
        """Block until the console says Done, then open the management connection."""
        if not self.ready.wait(timeout):
            raise TimeoutError(f"{self.instance.name} wasn't ready after {timeout}s")
        if self.exited.is_set():
            tail = "\n".join(list(self.console)[-25:])
            raise HostError(f"{self.instance.name} exited with code {self.proc.returncode} before it was "
                            f"ready:\n{tail}")
        if connect and self.instance.properties().get("management-server-enabled") == "true":
            self.rpc = self.instance.connect()

    def send_command(self, command: str) -> None:
        """Type a console command, e.g. "time set day". Use the management API where it covers the task."""
        if not self.running:
            raise HostError("the server isn't running")
        with self._stdin_lock:
            self.proc.stdin.write(command.rstrip("\n") + "\n")
            self.proc.stdin.flush()

    def allowlist_add(self, *names: str) -> list[str]:
        """Allowlist players the way that works for this server: the vanilla file when offline, else the API."""
        if self.instance.offline():
            names_now = self.instance.allowlist_add(*names)
            self._reload_allowlist(names_now)
            return names_now
        return [p["name"] for p in self._api().allowlist_add(*names)]

    def allowlist_remove(self, *names: str) -> list[str]:
        if self.instance.offline():
            names_now = self.instance.allowlist_remove(*names)
            self._reload_allowlist(names_now)
            return names_now
        return [p["name"] for p in self._api().allowlist_remove(*names)]

    def _reload_allowlist(self, expected: list[str], timeout: float = 10.0) -> None:
        """Have a running server re-read whitelist.json, and wait until the API shows the new list."""
        if not self.running:
            return
        self.send_command("whitelist reload")  # runs on the server's next tick
        if not self.rpc:
            return
        want = sorted(n.lower() for n in expected)
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            if sorted(p["name"].lower() for p in self.rpc.allowlist()) == want:
                return
            time.sleep(0.1)
        raise HostError("the server didn't pick up the new whitelist.json")

    def op(self, *names: str, level: int = 4) -> None:
        """Offline and running: vanilla's op command, which finds players who are connected right now."""
        offline = self.instance.offline()
        if offline and not self.running:
            self.instance.ops_add(*names, level=level)
        elif offline:
            online = {p["name"].lower() for p in self._api().players()}
            missing = [n for n in names if n.lower() not in online]
            if missing:
                raise HostError(f"offline server: {', '.join(missing)} must be connected to be opped while it "
                                "runs (or stop the server and op them then)")
            for n in names:
                self.send_command(f"op {n}")
        else:
            self._api().op(*names, level=level)

    def _api(self) -> ManagementClient:
        if not self.rpc:
            raise HostError("no management connection; is the server running and ready?")
        return self.rpc

    @property
    def running(self) -> bool:
        return self.proc is not None and self.proc.poll() is None

    def stop(self, timeout: float = 60.0) -> int:
        """Stop cleanly (the server saves the world first). Kills the process only if it doesn't exit."""
        if self.proc is None:
            return 0
        if self.running:
            asked = False
            if self.rpc and not self.rpc.closed.is_set():
                try:
                    self.rpc.stop()
                    asked = True
                except (OSError, ConnectionError, TimeoutError):
                    pass
            if not asked:
                try:
                    self.send_command("stop")
                except (OSError, HostError):
                    pass
            if not self.exited.wait(timeout):
                self.proc.kill()
                self.exited.wait(10)
        if self.rpc:
            self.rpc.close()
            self.rpc = None
        return self.proc.returncode

    def __enter__(self) -> "Server":
        return self

    def __exit__(self, *exc) -> None:
        self.stop()


def pid_alive(pid: int, exe: str = "java") -> bool:
    """True if `pid` is running and is `exe` (a reused pid from something else doesn't count)."""
    if sys.platform == "win32":
        out = subprocess.run(["tasklist", "/FI", f"PID eq {pid}", "/NH", "/FO", "CSV"],
                             capture_output=True, text=True)
        return f'"{pid}"' in out.stdout and exe.lower() in out.stdout.lower()
    try:
        os.kill(pid, 0)
    except OSError:
        return False
    return True
