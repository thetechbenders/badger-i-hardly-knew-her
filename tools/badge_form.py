#!/usr/bin/env python3
"""Fill-in personalisation form (TOML) for BHIHKH!.

The form is a commented text file (template: config/badge-form.toml). This
tool turns it into the existing profile document (the JSON model that
badge_profile.flatten validates) plus a processed portrait, previews every
screen with the firmware renderer, and builds the personalised firmware:

  badge_form.py new [local/badge.toml]          copy the template (never overwrites)
  badge_form.py check   FORM                    validate the form and its files
  badge_form.py preview FORM [--out DIR]        + portrait, profile, previews (fit and QR gates)
  badge_form.py build   FORM [--out DIR]        + RP2040 firmware and artifact checks
  badge_form.py export  PROFILE.json --form OUT.toml [--processed PNG | --photo IMG [--settings JSON]]
                                                write an equivalent form for an existing JSON profile

scripts/build-badge.sh wraps `build` (and `preview` with --preview).

Outputs go to local/out/<form name>/ (git-ignored) unless --out is given:
profile.json (generated; never edit it), portrait.png, assets.bin, previews/
and fw/. An existing profile.json there that this tool did not write is
never overwritten, and nothing outside the output directory is written.

Paths in the form (photo, vCard file) are relative to the form file. The
original photo is only read: crop, scale and global tone operations are
applied to a copy (tools/portrait.py), nothing is retouched.
"""
from __future__ import annotations

import argparse
import difflib
import json
import os
import re
import shutil
import subprocess
import sys
from dataclasses import dataclass, field
from pathlib import Path

TOOLS = Path(__file__).resolve().parent
ROOT = TOOLS.parent
sys.path.insert(0, str(TOOLS))
import badge_profile as prof  # noqa: E402

try:
    import tomllib
except ModuleNotFoundError:  # Python 3.10: same parser as the 'tomli' package
    try:
        import tomli as tomllib  # type: ignore[no-redef]
    except ModuleNotFoundError:
        tomllib = None

FORM_VERSION = 1
TEMPLATE = ROOT / "config" / "badge-form.toml"
DEFAULT_FORM = ROOT / "local" / "badge.toml"
PLACEHOLDER = ROOT / "assets" / "sample" / "portrait_placeholder.png"
GENERATED_MARK = "_generated_by"

CONTACT_TYPES = ("email", "phone", "web", "github", "discord", "text")
QR_CHOICES = ("none", "link", "vcard", "vcard-from-contacts")
PORTRAIT_METHODS = ("atkinson", "floyd", "bayer8", "threshold")
PORTRAIT_SIZE = (104, 128)
INTERESTS_SEPARATOR = " \u00b7 "

SECTIONS = {
    "form": None, "person": ("name", "title", "affiliation", "event", "interests"),
    "contacts": ("type", "label", "value"), "qr": ("show", "link", "vcard", "vcard_file", "caption"),
    "portrait": ("photo", "processed", "settings", "crop", "size", "method", "gamma", "black_pct",
                 "white_pct", "sharpen"),
    "projects": ("title", "tagline", "description", "status", "banner", "link"),
    "preferences": None,
}
PROJECT_KEYS = {"title": "title", "tagline": "tagline", "description": "body", "status": "status",
                "banner": "banner", "link": "link"}
PREF_KEYS = {k: v for k, v in prof.FIELD_LIMITS.items() if v[0] != "str"}


class FormError(ValueError):
    """One or more problems with the form; each line names the field."""

    def __init__(self, problems: list[str]):
        super().__init__("\n".join(problems))
        self.problems = problems


@dataclass
class Portrait:
    kind: str                      # "photo", "processed"
    path: Path
    settings: dict = field(default_factory=dict)   # photo: tools/portrait.py settings


@dataclass
class Form:
    path: Path
    doc: dict                      # profile document for badge_profile.flatten
    portrait: Portrait | None
    notes: list[str] = field(default_factory=list)
    inputs: list[Path] = field(default_factory=list)   # every file the form reads


# ------------------------------------------------------------------ parsing

def _suggest(key: str, allowed) -> str:
    m = difflib.get_close_matches(key, list(allowed), n=1)
    return f' Did you mean "{m[0]}"?' if m else f" Allowed: {', '.join(allowed)}."


def _bytes_hint(text: str, cap: int) -> str:
    n = len(text.encode("utf-8"))
    extra = "" if n == len(text) else " (letters such as é count 2 bytes, symbols such as \u00b7 or \u2192 2-3)"
    return f"{n} bytes, the limit is {cap - 1}: shorten it by {n - cap + 1} bytes{extra}"


def _clean(text: str, multiline: bool) -> str:
    """Strip the ends (and each line's trailing spaces) of supplied text; the
    TOML layout around triple-quoted text is not content."""
    text = text.replace("\r\n", "\n").replace("\r", "\n")
    if multiline:
        text = "\n".join(line.rstrip() for line in text.split("\n"))
    return text.strip()


def _resolve(form_dir: Path, raw: str) -> Path:
    """A form path: relative to the form, ~ expanded, and a Windows path
    (C:\\Users\\...) mapped to /mnt/c/... when running under WSL."""
    m = re.match(r"^([A-Za-z]):[\\/](.*)$", raw)
    if m and Path(f"/mnt/{m[1].lower()}").is_dir():
        return Path(f"/mnt/{m[1].lower()}") / m[2].replace("\\", "/")
    p = Path(raw).expanduser()
    return p if p.is_absolute() else form_dir / p


