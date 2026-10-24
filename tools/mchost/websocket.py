"""A minimal WebSocket (RFC 6455) client over a plain socket, for the server's management API.

Standard library only, like the rest of tools/. Text and close frames, fragmentation, ping/pong. The frame
helpers work for both ends so the tests can run a fake server with them.
"""

from __future__ import annotations

import base64
import hashlib
import os
import socket
import ssl
import struct
import threading

GUID = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"

OP_CONT, OP_TEXT, OP_BINARY, OP_CLOSE, OP_PING, OP_PONG = 0x0, 0x1, 0x2, 0x8, 0x9, 0xA


class WebSocketError(Exception):
    pass


class ConnectionClosed(WebSocketError):
    def __init__(self, code: int | None = None, reason: str = ""):
        super().__init__(f"connection closed ({code} {reason})".strip())
        self.code = code
        self.reason = reason


def accept_key(key: str) -> str:
    return base64.b64encode(hashlib.sha1((key + GUID).encode()).digest()).decode()


def encode_frame(opcode: int, payload: bytes, mask: bool, fin: bool = True) -> bytes:
    head = bytearray([(0x80 if fin else 0) | opcode])
    n = len(payload)
    mbit = 0x80 if mask else 0
    if n < 126:
        head.append(mbit | n)
    elif n < 1 << 16:
        head.append(mbit | 126)
        head += struct.pack("!H", n)
    else:
        head.append(mbit | 127)
        head += struct.pack("!Q", n)
    if mask:
        key = os.urandom(4)
        head += key
        payload = bytes(b ^ key[i % 4] for i, b in enumerate(payload))
    return bytes(head) + payload


def _recv_exact(sock: socket.socket, n: int) -> bytes:
    buf = bytearray()
    while len(buf) < n:
        chunk = sock.recv(n - len(buf))
        if not chunk:
            raise ConnectionClosed(None, "socket closed")
        buf += chunk
    return bytes(buf)


def read_frame(sock: socket.socket) -> tuple[bool, int, bytes]:
    """Read one frame and return (fin, opcode, unmasked payload)."""
    b0, b1 = _recv_exact(sock, 2)
    n = b1 & 0x7F
    if n == 126:
        (n,) = struct.unpack("!H", _recv_exact(sock, 2))
    elif n == 127:
        (n,) = struct.unpack("!Q", _recv_exact(sock, 8))
    key = _recv_exact(sock, 4) if b1 & 0x80 else None
    payload = _recv_exact(sock, n)
    if key:
        payload = bytes(b ^ key[i % 4] for i, b in enumerate(payload))
    return bool(b0 & 0x80), b0 & 0x0F, payload


class WebSocket:
    """One client connection. send_text is thread-safe; call recv_text from a single reader thread."""

    def __init__(self, sock: socket.socket, mask: bool = True):
        self.sock = sock
        self.mask = mask
        self._send_lock = threading.Lock()
        self.closed = False

    @classmethod
    def connect(cls, host: str, port: int, path: str = "/", headers: dict[str, str] | None = None,
                tls: bool = False, timeout: float = 10.0) -> "WebSocket":
        sock = socket.create_connection((host, port), timeout=timeout)
        try:
            return cls._handshake(sock, host, port, path, headers, tls)
        except BaseException:
            sock.close()
            raise

    @classmethod
    def _handshake(cls, sock: socket.socket, host: str, port: int, path: str, headers: dict[str, str] | None,
                   tls: bool) -> "WebSocket":
        if tls:
            ctx = ssl.create_default_context()
            # The server's certificate is usually self-signed and local, so it is not verified here.
            ctx.check_hostname = False
            ctx.verify_mode = ssl.CERT_NONE
            sock = ctx.wrap_socket(sock, server_hostname=host)
        key = base64.b64encode(os.urandom(16)).decode()
        lines = [f"GET {path} HTTP/1.1", f"Host: {host}:{port}", "Upgrade: websocket", "Connection: Upgrade",
                 f"Sec-WebSocket-Key: {key}", "Sec-WebSocket-Version: 13"]
        lines += [f"{k}: {v}" for k, v in (headers or {}).items()]
        sock.sendall(("\r\n".join(lines) + "\r\n\r\n").encode())
        response = bytearray()
        while b"\r\n\r\n" not in response:
            chunk = sock.recv(4096)
            if not chunk:
                raise WebSocketError("server closed the connection during the handshake")
            response += chunk
            if len(response) > 65536:
                raise WebSocketError("handshake response too long")
        head, _, extra = bytes(response).partition(b"\r\n\r\n")
        status, *header_lines = head.decode("latin-1").split("\r\n")
        if " 101 " not in status + " ":
            raise WebSocketError(f"handshake refused: {status}")
        got = {k.strip().lower(): v.strip() for k, _, v in (h.partition(":") for h in header_lines)}
        if got.get("sec-websocket-accept") != accept_key(key):
            raise WebSocketError("handshake failed: bad Sec-WebSocket-Accept")
        if extra:
            raise WebSocketError("unexpected data after the handshake")
        sock.settimeout(None)
        return cls(sock)

    def _send(self, opcode: int, payload: bytes) -> None:
        with self._send_lock:
            self.sock.sendall(encode_frame(opcode, payload, self.mask))

    def send_text(self, text: str) -> None:
        if self.closed:
            raise ConnectionClosed(None, "already closed")
        self._send(OP_TEXT, text.encode("utf-8"))

    def recv_text(self) -> str:
        """Return the next text message, answering pings on the way. Raises ConnectionClosed."""
        parts: list[bytes] = []
        while True:
            fin, op, payload = read_frame(self.sock)
            if op == OP_PING:
                self._send(OP_PONG, payload)
            elif op == OP_PONG:
                pass
            elif op == OP_CLOSE:
                code = struct.unpack("!H", payload[:2])[0] if len(payload) >= 2 else None
                reason = payload[2:].decode("utf-8", "replace")
                if not self.closed:
                    try:
                        self._send(OP_CLOSE, payload[:2])
                    except OSError:
                        pass
                self.closed = True
                raise ConnectionClosed(code, reason)
            elif op in (OP_TEXT, OP_BINARY, OP_CONT):
                parts.append(payload)
                if fin:
                    return b"".join(parts).decode("utf-8")
            else:
                raise WebSocketError(f"unknown opcode {op}")

    def close(self, code: int = 1000) -> None:
        if not self.closed:
            self.closed = True
            try:
                self._send(OP_CLOSE, struct.pack("!H", code))
            except OSError:
                pass
        try:
            self.sock.close()
        except OSError:
            pass
