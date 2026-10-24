"""Read and update server.properties (Java properties format) without losing comments or order."""

from __future__ import annotations

from pathlib import Path


def unescape(text: str) -> str:
    out: list[str] = []
    i = 0
    while i < len(text):
        c = text[i]
        if c != "\\" or i + 1 == len(text):
            out.append(c)
            i += 1
            continue
        n = text[i + 1]
        if n == "u" and i + 6 <= len(text):
            out.append(chr(int(text[i + 2:i + 6], 16)))
            i += 6
            continue
        out.append({"t": "\t", "n": "\n", "r": "\r", "f": "\f"}.get(n, n))
        i += 2
    return "".join(out)


def escape(text: str, key: bool = False) -> str:
    out: list[str] = []
    for i, c in enumerate(text):
        if c == "\\":
            out.append("\\\\")
        elif c in "=:#!":
            out.append("\\" + c)
        elif c == "\n":
            out.append("\\n")
        elif c == "\r":
            out.append("\\r")
        elif c == "\t":
            out.append("\\t")
        elif c == " " and (key or i == 0):
            out.append("\\ ")
        elif ord(c) > 0x7E or ord(c) < 0x20:
            out.append(f"\\u{ord(c):04x}")
        else:
            out.append(c)
    return "".join(out)


def _split(line: str) -> tuple[str, str] | None:
    """Return (key, value) for a property line, or None for blanks and comments."""
    s = line.lstrip()
    if not s or s[0] in "#!":
        return None
    i = 0
    while i < len(s):
        if s[i] == "\\":
            i += 2
            continue
        if s[i] in "=: \t":
            break
        i += 1
    key = s[:i]
    rest = s[i:].lstrip(" \t")
    if rest[:1] in ("=", ":"):
        rest = rest[1:].lstrip(" \t")
    return unescape(key), unescape(rest)


def parse(text: str) -> dict[str, str]:
    props: dict[str, str] = {}
    for line in text.splitlines():
        kv = _split(line)
        if kv:
            props[kv[0]] = kv[1]
    return props


def read(path: Path) -> dict[str, str]:
    try:
        return parse(path.read_text(encoding="utf-8"))
    except FileNotFoundError:
        return {}


def update(path: Path, values: dict[str, str]) -> None:
    """Set keys in place, keeping every other line as it is; new keys go at the end."""
    try:
        lines = path.read_text(encoding="utf-8").splitlines()
    except FileNotFoundError:
        lines = ["#Minecraft server properties"]
    pending = dict(values)
    for n, line in enumerate(lines):
        kv = _split(line)
        if kv and kv[0] in pending:
            lines[n] = f"{escape(kv[0], key=True)}={escape(pending.pop(kv[0]))}"
    lines += [f"{escape(k, key=True)}={escape(v)}" for k, v in pending.items()]
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")
