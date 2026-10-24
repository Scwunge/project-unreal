"""Client for the vanilla server's JSON-RPC management API (a WebSocket on management-server-port).

Method names and parameters come from the schema the data generator writes,
cache/data/reports/json-rpc-api-schema.json, so keyword arguments are mapped to the right positions and
unknown methods are refused before they reach the server.
"""

from __future__ import annotations

import hashlib
import itertools
import json
import queue
import threading
import time
import uuid
from pathlib import Path
from typing import Any, Callable

from .websocket import ConnectionClosed, WebSocket

NOTIFICATION_PREFIX = "minecraft:notification/"


class RpcError(Exception):
    def __init__(self, code: int, message: str, data: Any = None):
        super().__init__(f"{message} (code {code})" + (f": {data}" if data else ""))
        self.code = code
        self.message = message
        self.data = data


class Schema:
    def __init__(self, doc: dict):
        self.version = doc.get("info", {}).get("version")
        self.methods = {m["name"]: m for m in doc.get("methods", [])}

    @classmethod
    def load(cls, path: Path) -> "Schema":
        return cls(json.loads(path.read_text(encoding="utf-8")))

    def calls(self) -> list[str]:
        return sorted(n for n in self.methods if not n.startswith(NOTIFICATION_PREFIX))

    def notifications(self) -> list[str]:
        return sorted(n for n in self.methods if n.startswith(NOTIFICATION_PREFIX))

    def bind(self, method: str, args: tuple, kwargs: dict) -> list:
        """Turn positional and keyword arguments into the positional params list for `method`."""
        spec = self.methods.get(method)
        if spec is None or method.startswith(NOTIFICATION_PREFIX):
            raise ValueError(f"unknown method {method}")
        names = [p["name"] for p in spec.get("params", [])]
        if len(args) > len(names):
            raise TypeError(f"{method} takes {len(names)} params, got {len(args)}")
        params = list(args)
        for name in names[len(args):]:
            if name in kwargs:
                params.append(kwargs.pop(name))
            elif any(p["name"] == name and p.get("required") for p in spec["params"]):
                raise TypeError(f"{method} needs param {name!r}")
            else:
                break
        if kwargs:
            raise TypeError(f"{method} has no param {', '.join(kwargs)}")
        return params


def player(name: str) -> dict:
    """A player object by name; the server looks the name up with Mojang's profile service.

    Tested on 26.3: that lookup happens even in offline mode, so the stored UUID is the online one and an
    offline player with that name still can't join. Ids the profile service doesn't know (offline UUIDs) are
    dropped without an error, and allowlist/set with them empties the list. For offline servers use
    Instance.allowlist_add and friends, which write the vanilla files instead.
    """
    return {"name": name}


def offline_uuid(name: str) -> uuid.UUID:
    """The UUID an offline-mode server gives a name: a version 3 UUID of "OfflinePlayer:<name>"."""
    digest = bytearray(hashlib.md5(f"OfflinePlayer:{name}".encode("utf-8")).digest())
    digest[6] = (digest[6] & 0x0F) | 0x30
    digest[8] = (digest[8] & 0x3F) | 0x80
    return uuid.UUID(bytes=bytes(digest))


def text(literal: str) -> dict:
    return {"literal": literal}