class _Reader:
    """Typed access to one form table, recording problems by field name."""

    def __init__(self, problems: list[str], where: str, table, allowed):
        self.problems, self.where, self.allowed = problems, where, allowed
        if not isinstance(table, dict):
            problems.append(f"{where}: expected a table of fields, got {type(table).__name__}")
            table = {}
        self.table = table
        for k in table:
            if allowed is not None and k not in allowed:
                problems.append(f"{where}: unknown field \"{k}\".{_suggest(k, allowed)}")

    def name(self, key: str) -> str:
        return f"{self.where}.{key}"

    def text(self, key: str, cap: int | None, multiline: bool = False, required: bool = False) -> str:
        v = self.table.get(key, "")
        if not isinstance(v, str):
            self.problems.append(f"{self.name(key)}: expected text in quotes, got {_typename(v)} {v!r}")
            return ""
        v = _clean(v, multiline)
        if required and not v:
            self.problems.append(f"{self.name(key)}: required; fill it in")
        if cap is not None and len(v.encode("utf-8")) >= cap:
            self.problems.append(f"{self.name(key)}: {_bytes_hint(v, cap)}")
        for ch in v:
            o = ord(ch)
            if (o < 0x20 and ch != "\n") or o == 0x7F or 0x80 <= o < 0xA0:
                self.problems.append(f"{self.name(key)}: control character U+{o:04X} (a tab?); use spaces")
                break
        if "\n" in v and not multiline:
            self.problems.append(f"{self.name(key)}: must be one line; remove the line break")
        missing = sorted({ch for ch in v if ch != "\n" and ord(ch) not in prof.GLYPHS})
        if missing:
            shown = ", ".join(f"'{ch}' (U+{ord(ch):04X})" for ch in missing)
            self.problems.append(f"{self.name(key)}: the badge font cannot draw {shown}; "
                                 "use Latin letters, digits and common punctuation")
        return v


def _typename(v) -> str:
    return {bool: "true/false", int: "a number", float: "a number", list: "a list", dict: "a table"}.get(
        type(v), type(v).__name__)


def parse_form(path: Path, text: str | None = None) -> dict:
    """Parse the TOML; syntax errors name the line."""
    if tomllib is None:
        raise FormError(["Python 3.11 or newer is needed to read the form (or: pip install tomli)"])
    if text is None:
        try:
            text = path.read_text(encoding="utf-8")
        except FileNotFoundError:
            raise FormError([f"{path}: form not found (copy config/badge-form.toml, see README)"]) from None
        except UnicodeDecodeError as e:
            raise FormError([f"{path}: not UTF-8 text ({e.reason}); save the form as UTF-8"]) from None
    text = text.lstrip("\ufeff")  # Notepad's UTF-8 byte-order mark
    try:
        data = tomllib.loads(text)
    except tomllib.TOMLDecodeError as e:
        raise FormError([f"{path}: {e} (check quotes and brackets on that line)"]) from None
    return data


