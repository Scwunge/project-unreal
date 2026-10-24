"""Run local vanilla 26.3 servers for the client (M8 hosting).

  python tools/mc_host.py create NAME [--seed S] [--game-mode M] [--difficulty D] [--port P] [--accept-eula]
  python tools/mc_host.py eula NAME --accept
  python tools/mc_host.py list
  python tools/mc_host.py start NAME       runs in the foreground; type console commands, Ctrl+C stops it
  python tools/mc_host.py rpc NAME METHOD [JSON ...]   e.g. rpc dev minecraft:allowlist/add '[{"name":"Steve"}]'
  python tools/mc_host.py stop NAME
  python tools/mc_host.py allow NAME PLAYER... [--remove]     offline servers: whitelist.json
  python tools/mc_host.py op NAME PLAYER... [--level N] [--remove]   offline servers, while stopped: ops.json
  python tools/mc_host.py methods

The server is Mojang's unmodified cache/server/server.jar, run with Java 25. Instances live in server/<name>/,
which git ignores. Writing eula=true means you accept https://aka.ms/MinecraftEULA; nothing here does it for you.

Exit codes: 0 ok, 1 error, 2 the EULA isn't accepted.
"""

from __future__ import annotations

import argparse
import json
import sys
import threading
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from mchost import EulaNotAccepted, HostError, Instance, RpcError, Server, WorldOptions, load_schema
from mchost.server import DEFAULT_ROOT, DIFFICULTIES, GAME_MODES


def log(msg: str) -> None:
    print(msg, flush=True)


def get_instance(root: Path, name: str) -> Instance:
    inst = Instance(root / name)
    if not inst.exists():
        raise HostError(f"no instance {name!r} in {root}; create it first")
    return inst


def cmd_create(args) -> int:
    opts = WorldOptions(seed=args.seed, game_mode=args.game_mode, difficulty=args.difficulty, host=args.host,
                        port=args.port, motd=args.motd or "", hardcore=args.hardcore, allowlist=args.allowlist)
    inst = Instance.create(args.root, args.name, opts, accept_eula=args.accept_eula)
    log(f"created {inst.path}")
    if not inst.eula_accepted():
        log("The EULA isn't accepted yet. Read https://aka.ms/MinecraftEULA, then run:")
        log(f"  python tools/mc_host.py eula {inst.name} --accept")
    return 0


def cmd_eula(args) -> int:
    inst = get_instance(args.root, args.name)
    if args.accept:
        inst.accept_eula()
        log(f"{inst.name}: eula=true written")
    else:
        log(f"{inst.name}: EULA {'accepted' if inst.eula_accepted() else 'not accepted'}")
    return 0


def cmd_list(args) -> int:
    for inst in Instance.list(args.root):
        p = inst.properties()
        state = "running" if inst.running_pid() else "stopped"
        log(f"{inst.name:20} {state:8} {p.get('server-ip') or '*'}:{p.get('server-port')}  "
            f"{p.get('gamemode')}, {p.get('difficulty')}, seed {p.get('level-seed') or '(random)'}"
            f"{'' if inst.eula_accepted() else '  [EULA not accepted]'}")
    return 0


def cmd_start(args) -> int:
    inst = get_instance(args.root, args.name)
    server = Server(inst, java=args.java, memory=args.memory)
    server.on_console(lambda line: log(line))
    server.start()
    try:
        server.wait_ready(timeout=args.timeout)
        status = server.rpc.status() if server.rpc else None
        p = inst.properties()
        log(f"[mc_host] ready in {server.startup_seconds}s on {p.get('server-ip') or '*'}:{p.get('server-port')}"
            + (f", management API on port {p.get('management-server-port')}, "
               f"{status['version']['name']} protocol {status['version']['protocol']}" if status else ""))
        if server.rpc:
            server.rpc.on_notification(lambda m, params: log(f"[mc_host] {m.removeprefix('minecraft:notification/')} "
                                                                   f"{json.dumps(params)}"))

        def pump_stdin() -> None:
            if not sys.stdin.isatty():
                # Piped input: PowerShell sends UTF-8 with a byte-order mark, which would reach the server.
                sys.stdin.reconfigure(encoding="utf-8-sig")
            for line in sys.stdin:
                line = line.strip()
                if line and server.running:
                    server.send_command(line)

        threading.Thread(target=pump_stdin, daemon=True).start()
        while not server.exited.wait(0.5):
            pass
    except KeyboardInterrupt:
        log("[mc_host] stopping...")
    finally:
        code = server.stop()
    log(f"[mc_host] exited with code {code}")
    return 0 if code == 0 else 1


def cmd_rpc(args) -> int:
    inst = get_instance(args.root, args.name)
    params = [json.loads(p) for p in args.params]
    client = inst.connect()
    try:
        result = client.call(args.method, *params)
    finally:
        client.close()
    log(json.dumps(result, indent=2))
    return 0


