"""Test doubles: a fake management server, a fake server console, and a bare offline login."""

from __future__ import annotations

import io
import json
import socket
import struct
import sys
import threading
import uuid
import zlib
from pathlib import Path

TOOLS = Path(__file__).resolve().parents[1]
if str(TOOLS) not in sys.path:
    sys.path.insert(0, str(TOOLS))

from mchost import websocket as ws  # noqa: E402
from mchost.rpc import offline_uuid  # noqa: E402


class FakeManagementServer:
    """Speaks just enough WebSocket and JSON-RPC to stand in for the vanilla management server.

    `handlers` maps a method name to fn(params) -> result; raise FakeRpcError to send an error. Every request
    is recorded in `requests`. push() sends a notification to every connected client.
    """

    def __init__(self, secret: str, handlers: dict | None = None):
        self.secret = secret
        self.handlers = handlers or {}
        self.requests: list[dict] = []
        self.auth_headers: list[str | None] = []
        self.clients: list[ws.WebSocket] = []
        self.sock = socket.socket()
        self.sock.bind(("127.0.0.1", 0))
        self.sock.listen()
        self.port = self.sock.getsockname()[1]
        threading.Thread(target=self._accept, daemon=True).start()

    def _accept(self) -> None:
        while True:
            try:
                conn, _ = self.sock.accept()
            except OSError:
                return
            threading.Thread(target=self._serve, args=(conn,), daemon=True).start()

    def _serve(self, conn: socket.socket) -> None:
        data = b""
        while b"\r\n\r\n" not in data:
            chunk = conn.recv(4096)
            if not chunk:
                return
            data += chunk
        lines = data.decode("latin-1").split("\r\n")
        headers = {k.strip().lower(): v.strip() for k, _, v in (h.partition(":") for h in lines[1:] if h)}
        auth = headers.get("authorization")
        self.auth_headers.append(auth)
        if auth != f"Bearer {self.secret}":
            conn.sendall(b"HTTP/1.1 401 Unauthorized\r\nContent-Length: 0\r\n\r\n")
            conn.close()
            return
        conn.sendall(("HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
                      f"Sec-WebSocket-Accept: {ws.accept_key(headers['sec-websocket-key'])}\r\n\r\n").encode())
        client = ws.WebSocket(conn, mask=False)
        self.clients.append(client)
        try:
            while True:
                req = json.loads(client.recv_text())
                self.requests.append(req)
                fn = self.handlers.get(req["method"])
                if fn is None:
                    reply = {"jsonrpc": "2.0", "id": req["id"],
                             "error": {"code": -32601, "message": "Method not found"}}
                else:
                    try:
                        reply = {"jsonrpc": "2.0", "id": req["id"], "result": fn(req.get("params", []))}
                    except FakeRpcError as e:
                        reply = {"jsonrpc": "2.0", "id": req["id"],
                                 "error": {"code": e.code, "message": e.message}}
                client.send_text(json.dumps(reply))
        except (ws.ConnectionClosed, OSError):
            pass

    def push(self, method: str, params=None) -> None:
        msg = {"jsonrpc": "2.0", "method": method}
        if params is not None:
            msg["params"] = params
        for c in self.clients:
            c.send_text(json.dumps(msg))

    def close(self) -> None:
        self.sock.close()
        for c in self.clients:
            c.close()


class FakeRpcError(Exception):
    def __init__(self, code: int, message: str):
        super().__init__(message)
        self.code = code
        self.message = message


# A stand-in for `java -jar server.jar`: prints the lines the host watches for, echoes commands, exits on stop.
# argv[1] picks a behaviour: "normal", "crash" (exits before Done), "stubborn" (ignores stop).
FAKE_CONSOLE = r'''
import sys, time
mode = sys.argv[1]
print("[00:00:00] [ServerMain/INFO]: Starting json RPC server on localhost:1", flush=True)
if mode == "crash":
    print("[00:00:00] [ServerMain/ERROR]: Failed to start the minecraft server", flush=True)
    sys.exit(3)
print("[00:00:01] [Server thread/INFO]: Starting minecraft server version 26.3", flush=True)
print('[00:00:02] [Server thread/INFO]: Done (1,234s)! For help, type "help"', flush=True)
for line in sys.stdin:
    cmd = line.strip()
    print(f"[00:00:03] [Server thread/INFO]: got {cmd}", flush=True)
    if cmd == "stop" and mode != "stubborn":
        print("[00:00:04] [Server thread/INFO]: Stopping server", flush=True)
        sys.exit(0)
time.sleep(600)
'''


def fake_console_command(tmp: Path, mode: str = "normal") -> list[str]:
    script = tmp / "fake_server.py"
    script.write_text(FAKE_CONSOLE, encoding="utf-8")
    return [sys.executable, "-I", str(script), mode]


# --- A bare offline login, to check who the real server lets in -----------------------------------------

def _varint(n: int) -> bytes:
    out = bytearray()
    while True:
        b, n = n & 0x7F, n >> 7
        out.append(b | (0x80 if n else 0))
        if not n:
            return bytes(out)


def _read_varint(f) -> int:
    n = shift = 0
    while True:
        byte = f.read(1)
        if not byte:
            raise EOFError
        n |= (byte[0] & 0x7F) << shift
        shift += 7
        if not byte[0] & 0x80:
            return n


def _string(s: str) -> bytes:
    b = s.encode("utf-8")
    return _varint(len(b)) + b


def offline_login(port: int, name: str, protocol: int, host: str = "127.0.0.1") -> tuple[str, str]:
    """Handshake and log in as an offline player. Returns ("finished", uuid) or ("disconnect", reason).

    Packet ids are from packets.json for 26.3: handshake intention 0, login hello 0; clientbound
    login_disconnect 0, login_finished 2, login_compression 3.
    """
    def frame(pid: int, body: bytes) -> bytes:
        data = _varint(pid) + body
        return _varint(len(data)) + data

    with socket.create_connection((host, port), timeout=10) as s:
        f = s.makefile("rb")
        s.sendall(frame(0, _varint(protocol) + _string(host) + struct.pack(">H", port) + _varint(2)))
        s.sendall(frame(0, _string(name) + offline_uuid(name).bytes))
        compressed = False
        while True:
            data = f.read(_read_varint(f))
            if compressed:
                b = io.BytesIO(data)
                size = _read_varint(b)
                data = zlib.decompress(b.read()) if size else b.read()
            b = io.BytesIO(data)
            pid, body = _read_varint(b), b.read()
            if pid == 3:
                compressed = True
            elif pid == 2:
                return "finished", str(uuid.UUID(bytes=body[:16]))
            elif pid == 0:
                b = io.BytesIO(body)
                return "disconnect", b.read(_read_varint(b)).decode("utf-8", "replace")
            else:
                return "other", str(pid)