def load_form(path: Path, text: str | None = None) -> Form:
    """Validate a form and return the profile document it describes. Every
    problem found is reported at once, each naming its field."""
    path = Path(path)
    form_dir = path.resolve().parent
    data = parse_form(path, text)
    problems: list[str] = []
    notes: list[str] = []
    inputs: list[Path] = [path]
    _Reader(problems, "top level", data, tuple(SECTIONS))
    if data.get("form") != FORM_VERSION:
        problems.append(f"form: expected `form = {FORM_VERSION}` at the top (got {data.get('form')!r})")
    for sec in ("person", "qr", "portrait", "preferences"):
        if sec in data and not isinstance(data[sec], dict):
            problems.append(f"{sec}: write it as a [{sec}] section")
    for sec in ("contacts", "projects"):
        if sec in data and not (isinstance(data[sec], list) and all(isinstance(x, dict) for x in data[sec])):
            problems.append(f"{sec}: write each entry as a [[{sec}]] block")
            data[sec] = []
    lim = prof.FIELD_LIMITS

    # Identity: all five keys are always present (blank = not shown).
    pr = _Reader(problems, "person", data.get("person", {}), SECTIONS["person"])
    person = {
        "name": pr.text("name", lim["name"][1], required=True),
        "title": pr.text("title", lim["title"][1], required=True),
        "affiliation": pr.text("affiliation", lim["affiliation"][1]),
        "interests": "",
        "event": pr.text("event", lim["event"][1]),
    }
    interests = pr.table.get("interests", "")
    if isinstance(interests, list):
        items = []
        for i, item in enumerate(interests, 1):
            if not isinstance(item, str):
                problems.append(f"person.interests item {i}: expected text in quotes, got {_typename(item)}")
                continue
            r = _Reader(problems, f"person.interests item {i}", {"v": item}, None)
            v = r.text("v", None)
            if v:
                items.append(v)
            elif item.strip() == "":
                problems.append(f"person.interests item {i}: empty; remove it from the list")
        joined = INTERESTS_SEPARATOR.join(items)
        if len(joined.encode()) >= lim["interests"][1]:
            problems.append(f"person.interests: joined with ' \u00b7 ' that is "
                            f"{_bytes_hint(joined, lim['interests'][1])}")
        person["interests"] = joined
    else:
        person["interests"] = pr.text("interests", lim["interests"][1], multiline=True)

    # Contacts, in form order.
    contacts = []
    raw_contacts = data.get("contacts", [])
    if len(raw_contacts) > prof.MAX_CONTACTS:
        problems.append(f"contacts: {len(raw_contacts)} [[contacts]] blocks, the badge holds at most "
                        f"{prof.MAX_CONTACTS}; remove {len(raw_contacts) - prof.MAX_CONTACTS}")
    for i, c in enumerate(raw_contacts[:prof.MAX_CONTACTS], 1):
        r = _Reader(problems, f"contacts #{i}", c, SECTIONS["contacts"])
        ctype = r.table.get("type", "")
        label = r.text("label", lim["contact1.label"][1])
        value = r.text("value", lim["contact1.value"][1])
        if not label and not value and (not ctype or isinstance(ctype, str)):
            notes.append(f"contacts #{i} is blank and was left out")
            continue
        where = f"contacts #{i} ({label or ctype or value})"
        if not isinstance(ctype, str) or ctype not in CONTACT_TYPES:
            problems.append(f"{where}.type: {ctype!r} is not a contact type; use one of "
                            + ", ".join(f'"{t}"' for t in CONTACT_TYPES))
            ctype = "text"
        if not value:
            problems.append(f"{where}.value: empty; fill it in or delete this [[contacts]] block")
        elif msg := _contact_value_problem(ctype, value):
            problems.append(f"{where}.value: {msg}")
        if not label and ctype in ("email", "phone", "web", "text"):
            notes.append(f"{where} has no label: the value is shown on its own")
        contacts.append({"label": label, "value": value, "type": ctype})

    # QR code shown on the card.
    qr_doc = {"payload": "", "caption": ""}
    qr = _Reader(problems, "qr", data.get("qr", {}), SECTIONS["qr"])
    show = qr.table.get("show", "none")
    caption = qr.text("caption", lim["qr.caption"][1])
    if show not in QR_CHOICES:
        problems.append(f"qr.show: {show!r} is not a choice; use one of " + ", ".join(f'"{c}"' for c in QR_CHOICES))
        show = "none"
    used = {k for k in ("link", "vcard", "vcard_file") if qr.table.get(k)}
    wanted = {"none": set(), "link": {"link"}, "vcard": {"vcard", "vcard_file"},
              "vcard-from-contacts": set()}[show]
    for k in sorted(used - wanted):
        problems.append(f"qr.{k}: set, but qr.show is \"{show}\"; clear it or change qr.show")
    payload = ""
    if show == "link":
        payload = qr.text("link", lim["qr.payload"][1])
        if not payload:
            problems.append("qr.link: required when qr.show = \"link\"; e.g. \"https://example.com/you\"")
        elif not prof.valid_link(payload):
            problems.append(f"qr.link: {payload!r} must be an https:// address without spaces")
    elif show == "vcard":
        if qr.table.get("vcard") and qr.table.get("vcard_file"):
            problems.append("qr: set either qr.vcard or qr.vcard_file, not both")
        elif qr.table.get("vcard_file"):
            fname = qr.table["vcard_file"]
            if not isinstance(fname, str):
                problems.append(f"qr.vcard_file: expected a file name in quotes, got {_typename(fname)}")
            else:
                vpath = _resolve(form_dir, fname)
                inputs.append(vpath)
                try:
                    payload = _clean(vpath.read_text(encoding="utf-8-sig"), True)
                except FileNotFoundError:
                    problems.append(f"qr.vcard_file: {vpath} not found (paths are relative to the form)")
                except (UnicodeDecodeError, IsADirectoryError) as e:
                    problems.append(f"qr.vcard_file: {vpath} is not a UTF-8 text file ({e})")
        else:
            v = qr.table.get("vcard", "")
            if not isinstance(v, str):
                problems.append(f"qr.vcard: expected text in triple quotes, got {_typename(v)}")
            else:
                payload = _clean(v, True)
            if not payload:
                problems.append("qr.vcard: required when qr.show = \"vcard\" (or set qr.vcard_file)")
        if payload:
            if msg := _vcard_problem(payload):
                problems.append(f"qr.vcard: {msg}")
    elif show == "vcard-from-contacts":
        payload = make_vcard(person, contacts)
    if payload and len(payload.encode()) >= lim["qr.payload"][1]:
        problems.append(f"qr: the QR content is {_bytes_hint(payload, lim['qr.payload'][1])}"
                        + ("; drop contact lines from the vCard" if show.startswith("vcard") else ""))
    if caption and show == "none":
        problems.append("qr.caption: there is no QR code to describe; clear it or set qr.show")
    qr_doc = {"payload": payload, "caption": caption}

    # Portfolio, in form order.
    projects = []
    raw_projects = data.get("projects", [])
    if len(raw_projects) > prof.MAX_PROJECTS:
        problems.append(f"projects: {len(raw_projects)} [[projects]] blocks, the badge holds at most "
                        f"{prof.MAX_PROJECTS}; remove {len(raw_projects) - prof.MAX_PROJECTS}")
    for i, p in enumerate(raw_projects[:prof.MAX_PROJECTS], 1):
        title = p.get("title") if isinstance(p.get("title"), str) else ""
        r = _Reader(problems, f"projects #{i}" + (f" ({title.strip()})" if title.strip() else ""), p,
                    SECTIONS["projects"])
        entry = {}
        for k, model in PROJECT_KEYS.items():
            v = r.text(k, lim[f"project1.{model}"][1], multiline=prof.is_multiline(model))
            if v:
                entry[model] = v
        if not entry:
            notes.append(f"projects #{i} is blank and was left out")
            continue
        if "title" not in entry:
            problems.append(f"{r.where}.title: required (a project without a title is never shown)")
        if entry.get("link") and not prof.valid_link(entry["link"]):
            problems.append(f"{r.where}.link: {entry['link']!r} must be an https:// address without spaces, "
                            "e.g. \"https://github.com/you/project\"")
        projects.append(entry)

    # Preferences (optional; same names as the USB settings keys).
    prefs = {}
    raw_prefs = data.get("preferences", {})
    for key, v in _flatten_table(raw_prefs if isinstance(raw_prefs, dict) else {}):
        name = f"preferences.{key}"
        if key not in PREF_KEYS:
            problems.append(f"{name}: unknown setting.{_suggest(key, PREF_KEYS)}")
            continue
        typ, (lo, hi) = PREF_KEYS[key]
        if typ == "bool":
            if not isinstance(v, bool):
                problems.append(f"{name}: expected true or false, got {v!r}")
                continue
        elif isinstance(v, bool) or not isinstance(v, int):
            problems.append(f"{name}: expected a whole number {lo}..{hi}, got {v!r}")
            continue
        elif not lo <= v <= hi or (key == "sleep.timeout_s" and 0 < v < 15):
            extra = " (0 = never, otherwise at least 15)" if key == "sleep.timeout_s" else ""
            problems.append(f"{name}: {v} is outside {lo}..{hi}{extra}")
            continue
        prefs[key] = v

    portrait = _portrait(problems, notes, form_dir, data.get("portrait"))
    if portrait:
        inputs.append(portrait.path)
        if isinstance(data["portrait"].get("settings"), str):
            inputs.append(_resolve(form_dir, data["portrait"]["settings"]))
    doc = {"format": 1, "profile": {**person, "contacts": contacts, "qr": qr_doc, "projects": projects},
           "prefs": prefs}
    if problems:
        raise FormError(problems)
    # The profile model has the final word (limits, link rules, battery order).
    try:
        prof.flatten(doc)
    except prof.ProfileError as e:
        raise FormError([f"profile: {e}"]) from None
    return Form(path=path, doc=doc, portrait=portrait, notes=notes, inputs=inputs)


