#!/usr/bin/env python3
"""Talk to the badge over its USB serial (CDC) port.

  badgerctl.py [--port PORT] cmd "status"          run one CLI command
  badgerctl.py [--port PORT] push local/profile.json [--no-commit]
  badgerctl.py [--port PORT] backup settings-backup.txt   (replayable export)
  badgerctl.py [--port PORT] restore settings-backup.txt
  badgerctl.py [--port PORT] shell                  interactive passthrough

PORT: /dev/ttyACM0 (Linux/WSL with usbipd), COM5 (Windows), or auto-detect
by USB VID:PID 2E8A:000A (Raspberry Pi Pico SDK stdio). Requires pyserial.
"""
from __future__ import annotations

import argparse
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import badge_profile  # noqa: E402


def open_port(port: str | None):
    import serial
    import serial.tools.list_ports
    if not port:
        cands = [p.device for p in serial.tools.list_ports.comports() if (p.vid, p.pid) == (0x2E8A, 0x000A)]
        if not cands:
            raise SystemExit("badge not found; pass --port")
        port = cands[0]
    s = serial.Serial(port, 115200, timeout=0.2)
    time.sleep(0.1)
    s.reset_input_buffer()
    return s


def run(s, line: str, timeout: float = 10.0) -> list[str]:
    """Send one command and return its output lines; raise on ERR."""
    s.write((line + "\n").encode("utf-8"))
    out, buf, deadline = [], b"", time.time() + timeout
    while time.time() < deadline:
        buf += s.read(4096)
        while b"\n" in buf:
            raw, buf = buf.split(b"\n", 1)
            text = raw.decode("utf-8", "replace").rstrip("\r")
            if text == "OK":
                return out
            if text.startswith("ERR "):
                raise RuntimeError(f"{line.split()[0]}: {text[4:]}")
            out.append(text)
    raise TimeoutError(f"no response to {line!r}")


def quote(v: str) -> str:
    return '"' + v.replace("\\", "\\\\").replace('"', '\\"').replace("\n", "\\n") + '"'


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--port")
    sub = ap.add_subparsers(dest="op", required=True)
    c = sub.add_parser("cmd")
    c.add_argument("line", nargs="+")
    p = sub.add_parser("push")
    p.add_argument("profile", type=Path)
    p.add_argument("--no-commit", action="store_true")
    b = sub.add_parser("backup")
    b.add_argument("file", type=Path)
    r = sub.add_parser("restore")
    r.add_argument("file", type=Path)
    sub.add_parser("shell")
    a = ap.parse_args(argv)

    if a.op == "push":
        pairs = badge_profile.load(a.profile)  # validate before touching the device
    s = open_port(a.port)
    try:
        if a.op == "shell":
            import threading
            s.write(b"echo on\n")
            threading.Thread(target=lambda: [sys.stdout.write(s.read(256).decode("utf-8", "replace")) or
                                              sys.stdout.flush() for _ in iter(int, 1)], daemon=True).start()
            for line in sys.stdin:
                s.write(line.encode("utf-8"))
            return 0
        run(s, "echo off")
        if a.op == "cmd":
            print("\n".join(run(s, " ".join(a.line))))
        elif a.op == "push":
            for k, v in pairs:
                run(s, f"set {k} {quote(v)}")
            print(f"staged {len(pairs)} values")
            if not a.no_commit:
                print("\n".join(run(s, "commit")))
        elif a.op == "backup":
            lines = run(s, "export")
            a.file.write_text("\n".join(lines) + "\n", encoding="utf-8")
            print(f"wrote {len(lines)} settings to {a.file}")
        elif a.op == "restore":
            n = 0
            for line in a.file.read_text(encoding="utf-8").splitlines():
                if line.startswith("set "):
                    run(s, line)
                    n += 1
            print(f"staged {n} values")
            print("\n".join(run(s, "commit")))
    finally:
        s.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