class ManagementClient:
    """One connection. Calls block until their response; notifications go to listeners and a queue."""

    def __init__(self, ws: WebSocket, schema: Schema | None = None):
        self.ws = ws
        self.schema = schema
        self._ids = itertools.count(1)
        self._pending: dict[int, queue.Queue] = {}
        self._lock = threading.Lock()
        self._listeners: list[Callable[[str, Any], None]] = []
        self.notifications: queue.Queue = queue.Queue()
        self.closed = threading.Event()
        self.close_reason: str | None = None
        self._reader = threading.Thread(target=self._read_loop, name="mchost-rpc", daemon=True)
        self._reader.start()

    @classmethod
    def connect(cls, host: str, port: int, secret: str, schema: Schema | None = None, tls: bool = False,
                timeout: float = 10.0) -> "ManagementClient":
        ws = WebSocket.connect(host, port, headers={"Authorization": f"Bearer {secret}"}, tls=tls,
                               timeout=timeout)
        return cls(ws, schema)

    def on_notification(self, fn: Callable[[str, Any], None]) -> None:
        self._listeners.append(fn)

    def _read_loop(self) -> None:
        try:
            while True:
                msg = json.loads(self.ws.recv_text())
                if "id" in msg and msg["id"] is not None and ("result" in msg or "error" in msg):
                    with self._lock:
                        q = self._pending.pop(msg["id"], None)
                    if q:
                        q.put(msg)
                elif "method" in msg:
                    params = msg.get("params")
                    self.notifications.put((msg["method"], params))
                    for fn in list(self._listeners):
                        fn(msg["method"], params)
        except ConnectionClosed as e:
            self.close_reason = str(e)
        except (OSError, ValueError) as e:
            self.close_reason = f"{type(e).__name__}: {e}"
        finally:
            self.closed.set()
            with self._lock:
                pending, self._pending = self._pending, {}
            for q in pending.values():
                q.put(None)

    def call(self, method: str, *args, timeout: float = 30.0, **kwargs) -> Any:
        if self.schema:
            params = self.schema.bind(method, args, kwargs)
        elif kwargs:
            raise TypeError("keyword params need the schema")
        else:
            params = list(args)
        if self.closed.is_set():
            raise ConnectionError(f"management connection is closed: {self.close_reason}")
        msg_id = next(self._ids)
        q: queue.Queue = queue.Queue(maxsize=1)
        with self._lock:
            self._pending[msg_id] = q
        request = {"jsonrpc": "2.0", "id": msg_id, "method": method}
        if params:
            request["params"] = params
        self.ws.send_text(json.dumps(request))
        try:
            reply = q.get(timeout=timeout)
        except queue.Empty:
            with self._lock:
                self._pending.pop(msg_id, None)
            raise TimeoutError(f"{method}: no reply in {timeout}s") from None
        if reply is None:
            raise ConnectionError(f"management connection closed during {method}: {self.close_reason}")
        if "error" in reply:
            err = reply["error"]
            raise RpcError(err.get("code", 0), err.get("message", "error"), err.get("data"))
        return reply.get("result")

    def wait_for(self, method: str, timeout: float = 30.0) -> Any:
        """Wait for a notification by name (with or without the minecraft:notification/ prefix).

        Returns its params as sent: a positional list such as [{"key": ..., "value": ...}], or None.
        """
        if not method.startswith(NOTIFICATION_PREFIX):
            method = NOTIFICATION_PREFIX + method
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            try:
                name, params = self.notifications.get(timeout=0.2)
            except queue.Empty:
                if self.closed.is_set():
                    raise ConnectionError(f"closed while waiting for {method}: {self.close_reason}")
                continue
            if name == method:
                return params
        raise TimeoutError(f"no {method} in {timeout}s")

    # Shortcuts for what a host UI uses most. Anything else: call("minecraft:...", ...).

    def status(self) -> dict:
        return self.call("minecraft:server/status")

    def players(self) -> list:
        return self.call("minecraft:players")

    def save(self, flush: bool = True) -> bool:
        return self.call("minecraft:server/save", flush)

    def stop(self) -> bool:
        return self.call("minecraft:server/stop")

    def say(self, message: str) -> bool:
        return self.call("minecraft:server/system_message", {"message": text(message), "overlay": False})

    def allowlist(self) -> list:
        return self.call("minecraft:allowlist")

    # The allowlist and op shortcuts resolve names online; see player() for what that means offline.

    def allowlist_add(self, *names: str) -> list:
        return self.call("minecraft:allowlist/add", [player(n) for n in names])

    def allowlist_remove(self, *names: str) -> list:
        return self.call("minecraft:allowlist/remove", [player(n) for n in names])

    def operators(self) -> list:
        return self.call("minecraft:operators")

    def op(self, *names: str, level: int = 4) -> list:
        return self.call("minecraft:operators/add",
                         [{"player": player(n), "permissionLevel": level, "bypassesPlayerLimit": False}
                          for n in names])

    def deop(self, *names: str) -> list:
        return self.call("minecraft:operators/remove", [player(n) for n in names])

    def kick(self, name: str, message: str = "") -> list:
        entry: dict = {"player": player(name)}
        if message:
            entry["message"] = text(message)
        return self.call("minecraft:players/kick", [entry])

    def gamerules(self) -> list:
        return self.call("minecraft:gamerules")

    def set_gamerule(self, key: str, value: bool | int) -> dict:
        return self.call("minecraft:gamerules/update", {"key": key, "value": value})

    def setting(self, name: str) -> Any:
        return self.call(f"minecraft:serversettings/{name}")

    def set_setting(self, name: str, value: Any) -> Any:
        return self.call(f"minecraft:serversettings/{name}/set", value)

    def close(self) -> None:
        self.ws.close()
        self._reader.join(timeout=2)