def load_doc(path: Path) -> dict:
    """Profile document only (for badge_profile.load, badgerctl push)."""
    return load_form(path).doc


def _flatten_table(t: dict, prefix: str = ""):
    for k, v in t.items():
        key = f"{prefix}{k}"
        if isinstance(v, dict):
            yield from _flatten_table(v, key + ".")
        else:
            yield key, v


_PHONE = re.compile(r"^\+?[0-9 ()./-]+$")
_GITHUB = re.compile(r"^[A-Za-z0-9](?:[A-Za-z0-9]|-(?=[A-Za-z0-9])){0,38}$")
_DISCORD = re.compile(r"^[A-Za-z0-9_.]{2,32}(#[0-9]{4})?$")


def _contact_value_problem(ctype: str, value: str) -> str | None:
    if ctype == "email" and not re.fullmatch(r"[^@\s]+@[^@\s]+\.[^@\s]+", value):
        return f"{value!r} is not an email address (name@example.com)"
    if ctype == "phone" and (not _PHONE.fullmatch(value) or sum(c.isdigit() for c in value) < 3):
        return f"{value!r} is not a phone number (digits, spaces, + ( ) - . / only)"
    if ctype == "web" and (" " in value or "." not in value):
        return f"{value!r} is not a web address (example.com/you, no spaces)"
    if ctype == "github":
        if "/" in value or value.startswith("@"):
            return f"{value!r}: write only the GitHub user name (e.g. \"octocat\"), not a link or @name"
        if not _GITHUB.fullmatch(value):
            return f"{value!r} is not a GitHub user name (letters, digits, single hyphens, up to 39)"
    if ctype == "discord" and not _DISCORD.fullmatch(value.lstrip("@")):
        return f"{value!r} is not a Discord user name (2-32 letters, digits, _ or .)"
    if ctype == "discord" and value.startswith("@"):
        return f"{value!r}: write the Discord user name without the @"
    return None


def _vcard_problem(payload: str) -> str | None:
    lines = payload.split("\n")
    if lines[0].strip().upper() != "BEGIN:VCARD" or lines[-1].strip().upper() != "END:VCARD":
        return "a vCard starts with a BEGIN:VCARD line and ends with an END:VCARD line"
    if not any(line.upper().startswith("VERSION:") for line in lines):
        return "the vCard needs a VERSION:3.0 line"
    for ch in payload:
        o = ord(ch)
        if (o < 0x20 and ch != "\n") or o == 0x7F:
            return f"control character U+{o:04X} in the vCard"
    return None


def _vc(text: str) -> str:
    return text.replace("\\", "\\\\").replace(",", "\\,").replace(";", "\\;")


def make_vcard(person: dict, contacts: list[dict]) -> str:
    """A minimal vCard 3.0 from the name, title, affiliation and contacts."""
    given, _, family = person["name"].rpartition(" ") if " " in person["name"] else ("", "", person["name"])
    out = ["BEGIN:VCARD", "VERSION:3.0", f"N:{_vc(family)};{_vc(given)};;;", f"FN:{_vc(person['name'])}"]
    if person["title"]:
        out.append(f"TITLE:{_vc(person['title'])}")
    if person["affiliation"]:
        out.append(f"ORG:{_vc(person['affiliation'])}")
    for c in contacts:
        v = c["value"]
        if c["type"] == "email":
            out.append(f"EMAIL:{v}")
        elif c["type"] == "phone":
            out.append(f"TEL:{v}")
        elif c["type"] == "web":
            out.append(f"URL:{v if v.startswith(('http://', 'https://')) else 'https://' + v}")
        elif c["type"] == "github":
            out.append(f"URL:https://github.com/{v}")
        elif c["type"] == "discord":
            out.append(f"NOTE:Discord {_vc(v)}")
    out.append("END:VCARD")
    return "\n".join(out)


# ----------------------------------------------------------------- portrait

