"""Shared profile JSON handling for profilegen.py and badgerctl.py.

FIELD_LIMITS mirrors the firmware field table (firmware/core/settings.cpp);
host test `fields_match_python` fails if the two drift apart.
"""
from __future__ import annotations

import json
from pathlib import Path

MAX_CONTACTS = 6
MAX_PROJECTS = 12
CONTACT_TYPES = ("", "email", "phone", "web", "github", "discord", "text")
PROJECT_FIELDS = ("title", "tagline", "body", "link", "status", "banner")

# key -> (type, capacity-or-(min,max)). String capacity includes the NUL.
FIELD_LIMITS: dict[str, tuple] = {
    "name": ("str", 48), "title": ("str", 56), "affiliation": ("str", 56),
    "interests": ("str", 112), "event": ("str", 32),
    **{f"contact{i}.label": ("str", 16) for i in range(1, 7)},
    **{f"contact{i}.value": ("str", 72) for i in range(1, 7)},
    **{f"contact{i}.type": ("str", 12) for i in range(1, 7)},
    "qr.payload": ("str", 384), "qr.caption": ("str", 40),
    **{f"project{i}.title": ("str", 40) for i in range(1, 13)},
    **{f"project{i}.tagline": ("str", 64) for i in range(1, 13)},
    **{f"project{i}.body": ("str", 200) for i in range(1, 13)},
    **{f"project{i}.link": ("str", 72) for i in range(1, 13)},
    **{f"project{i}.status": ("str", 48) for i in range(1, 13)},
    **{f"project{i}.banner": ("str", 40) for i in range(1, 13)},
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


# Characters the badge fonts can draw (tools/fontgen.py CODEPOINTS; a test
# keeps the two equal). Anything else is drawn as "?", so the form rejects it.
GLYPHS = frozenset(list(range(0x20, 0x7F)) + list(range(0xA0, 0x100)) +
                   [0x2013, 0x2014, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2026, 0x20AC, 0x2192])

# Fields the renderer wraps over several lines; a line break anywhere else
# would hide the text after it.
MULTILINE_FIELDS = ("interests", "tagline", "body")


class ProfileError(ValueError):
    pass


def is_multiline(key: str) -> bool:
    return key.rsplit(".", 1)[-1] in MULTILINE_FIELDS


def text_problems(pairs) -> list[tuple[str, str]]:
    """Displayed text the badge cannot show as written: characters without a
    glyph, and line breaks in single-line fields. Returns (key, reason)."""
    out = []
    for key, value in pairs:
        if FIELD_LIMITS.get(key, ("",))[0] != "str" or key == "qr.payload" or key.endswith(".type"):
            continue
        missing = sorted({ch for ch in value if ch != "\n" and ord(ch) not in GLYPHS})
        if missing:
            shown = ", ".join(f"'{ch}' (U+{ord(ch):04X})" for ch in missing)
            out.append((key, f"the badge font has no glyph for {shown}; it would be drawn as '?'"))
        if "\n" in value and not is_multiline(key):
            out.append((key, "line break in a single-line field: the text after it would not be shown"))
    return out


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


def valid_link(link: str) -> bool:
    """Same rule as the firmware (settings_text_ok): https, a host, no spaces."""
    return (link.startswith("https://") and len(link) > 8 and link[8] != "/"
            and " " not in link and "\n" not in link)


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
        ctype = c.get("type", "")
        if ctype not in CONTACT_TYPES:
            raise ProfileError(f"contacts[{i}].type {ctype!r}: expected one of {', '.join(t for t in CONTACT_TYPES if t)}")
        out[f"contact{i + 1}.type"] = ctype
    qr = p.get("qr", {})
    out["qr.payload"] = qr.get("payload", "")
    out["qr.caption"] = qr.get("caption", "")
    projects = p.get("projects", [])
    if len(projects) > MAX_PROJECTS:
        raise ProfileError(f"at most {MAX_PROJECTS} projects")
    for i in range(MAX_PROJECTS):
        pr = projects[i] if i < len(projects) else {}
        unknown = set(pr) - set(PROJECT_FIELDS)
        if unknown:
            raise ProfileError(f"projects[{i}]: unknown field(s) {', '.join(sorted(unknown))}")
        if any(pr.get(f) for f in PROJECT_FIELDS) and not pr.get("title"):
            raise ProfileError(f"projects[{i}]: a project needs a title (it would be invisible)")
        link = pr.get("link", "")
        if link and not valid_link(link):
            raise ProfileError(f"projects[{i}].link must be an https:// URL without spaces: {link!r}")
        for f in PROJECT_FIELDS:
            out[f"project{i + 1}.{f}"] = pr.get(f, "")
    for k, v in doc.get("prefs", {}).items():
        if isinstance(v, bool):
            out[k] = "true" if v else "false"
        elif isinstance(v, int):
            out[k] = str(v)
        else:  # no silent truncation of 1.9, and "abc" is a ProfileError, not a traceback
            raise ProfileError(f"prefs.{k}: expected an integer or boolean, got {v!r}")
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
