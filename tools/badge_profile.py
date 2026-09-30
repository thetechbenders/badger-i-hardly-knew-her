"""Shared profile JSON handling for profilegen.py and badgerctl.py.

FIELD_LIMITS mirrors the firmware field table (firmware/core/settings.cpp);
host test `fields_match_python` fails if the two drift apart.
"""
from __future__ import annotations

import json
from pathlib import Path

MAX_CONTACTS = 6
MAX_PROJECTS = 4

# key -> (type, capacity-or-(min,max)). String capacity includes the NUL.
FIELD_LIMITS: dict[str, tuple] = {
    "name": ("str", 48), "title": ("str", 56), "affiliation": ("str", 56),
    "interests": ("str", 112), "event": ("str", 32),
    **{f"contact{i}.label": ("str", 16) for i in range(1, 7)},
    **{f"contact{i}.value": ("str", 72) for i in range(1, 7)},
    "qr.payload": ("str", 384), "qr.caption": ("str", 40),
    **{f"project{i}.title": ("str", 40) for i in range(1, 5)},
    **{f"project{i}.tagline": ("str", 64) for i in range(1, 5)},
    **{f"project{i}.body": ("str", 200) for i in range(1, 5)},
    **{f"project{i}.link": ("str", 72) for i in range(1, 5)},
    "layout": ("u8", (0, 1)), "refresh.speed": ("u8", (0, 3)), "refresh.partial": ("bool", (0, 1)),
    "refresh.max_partials": ("u8", (0, 20)), "sleep.timeout_s": ("u16", (0, 3600)),
    "sleep.screen": ("u8", (0, 1)), "wake.selects_screen": ("bool", (0, 1)),
    "diag.single_core": ("bool", (0, 1)), "led.level": ("u8", (0, 255)),
    "battery.low_mv": ("u16", (0, 4500)),
    **{f"battery.bar{i}_mv": ("u16", (3000, 4500)) for i in range(1, 5)},
    "battery.hyst_mv": ("u16", (0, 300)), "battery.cal_permille": ("u16", (900, 1100)),
    "gesture.default_on": ("bool", (0, 1)), "gesture.rotation": ("u8", (0, 3)),
    "gesture.mirror": ("bool", (0, 1)), "gesture.sensitivity": ("u8", (10, 90)),
    "gesture.timeout_s": ("u16", (0, 3600)), "gesture.cooldown_ms": ("u16", (200, 3000)),
}


class ProfileError(ValueError):
    pass


def _check_text(key: str, value: str, cap: int) -> str:
    if not isinstance(value, str):
        raise ProfileError(f"{key}: expected a string")
    raw = value.encode("utf-8")
    if len(raw) >= cap:
        raise ProfileError(f"{key}: {len(raw)} bytes, limit {cap - 1}")
    for ch in value:
        o = ord(ch)
        if (o < 0x20 and ch != "\n") or o == 0x7F or 0x80 <= o < 0xA0:
            raise ProfileError(f"{key}: control character U+{o:04X}")
    return value


def flatten(doc: dict) -> list[tuple[str, str]]:
    """Convert a profile document into validated (key, value) pairs."""
    if doc.get("format") != 1:
        raise ProfileError("unsupported profile format (expected 1)")
    p = doc.get("profile", {})
    out: dict[str, str] = {}
    for k in ("name", "title", "affiliation", "interests", "event"):
        if k in p:
            out[k] = p[k]
    contacts = p.get("contacts", [])
    if len(contacts) > MAX_CONTACTS:
        raise ProfileError(f"at most {MAX_CONTACTS} contacts")
    for i in range(MAX_CONTACTS):
        c = contacts[i] if i < len(contacts) else {}
        out[f"contact{i + 1}.label"] = c.get("label", "")
        out[f"contact{i + 1}.value"] = c.get("value", "")
    qr = p.get("qr", {})
    out["qr.payload"] = qr.get("payload", "")
    out["qr.caption"] = qr.get("caption", "")
    projects = p.get("projects", [])
    if len(projects) > MAX_PROJECTS:
        raise ProfileError(f"at most {MAX_PROJECTS} projects")
    for i in range(MAX_PROJECTS):
        pr = projects[i] if i < len(projects) else {}
        for f in ("title", "tagline", "body", "link"):
            out[f"project{i + 1}.{f}"] = pr.get(f, "")
    for k, v in doc.get("prefs", {}).items():
        out[k] = str(int(v)) if not isinstance(v, bool) else ("true" if v else "false")
    pairs = []
    for k, v in out.items():
        if k not in FIELD_LIMITS:
            raise ProfileError(f"unknown key {k}")
        typ, lim = FIELD_LIMITS[k]
        if typ == "str":
            _check_text(k, v, lim)
        else:
            n = 1 if v == "true" else 0 if v == "false" else int(v)
            lo, hi = lim
            if not lo <= n <= hi or (k == "sleep.timeout_s" and 0 < n < 15):
                raise ProfileError(f"{k}: {n} out of range {lo}..{hi}")
        pairs.append((k, v))
    d = dict(pairs)
    bars = [int(d.get(f"battery.bar{i}_mv", dflt)) for i, dflt in zip(range(1, 5), (3600, 3700, 3800, 3950))]
    if any(b <= a for a, b in zip(bars, bars[1:])):
        raise ProfileError("battery.bar1..4_mv must be strictly increasing")
    low = int(d.get("battery.low_mv", 3500))
    if low and low > bars[0]:
        raise ProfileError("battery.low_mv must not exceed battery.bar1_mv")
    payload = out.get("qr.payload", "")
    if payload and not (payload.startswith("https://") or payload.startswith("BEGIN:VCARD")):
        raise ProfileError("qr.payload must be an https:// URL or a BEGIN:VCARD block")
    return pairs


def load(path: Path) -> list[tuple[str, str]]:
    return flatten(json.loads(path.read_text(encoding="utf-8")))