def _portrait(problems: list[str], notes: list[str], form_dir: Path, table) -> Portrait | None:
    if table is None:
        problems.append("portrait: missing; add a [portrait] section with photo = \"your-photo.jpg\"")
        return None
    r = _Reader(problems, "portrait", table, SECTIONS["portrait"])
    t = r.table
    given = [k for k in ("photo", "processed") if t.get(k)]
    if len(given) != 1:
        problems.append("portrait: set exactly one of photo = \"...\" (a normal picture, converted for you) "
                        "or processed = \"...\" (a ready black-and-white portrait)")
        return None
    kind = given[0]
    raw = t[kind]
    if not isinstance(raw, str):
        problems.append(f"portrait.{kind}: expected a file name in quotes, got {_typename(raw)}")
        return None
    path = _resolve(form_dir, raw.strip())
    tone_keys = [k for k in SECTIONS["portrait"] if k not in ("photo", "processed") and k in t]
    if kind == "processed":
        for k in tone_keys:
            problems.append(f"portrait.{k}: only applies to photo = \"...\"; remove it")
        _check_processed(problems, path)
        if path.resolve() == PLACEHOLDER.resolve():
            notes.append("portrait: using the sample silhouette; set portrait.photo to your own picture")
        return Portrait("processed", path)
    settings = {"size": list(PORTRAIT_SIZE), "gamma": 1.0, "black_pct": 1.0, "white_pct": 2.0,
                "sharpen": 0.6, "method": "atkinson"}
    if "settings" in t:
        sraw = t["settings"]
        spath = _resolve(form_dir, sraw) if isinstance(sraw, str) else None
        for k in tone_keys:
            if k != "settings":
                problems.append(f"portrait.{k}: set it in the settings file {sraw!r} instead (or drop settings)")
        try:
            loaded = json.loads(spath.read_text(encoding="utf-8")) if spath else None
        except FileNotFoundError:
            problems.append(f"portrait.settings: {spath} not found (paths are relative to the form)")
            loaded = {}
        except (json.JSONDecodeError, UnicodeDecodeError) as e:
            problems.append(f"portrait.settings: {spath} is not valid JSON ({e})")
            loaded = {}
        if loaded is None:
            problems.append("portrait.settings: expected a file name in quotes")
            loaded = {}
        if not isinstance(loaded, dict):
            problems.append(f"portrait.settings: {spath} must hold a JSON object")
            loaded = {}
        unknown = set(loaded) - {"crop", "size", "gamma", "black_pct", "white_pct", "sharpen", "method"}
        for k in sorted(unknown):
            problems.append(f"portrait.settings: unknown key {k!r} in {spath}")
        settings.update({k: v for k, v in loaded.items() if k not in unknown})
        where = f"portrait.settings ({spath.name if spath else sraw})"
    else:
        settings.update({k: t[k] for k in tone_keys})
        where = "portrait"
    _check_photo(problems, where, path, settings)
    return Portrait("photo", path, settings)


def _number(problems, name, v, lo, hi) -> bool:
    if isinstance(v, bool) or not isinstance(v, (int, float)):
        problems.append(f"{name}: expected a number {lo}..{hi}, got {v!r}")
        return False
    if not lo <= v <= hi:
        problems.append(f"{name}: {v} is outside {lo}..{hi}")
        return False
    return True


def _open_image(problems, name, path: Path):
    try:
        from PIL import Image, ImageOps, UnidentifiedImageError
    except ModuleNotFoundError:
        problems.append("Pillow is not installed: python3 -m pip install -r tools/requirements.txt")
        return None
    if not path.exists():
        problems.append(f"{name}: {path} not found (paths are relative to the form file)")
        return None
    if not path.is_file():
        problems.append(f"{name}: {path} is not a file")
        return None
    try:
        with Image.open(path) as im:
            im.load()
            return ImageOps.exif_transpose(im)
    except (UnidentifiedImageError, OSError) as e:
        problems.append(f"{name}: {path.name} is not a picture Pillow can read ({e}); use JPEG or PNG")
        return None


def _check_processed(problems, path: Path):
    im = _open_image(problems, "portrait.processed", path)
    if im is None:
        return
    w, h = im.size
    if not (8 <= w <= 148 and 8 <= h <= 128):
        problems.append(f"portrait.processed: {w}x{h} pixels; it must be at most 148x128 "
                        f"(104x128 is the designed size) so the text keeps its room")
    if im.mode != "1":
        rgba = im.convert("RGBA")
        lo, hi = rgba.getchannel("A").getextrema()
        values = set(rgba.convert("L").getdata())
        if lo < 255 or not values <= {0, 255}:
            problems.append(f"portrait.processed: {path.name} has grey or transparent pixels; a processed "
                            "portrait is pure black and white. Use photo = \"...\" to convert a picture")


def _check_photo(problems, where: str, path: Path, s: dict):
    if s.get("method") not in PORTRAIT_METHODS:
        problems.append(f"{where}.method: {s.get('method')!r}; use one of "
                        + ", ".join(f'"{m}"' for m in PORTRAIT_METHODS))
    _number(problems, f"{where}.gamma", s.get("gamma"), 0.2, 5.0)
    _number(problems, f"{where}.black_pct", s.get("black_pct"), 0, 40)
    _number(problems, f"{where}.white_pct", s.get("white_pct"), 0, 40)
    _number(problems, f"{where}.sharpen", s.get("sharpen"), 0, 3)
    size = s.get("size")
    if not (isinstance(size, list) and len(size) == 2 and all(type(v) is int for v in size)
            and 8 <= size[0] <= 148 and 8 <= size[1] <= 128):
        problems.append(f"{where}.size: {size!r}; expected [width, height] up to [148, 128] (designed: [104, 128])")
        return
    im = _open_image(problems, "portrait.photo", path)
    if im is None:
        return
    W, H = im.size
    crop = s.get("crop")
    if crop is None:
        s["crop"] = crop = default_crop((W, H), tuple(size))
    if not (isinstance(crop, list) and len(crop) == 4 and all(type(v) is int for v in crop)):
        problems.append(f"{where}.crop: {crop!r}; expected [x, y, width, height] in whole photo pixels")
        return
    x, y, w, h = crop
    if w <= 0 or h <= 0 or x < 0 or y < 0 or x + w > W or y + h > H:
        problems.append(f"{where}.crop: {crop} reaches outside the {W}x{H} photo")
        return
    want = size[0] / size[1]
    if abs((w / h) / want - 1) > 0.03:
        problems.append(f"{where}.crop: {w}x{h} would stretch the face; keep width:height at "
                        f"{size[0]}:{size[1]}, e.g. [{x}, {y}, {w}, {round(w / want)}]")
    if w < size[0] or h < size[1]:
        problems.append(f"{where}.crop: {w}x{h} photo pixels is smaller than the {size[0]}x{size[1]} portrait; "
                        "use a larger photo or a larger crop (pictures are never enlarged)")