def cmd_stop(args) -> int:
    inst = get_instance(args.root, args.name)
    client = inst.connect()
    try:
        client.stop()
    finally:
        client.close()
    log(f"{inst.name}: stop requested")
    return 0


def cmd_allow(args) -> int:
    inst = get_instance(args.root, args.name)
    if not inst.offline():
        raise HostError("online-mode server: use  rpc NAME minecraft:allowlist/add '[{\"name\": \"...\"}]'")
    names = inst.allowlist_remove(*args.players) if args.remove else inst.allowlist_add(*args.players)
    log(f"{inst.name} allowlist: {', '.join(names) or '(empty)'}")
    if inst.running_pid():
        log("It's running: type  whitelist reload  in its console, or restart it.")
    return 0


def cmd_op(args) -> int:
    inst = get_instance(args.root, args.name)
    if not inst.offline():
        raise HostError("online-mode server: use  rpc NAME minecraft:operators/add ...")
    if inst.running_pid():
        raise HostError("stop it first (a running server overwrites ops.json), or type  op NAME  in its console")
    names = inst.ops_remove(*args.players) if args.remove else inst.ops_add(*args.players, level=args.level)
    log(f"{inst.name} operators: {', '.join(names) or '(none)'}")
    return 0


def cmd_methods(args) -> int:
    schema = load_schema()
    if schema is None:
        raise HostError("no schema; run tools/fetch_cache.py first")
    log(f"Management API {schema.version}")
    for name in schema.calls():
        spec = schema.methods[name]
        params = ", ".join(p["name"] for p in spec.get("params", []))
        log(f"  {name}({params})  {spec.get('description', '')}")
    log("Notifications:")
    for name in schema.notifications():
        log(f"  {name}")
    return 0


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--root", type=Path, default=DEFAULT_ROOT, help="where instances live (default: server/)")
    sub = ap.add_subparsers(dest="cmd", required=True)

    c = sub.add_parser("create", help="make a new server and world")
    c.add_argument("name")
    c.add_argument("--seed", default="")
    c.add_argument("--game-mode", default="survival", choices=GAME_MODES)
    c.add_argument("--difficulty", default="normal", choices=DIFFICULTIES)
    c.add_argument("--host", default="127.0.0.1", help="address to listen on (default: this machine only)")
    c.add_argument("--port", type=int, default=25565)
    c.add_argument("--motd")
    c.add_argument("--hardcore", action="store_true")
    c.add_argument("--allowlist", action="store_true", help="only let allowlisted players join")
    c.add_argument("--accept-eula", action="store_true", help="you accept https://aka.ms/MinecraftEULA")
    c.set_defaults(fn=cmd_create)

    e = sub.add_parser("eula", help="show or accept the EULA for an instance")
    e.add_argument("name")
    e.add_argument("--accept", action="store_true", help="you accept https://aka.ms/MinecraftEULA")
    e.set_defaults(fn=cmd_eula)

    sub.add_parser("list", help="list instances").set_defaults(fn=cmd_list)

    s = sub.add_parser("start", help="run a server in the foreground")
    s.add_argument("name")
    s.add_argument("--java", help="path to a Java 25 java.exe")
    s.add_argument("--memory", default="2G", help="max heap, e.g. 2G")
    s.add_argument("--timeout", type=float, default=300, help="seconds to wait for it to be ready")
    s.set_defaults(fn=cmd_start)

    r = sub.add_parser("rpc", help="call a management API method on a running server")
    r.add_argument("name")
    r.add_argument("method")
    r.add_argument("params", nargs="*", help="one JSON value per param, in order")
    r.set_defaults(fn=cmd_rpc)

    t = sub.add_parser("stop", help="stop a running server through the management API")
    t.add_argument("name")
    t.set_defaults(fn=cmd_stop)

    a = sub.add_parser("allow", help="allowlist players on an offline server (writes whitelist.json)")
    a.add_argument("name")
    a.add_argument("players", nargs="+")
    a.add_argument("--remove", action="store_true")
    a.set_defaults(fn=cmd_allow)

    o = sub.add_parser("op", help="op players on a stopped offline server (writes ops.json)")
    o.add_argument("name")
    o.add_argument("players", nargs="+")
    o.add_argument("--level", type=int, default=4, choices=range(1, 5))
    o.add_argument("--remove", action="store_true")
    o.set_defaults(fn=cmd_op)

    sub.add_parser("methods", help="list the management API from the schema").set_defaults(fn=cmd_methods)

    args = ap.parse_args()
    try:
        return args.fn(args)
    except EulaNotAccepted as e:
        log(str(e))
        return 2
    except (HostError, RpcError, ConnectionError, TimeoutError, OSError, ValueError) as e:
        log(f"ERROR: {e}")
        return 1


if __name__ == "__main__":
    sys.exit(main())