def default_crop(image: tuple[int, int], size: tuple[int, int]) -> list[int]:
    """The largest crop with the portrait's aspect ratio, centred."""
    W, H = image
    w, h = W, round(W * size[1] / size[0])
    if h > H:
        w, h = round(H * size[0] / size[1]), H
    return [(W - w) // 2, (H - h) // 2, w, h]


def prepare_portrait(form: Form, out: Path) -> Path:
    """Write out/portrait.png (and a method comparison for photos). The
    source file is only read."""
    p = form.portrait
    dest = out / "portrait.png"
    if p.kind == "processed":
        from PIL import Image
        with Image.open(p.path) as im:
            im = im.convert("1", dither=Image.Dither.NONE) if im.mode != "1" else im.copy()
        im.save(dest)
        return dest
    import portrait as pt
    s = p.settings
    try:
        g = pt.load_gray(p.path, tuple(s["crop"]), tuple(s["size"]), s["gamma"], s["black_pct"],
                         s["white_pct"], s["sharpen"])
    except SystemExit as e:  # portrait.py reports bad input this way
        raise FormError([f"portrait.photo: {e}"]) from None
    pt.to_image(pt.convert(g, s["method"])).save(dest)
    from PIL import Image
    gray = Image.fromarray((g * 255).round().astype("uint8"))
    sheet = {"photo (grey)": gray, **{m: pt.to_image(pt.convert(g, m)) for m in pt.METHODS}}
    pt.comparison_sheet(sheet, 3).save(out / "portrait-methods_x3.png")
    return dest


# ---------------------------------------------------------- build pipeline

def default_out(form_path: Path) -> Path:
    return ROOT / "local" / "out" / form_path.stem


def write_profile(form: Form, out: Path) -> Path:
    """out/profile.json, marked as generated; refuses to replace a file it did not write."""
    dest = out / "profile.json"
    if dest.exists():
        try:
            old = json.loads(dest.read_text(encoding="utf-8"))
        except (json.JSONDecodeError, UnicodeDecodeError):
            old = {}
        if not (isinstance(old, dict) and GENERATED_MARK in old):
            raise FormError([f"{dest} exists and was not generated from a form; it is left alone. "
                             "Move it, or pass --out to use another directory"])
    doc = {GENERATED_MARK: "tools/badge_form.py", "_source": form.path.name,
           "_comment": "Generated from the form above; edit the form, not this file.", **form.doc}
    text = json.dumps(doc, indent=2, ensure_ascii=False) + "\n"
    if not dest.exists() or dest.read_text(encoding="utf-8") != text:
        dest.write_text(text, encoding="utf-8")
    return dest


def _run(cmd, **kw) -> subprocess.CompletedProcess:
    return subprocess.run([str(c) for c in cmd], **kw)


def host_preview_binary() -> Path:
    """build/host/badger_preview, configured as the host tests do (sample
    content only; the form's content is passed per render)."""
    build = ROOT / "build" / "host"
    exe = build / "badger_preview"
    if not (build / "build.ninja").exists():
        res = _run(["cmake", "-S", ROOT / "host", "-B", build, "-G", "Ninja", "-DCMAKE_BUILD_TYPE=Debug",
                    f"-DPython3_EXECUTABLE={sys.executable}",
                    f"-DBADGER_PROFILE={ROOT / 'config' / 'sample-profile.json'}",
                    f"-DBADGER_PORTRAIT={PLACEHOLDER}"], capture_output=True, text=True)
        if res.returncode:
            raise SystemExit(f"configuring the host preview failed:\n{res.stdout[-2000:]}{res.stderr[-2000:]}")
    res = _run(["cmake", "--build", build, "--target", "badger_preview"], capture_output=True, text=True)
    if res.returncode:
        raise SystemExit(f"building the host preview failed:\n{res.stdout[-3000:]}")
    return exe


FIELD_NAMES = {"name": "person.name", "title": "person.title", "affiliation": "person.affiliation",
               "interests": "person.interests", "event": "person.event", "qr.caption": "qr.caption",
               "qr.payload": "qr"}


def form_field(key: str, doc: dict) -> str:
    """Settings key -> the form field a user edits."""
    if key in FIELD_NAMES:
        return FIELD_NAMES[key]
    m = re.fullmatch(r"(contact|project)(\d+)\.(\w+)", key)
    if not m:
        return key
    n = int(m[2])
    if m[1] == "contact":
        c = doc["profile"]["contacts"][n - 1]
        return f"contacts #{n} ({c['label'] or c['type']}).{m[3]}"
    title = doc["profile"]["projects"][n - 1].get("title", "")
    field_ = {v: k for k, v in PROJECT_KEYS.items()}[m[3]]
    return f"projects #{n} ({title}).{field_}"


def preview(form: Form, out: Path) -> dict:
    """Portrait, profile, asset pack and previews, with the text-fit and QR
    gates. Raises FormError with field names when something would not show."""
    import render_previews as rp
    out.mkdir(parents=True, exist_ok=True)
    portrait = prepare_portrait(form, out)
    profile = write_profile(form, out)
    pack = out / "assets.bin"
    res = _run([sys.executable, TOOLS / "assetpack.py", "build", "--portrait", portrait, "--out", pack],
               capture_output=True, text=True)
    if res.returncode:
        raise FormError([f"portrait: asset pack: {res.stderr.strip() or res.stdout.strip()}"])
    pairs = prof.flatten(form.doc)
    problems = [f"{form_field(k, form.doc)}: {why}" for k, why in prof.text_problems(pairs)]
    exe = host_preview_binary()
    screens, projects = rp.fit_reports(exe, pairs, pack)
    for key, where in rp.fit_problems(screens, projects, pairs):
        problems.append(f"{form_field(key, form.doc)}: does not fit on the {where} (it would be cut "
                        "or left out); shorten it")
    if problems:
        raise FormError(problems)
    form.notes += rp.fit_notes(screens, pairs)
    previews = out / "previews"
    if previews.exists():
        shutil.rmtree(previews)
    res = _run([sys.executable, TOOLS / "render_previews.py", "--preview", exe, "--out", previews,
                "--profile", profile, "--pack", pack], capture_output=True, text=True)
    report_path = previews / "qr_report.json"
    report = json.loads(report_path.read_text()) if report_path.exists() else {"screens": {}}
    bad = [s for s, r in report["screens"].items() if r["ok"] is False]
    if res.returncode or bad:
        msgs = []
        for s in bad:
            if s.startswith("project-qr"):
                n = int(s.split("_")[1]) if "_" in s else 1
                msgs.append(f"projects #{n}.link: its QR code did not decode back to the link")
            else:
                msgs.append(f"qr: the {s} QR code did not decode back to its content (too long to draw?); "
                            "shorten the link or vCard")
        raise FormError(msgs or [f"previews failed:\n{res.stdout[-2000:]}{res.stderr[-2000:]}"])
    return {"profile": profile, "portrait": portrait, "pack": pack, "previews": previews,
            "sheet": previews / "contact_sheet.png", "report": report}


def build_firmware(prepared: dict, out: Path) -> Path:
    fw = out / "fw"
    log = out / "fw-build.log"
    env = {**os.environ, "BUILD_DIR": str(fw)}
    with open(log, "w") as f:
        res = _run([ROOT / "scripts" / "build-firmware.sh", f"-DPython3_EXECUTABLE={sys.executable}",
                    f"-DBADGER_PROFILE={prepared['profile']}", f"-DBADGER_PORTRAIT={prepared['portrait']}"],
                   stdout=f, stderr=subprocess.STDOUT, env=env)
    if res.returncode:
        tail = log.read_text(errors="replace").splitlines()[-30:]
        raise SystemExit("firmware build failed (log: {}):\n{}".format(log, "\n".join(tail)))
    res = _run([sys.executable, ROOT / "scripts" / "verify_artifacts.py", fw], capture_output=True, text=True)
    (out / "fw-verify.txt").write_text(res.stdout + res.stderr)
    if res.returncode:
        raise SystemExit(f"artifact checks failed:\n{res.stdout}{res.stderr}")
    return fw


# ------------------------------------------------------------------ export

def _toml_str(s: str) -> str:
    if "\n" in s and "'''" not in s and all(ord(c) >= 0x20 or c == "\n" for c in s) \
            and not s.endswith("'"):
        return "'''\n" + s + "'''"
    return json.dumps(s, ensure_ascii=False)  # a JSON string is a valid TOML basic string


def export_form(doc: dict, portrait_lines: list[str], source: str) -> str:
    """An equivalent form for a profile document (badge_profile.flatten gives
    the same pairs for both, given typed contacts)."""
    prof.flatten(doc)
    p = doc.get("profile", {})
    out = [f"# BHIHKH! badge form, exported from {source} by tools/badge_form.py.",
           "# Every field is explained in config/badge-form.toml. Build with:",
           "#   scripts/build-badge.sh <this file>", "", f"form = {FORM_VERSION}", "", "[person]"]
    for k in ("name", "title", "affiliation", "event", "interests"):
        out.append(f"{k} = {_toml_str(p.get(k, ''))}")
    for c in p.get("contacts", []):
        if not (c.get("label") or c.get("value")):
            continue
        out += ["", "[[contacts]]", f"type = {_toml_str(c.get('type') or 'text')}",
                f"label = {_toml_str(c.get('label', ''))}", f"value = {_toml_str(c.get('value', ''))}"]
    qr = p.get("qr", {})
    payload = qr.get("payload", "")
    out += ["", "[qr]"]
    if not payload:
        out.append('show = "none"')
    elif payload.startswith("BEGIN:VCARD"):
        out += ['show = "vcard"', f"vcard = {_toml_str(payload)}"]
    else:
        out += ['show = "link"', f"link = {_toml_str(payload)}"]
    if qr.get("caption"):
        out.append(f"caption = {_toml_str(qr['caption'])}")
    out += ["", "[portrait]", *portrait_lines]
    for pr in p.get("projects", []):
        if not any(pr.values()):
            continue
        out += ["", "[[projects]]"]
        for k, model in PROJECT_KEYS.items():
            if pr.get(model):
                out.append(f"{k} = {_toml_str(pr[model])}")
    if doc.get("prefs"):
        out += ["", "[preferences]"]
        for k, v in doc["prefs"].items():
            out.append(f'"{k}" = {json.dumps(v)}')
    return "\n".join(out) + "\n"


# --------------------------------------------------------------------- CLI

def _report(e: FormError, path) -> None:
    print(f"\n{path}: {len(e.problems)} problem(s) to fix:", file=sys.stderr)
    for msg in e.problems:
        print(f"  - {msg}", file=sys.stderr)


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    n = sub.add_parser("new", help="copy the template to a new form (never overwrites)")
    n.add_argument("form", type=Path, nargs="?", default=DEFAULT_FORM)
    i = sub.add_parser("inputs", help="list every file the form reads (for scripts/private-backup.sh)")
    i.add_argument("form", type=Path)
    for name in ("check", "preview", "build"):
        s = sub.add_parser(name)
        s.add_argument("form", type=Path)
        s.add_argument("--out", type=Path, help="output directory (default: local/out/<form name>)")
    e = sub.add_parser("export", help="write an equivalent form for an existing profile JSON")
    e.add_argument("profile", type=Path)
    e.add_argument("--form", type=Path, required=True, help="form to write (never overwritten)")
    g = e.add_mutually_exclusive_group()
    g.add_argument("--processed", type=Path, help="existing 1-bit portrait PNG")
    g.add_argument("--photo", type=Path, help="original photo")
    e.add_argument("--settings", type=Path, help="tools/portrait.py settings JSON for --photo")
    args = ap.parse_args(argv)

    if args.cmd == "new":
        if args.form.exists():
            print(f"{args.form} already exists; not overwritten. Edit it, or choose another name.")
            return 1
        args.form.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(TEMPLATE, args.form)
        print(f"created {args.form}\nnext: fill it in, put your photo next to it, then run\n"
              f"  scripts/build-badge.sh {args.form}")
        return 0

    if args.cmd == "export":
        if args.form.exists():
            print(f"{args.form} already exists; not overwritten.", file=sys.stderr)
            return 1
        doc = json.loads(args.profile.read_text(encoding="utf-8"))
        base = args.form.resolve().parent

        def rel(p: Path) -> str:
            return os.path.relpath(p.resolve(), base).replace(os.sep, "/")
        if args.processed:
            lines = [f"processed = {_toml_str(rel(args.processed))}"]
        elif args.photo:
            lines = [f"photo = {_toml_str(rel(args.photo))}"]
            if args.settings:
                lines.append(f"settings = {_toml_str(rel(args.settings))}")
        else:
            lines = ['photo = ""   # fill in: your picture, relative to this file']
        untyped = [c.get("label") or c.get("value") for c in doc.get("profile", {}).get("contacts", [])
                   if (c.get("label") or c.get("value")) and not c.get("type")]
        args.form.parent.mkdir(parents=True, exist_ok=True)
        args.form.write_text(export_form(doc, lines, args.profile.name), encoding="utf-8")
        print(f"wrote {args.form}")
        if untyped:
            print(f"note: untyped contacts {untyped} became type = \"text\" (drawn the same way)")
        return 0

    try:
        form = load_form(args.form)
    except FormError as err:
        _report(err, args.form)
        return 1
    if args.cmd == "inputs":
        for f in form.inputs:
            print(f.resolve())
        return 0
    print(f"form OK: {args.form}")
    p = form.doc["profile"]
    print(f"  {p['name']} - {p['title']}; {len(p['contacts'])} contact(s), "
          f"{len(p['projects'])} project(s): " + ", ".join(x["title"] for x in p["projects"]))
    print(f"  portrait: {form.portrait.kind} {form.portrait.path.resolve()}")
    if form.portrait.kind == "photo":
        print(f"  crop {form.portrait.settings['crop']} -> {form.portrait.settings['size']}, "
              f"{form.portrait.settings['method']}")
    _warn_tracked(args.form, form)
    if args.cmd == "check":
        for note in form.notes:
            print(f"note: {note}")
        return 0
    out = (args.out or default_out(args.form)).resolve()
    if out == (ROOT / "local").resolve():
        print("--out must be a directory of its own, not local/ itself", file=sys.stderr)
        return 1
    try:
        prepared = preview(form, out)
    except FormError as err:
        _report(err, args.form)
        return 1
    for note in form.notes:
        print(f"note: {note}")
    print(f"previews OK (every field fits, QR codes decode): {prepared['sheet']}")
    print(f"  one PNG per screen in {prepared['previews'] / 'x3'} (3x) and {prepared['previews'] / 'native'}")
    print(f"  generated profile: {prepared['profile']}")
    if args.cmd == "preview":
        return 0
    fw = build_firmware(prepared, out)
    print(f"firmware OK (artifact checks passed): {fw / 'badger_badge.uf2'}")
    print("flash: hold BOOT/USR, tap RST, copy the .uf2 onto the RPI-RP2 drive (see docs/INSTALL.md first)")
    return 0


def _warn_tracked(form_path: Path, form: Form) -> None:
    """Personal files belong in local/ (git-ignored); say so if they are not ignored."""
    files = [form_path] + ([form.portrait.path] if form.portrait else [])
    for f in files:
        try:
            f.resolve().relative_to(ROOT)
        except ValueError:
            continue  # outside the repository: Git never sees it
        if f.resolve() in (TEMPLATE.resolve(), PLACEHOLDER.resolve()):
            continue
        res = subprocess.run(["git", "-C", str(ROOT), "check-ignore", "-q", str(f.resolve())])
        if res.returncode == 1:
            print(f"warning: {f} is inside the repository but not git-ignored; keep personal files in local/")


if __name__ == "__main__":
    sys.exit(main())
