#!/usr/bin/env python3
"""Fill-in personalisation form (TOML) for BHIHKH!.

The form is a commented text file (template: config/badge-form.toml). This
tool turns it into the existing profile document (the JSON model that
badge_profile.flatten validates) plus a processed portrait, previews every
screen with the firmware renderer, and builds the personalised firmware:

  badge_form.py new [local/badge.toml]          copy the template (never overwrites)
  badge_form.py check   FORM [--target T]       validate the form and its files
  badge_form.py preview FORM [--out DIR] [--target T]   + portrait, profile, previews (fit and QR gates)
  badge_form.py build   FORM [--out DIR] [--target T]   + firmware and artifact checks
  badge_form.py export  PROFILE.json --form OUT.toml [--processed PNG | --photo IMG [--settings JSON]]
                                                write an equivalent form for an existing JSON profile

scripts/build-badge.sh wraps `build` (and `preview` with --preview).

--target is the hardware (BHIHKH_TARGET): badger2040 (the default, the
original Badger 2040) or badger2350. It decides the portrait size (104x128 or
104x176), the screens the previews render and the firmware built. One form
serves both badges.

Outputs go to local/out/<form name>/ (git-ignored; local/out/<form
name>-badger2350/ for the Badger 2350) unless --out is given: profile.json
(generated; never edit it), portrait.png, assets.bin, previews/ and fw/. The directory must be new, empty or this tool's own (manifest
.badge-form-output); every output path is checked before anything is
written or deleted, and nothing outside the output directory is written.

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
sys.path.insert(0, str(ROOT / "scripts"))
import badge_profile as prof  # noqa: E402
from bhihkh_targets import DEFAULT as DEFAULT_TARGET, TARGETS  # noqa: E402

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
PORTRAIT_SIZE = (104, 128)  # the Badger 2040's; per target: TARGETS[...].portrait_w/_h
INTERESTS_SEPARATOR = " \u00b7 "

SECTIONS = {
    "form": None, "person": ("name", "title", "affiliation", "event", "interests"),
    "contacts": ("type", "label", "value", "hidden"), "qr": ("show", "link", "vcard", "vcard_file", "caption"),
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
    refs: list[tuple[str, str]] = field(default_factory=list)  # (form field, path as written)
    blocks: dict = field(default_factory=dict)  # "contact"/"project" -> form block number of each kept entry
    target: str = DEFAULT_TARGET   # BHIHKH_TARGET the portrait and outputs are for


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


def load_form(path: Path, text: str | None = None, target: str = DEFAULT_TARGET) -> Form:
    """Validate a form and return the profile document it describes. Every
    problem found is reported at once, each naming its field. `target` is
    the badge it is checked for (portrait size limits)."""
    path = Path(path)
    form_dir = path.resolve().parent
    data = parse_form(path, text)
    problems: list[str] = []
    notes: list[str] = []
    inputs: list[Path] = [path]
    refs: list[tuple[str, str]] = []
    blocks: dict[str, list[int]] = {"contact": [], "project": []}
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
            r = _Reader(problems, "person.interests", {f"item {i}": item}, None)
            v = r.text(f"item {i}", None)
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
        hidden = r.table.get("hidden", False)
        if not isinstance(hidden, bool):
            problems.append(f"contacts #{i}.hidden: expected true or false, got {hidden!r}")
            hidden = False
        if not label and not value and not hidden and (not ctype or isinstance(ctype, str)):
            notes.append(f"contacts #{i} is blank and was left out")
            continue
        where = f"contacts #{i} ({label or ctype or value or 'hidden'})"
        if not isinstance(ctype, str) or ctype not in CONTACT_TYPES:
            problems.append(f"{where}.type: {ctype!r} is not a contact type; use one of "
                            + ", ".join(f'"{t}"' for t in CONTACT_TYPES))
            ctype = "text"
        if hidden:  # kept in its slot but never drawn (e.g. filled in over USB later)
            if value:
                problems.append(f"{where}: hidden = true needs an empty value (a value is always drawn); "
                                "clear the value or remove hidden")
        elif not value:
            problems.append(f"{where}.value: empty; fill it in, set hidden = true to keep the line "
                            "without showing it, or delete this [[contacts]] block")
        elif msg := _contact_value_problem(ctype, value):
            problems.append(f"{where}.value: {msg}")
        if not label and value and ctype in ("email", "phone", "web", "text"):
            notes.append(f"{where} has no label: the value is shown on its own")
        contacts.append({"label": label, "value": value, "type": ctype})
        blocks["contact"].append(i)

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
                refs.append(("qr.vcard_file", fname))
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
        blocks["project"].append(i)

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

    portrait = _portrait(problems, notes, form_dir, data.get("portrait"), target)
    if portrait:
        inputs.append(portrait.path)
        refs.append((f"portrait.{portrait.kind}", data["portrait"][portrait.kind].strip()))
        if isinstance(data["portrait"].get("settings"), str):
            inputs.append(_resolve(form_dir, data["portrait"]["settings"]))
            refs.append(("portrait.settings", data["portrait"]["settings"]))
    doc = {"format": 1, "profile": {**person, "contacts": contacts, "qr": qr_doc, "projects": projects},
           "prefs": prefs}
    if problems:
        raise FormError(problems)
    # The profile model has the final word (limits, link rules, battery order).
    try:
        prof.flatten(doc)
    except prof.ProfileError as e:
        raise FormError([f"profile: {e}"]) from None
    return Form(path=path, doc=doc, portrait=portrait, notes=notes, inputs=inputs, refs=refs, blocks=blocks,
                target=target)


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

def _portrait(problems: list[str], notes: list[str], form_dir: Path, table, target: str = DEFAULT_TARGET) -> Portrait | None:
    tgt = TARGETS[target]
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
        if path.resolve() == PLACEHOLDER.resolve() and target != DEFAULT_TARGET:
            path = placeholder(target)  # the sample silhouette at this badge's size, not a 104x128 one
        _check_processed(problems, path, target)
        if path.resolve() == placeholder(target).resolve():
            notes.append("portrait: using the sample silhouette; set portrait.photo to your own picture")
        return Portrait("processed", path)
    settings = {"size": [tgt.portrait_w, tgt.portrait_h], "gamma": 1.0, "black_pct": 1.0, "white_pct": 2.0,
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
    _check_photo(problems, where, path, settings, target)
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


def placeholder(target: str = DEFAULT_TARGET) -> Path:
    """The sample silhouette at the target's portrait size."""
    return PLACEHOLDER if target == "badger2040" else PLACEHOLDER.with_name(f"portrait_placeholder_{target}.png")


def _check_processed(problems, path: Path, target: str = DEFAULT_TARGET):
    t = TARGETS[target]
    im = _open_image(problems, "portrait.processed", path)
    if im is None:
        return
    w, h = im.size
    if not (8 <= w <= t.portrait_max_w and 8 <= h <= t.display_h):
        problems.append(f"portrait.processed: {w}x{h} pixels; it must be at most {t.portrait_max_w}x{t.display_h} "
                        f"({t.portrait_w}x{t.portrait_h} is the designed size) so the text keeps its room"
                        + ("" if target == DEFAULT_TARGET else f" on the {t.board}"))
    if im.mode != "1":
        rgba = im.convert("RGBA")
        lo, hi = rgba.getchannel("A").getextrema()
        grey = any(rgba.convert("L").histogram()[1:255])
        if lo < 255 or grey:
            problems.append(f"portrait.processed: {path.name} has grey or transparent pixels; a processed "
                            "portrait is pure black and white. Use photo = \"...\" to convert a picture")


def _check_photo(problems, where: str, path: Path, s: dict, target: str = DEFAULT_TARGET):
    t = TARGETS[target]
    if s.get("method") not in PORTRAIT_METHODS:
        problems.append(f"{where}.method: {s.get('method')!r}; use one of "
                        + ", ".join(f'"{m}"' for m in PORTRAIT_METHODS))
    _number(problems, f"{where}.gamma", s.get("gamma"), 0.2, 5.0)
    _number(problems, f"{where}.black_pct", s.get("black_pct"), 0, 40)
    _number(problems, f"{where}.white_pct", s.get("white_pct"), 0, 40)
    _number(problems, f"{where}.sharpen", s.get("sharpen"), 0, 3)
    size = s.get("size")
    if not (isinstance(size, list) and len(size) == 2 and all(type(v) is int for v in size)
            and 8 <= size[0] <= t.portrait_max_w and 8 <= size[1] <= t.display_h):
        problems.append(f"{where}.size: {size!r}; expected [width, height] up to [{t.portrait_max_w}, {t.display_h}] "
                        f"(designed: [{t.portrait_w}, {t.portrait_h}])")
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

def default_out(form_path: Path, target: str = DEFAULT_TARGET) -> Path:
    suffix = "" if target == DEFAULT_TARGET else f"-{target}"
    return ROOT / "local" / "out" / f"{form_path.stem}{suffix}"


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


def host_preview_binary(target: str = DEFAULT_TARGET) -> Path:
    """build/host/badger_preview (build/host-<target>/ for other targets),
    configured as the host tests do (sample content only; the form's content
    is passed per render)."""
    build = ROOT / "build" / ("host" if target == DEFAULT_TARGET else f"host-{target}")
    exe = build / "badger_preview"
    if not (build / "build.ninja").exists():
        res = _run(["cmake", "-S", ROOT / "host", "-B", build, "-G", "Ninja", "-DCMAKE_BUILD_TYPE=Debug",
                    f"-DPython3_EXECUTABLE={sys.executable}", f"-DBHIHKH_TARGET={target}",
                    f"-DBADGER_PROFILE={ROOT / 'config' / 'sample-profile.json'}",
                    f"-DBADGER_PORTRAIT={placeholder(target)}"], capture_output=True, text=True)
        if res.returncode:
            raise SystemExit(f"configuring the host preview failed:\n{res.stdout[-2000:]}{res.stderr[-2000:]}")
    res = _run(["cmake", "--build", build, "--target", "badger_preview"], capture_output=True, text=True)
    if res.returncode:
        raise SystemExit(f"building the host preview failed:\n{res.stdout[-3000:]}")
    return exe


FIELD_NAMES = {"name": "person.name", "title": "person.title", "affiliation": "person.affiliation",
               "interests": "person.interests", "event": "person.event", "qr.caption": "qr.caption",
               "qr.payload": "qr"}


def form_field(key: str, doc: dict, blocks: dict | None = None) -> str:
    """Settings key -> the form field a user edits. `blocks` maps each kept
    contact/project to its [[block]] number in the form (blank blocks are
    left out of the profile, so the slot number can be smaller)."""
    if key in FIELD_NAMES:
        return FIELD_NAMES[key]
    m = re.fullmatch(r"(contact|project)(\d+)\.(\w+)", key)
    if not m:
        return key
    n = int(m[2])
    shown = (blocks or {}).get(m[1], [])
    num = shown[n - 1] if n - 1 < len(shown) else n
    if m[1] == "contact":
        c = doc["profile"]["contacts"][n - 1]
        return f"contacts #{num} ({c['label'] or c['type']}).{m[3]}"
    title = doc["profile"]["projects"][n - 1].get("title", "")
    field_ = {v: k for k, v in PROJECT_KEYS.items()}[m[3]]
    return f"projects #{num} ({title}).{field_}"


OUT_MARK = ".badge-form-output"   # JSON manifest: which outputs this tool wrote here
OUTPUTS = ("profile.json", "portrait.png", "portrait-methods_x3.png", "assets.bin", "previews", "fw",
           "fw-build.log", "fw-verify.txt")


def _legacy_output(out: Path) -> bool:
    """A directory written by the first version of this tool (no manifest yet):
    its profile.json carries the generated marker."""
    p = out / "profile.json"
    if p.is_symlink() or not p.is_file():
        return False
    try:
        return GENERATED_MARK in json.loads(p.read_text(encoding="utf-8"))
    except (json.JSONDecodeError, UnicodeDecodeError, TypeError):
        return False


def _manifest(out: Path) -> dict | None:
    mark = out / OUT_MARK
    if mark.is_symlink() or not mark.is_file():
        return None
    try:
        m = json.loads(mark.read_text(encoding="utf-8"))
    except (json.JSONDecodeError, UnicodeDecodeError):
        return None
    return m if isinstance(m, dict) and m.get("tool") == "tools/badge_form.py" else None


def preflight_out(out: Path, form: Form) -> None:
    """Check every path this tool would write or delete in `out` before
    touching any of them. Refused: an `out` that is a symlink or a file; a
    non-empty directory without this tool's manifest; an output path that is a
    symlink (it would redirect the write) or exists without being listed in
    the manifest; and an output path that is, or contains, one of the form's
    own input files. On refusal nothing has been written."""
    problems = []
    if out.is_symlink():
        problems.append(f"--out {out} is a symbolic link; give the real directory")
    elif out.exists() and not out.is_dir():
        problems.append(f"--out {out} is a file, not a directory")
    else:
        owned: set = set()
        if out.is_dir() and any(out.iterdir()):
            m = _manifest(out)
            if m is None and not (out / OUT_MARK).exists() and _legacy_output(out):
                m = {"owned": [n for n in OUTPUTS if (out / n).exists()]}  # adopt the earlier output
            if m is None:
                problems.append(f"{out} already holds files this tool did not write; they are left alone. "
                                "Choose an empty or new directory with --out")
            else:
                owned = set(m.get("owned", []))
        if not problems:
            if (out / OUT_MARK).is_symlink():
                problems.append(f"{out / OUT_MARK} is a symbolic link")
            for name in OUTPUTS:
                p = out / name
                if p.is_symlink():
                    problems.append(f"{p} is a symbolic link; writing there would change another file")
                elif p.exists() and name not in owned:
                    problems.append(f"{p} exists and was not written by this tool; it is left alone")
    out_r = out.resolve()
    for f in form.inputs:
        fr = f.resolve()
        for name in OUTPUTS + (OUT_MARK,):
            target = out_r / name
            if fr == target or target in fr.parents:
                problems.append(f"{f} is one of the form's inputs but also an output path of --out "
                                f"({target}); move it, or choose another --out")
    if problems:
        raise FormError(problems)


def _own(out: Path, name: str) -> None:
    """Record `name` in the manifest before writing it."""
    m = _manifest(out) or {"tool": "tools/badge_form.py", "owned": []}
    if name not in m["owned"]:
        m["owned"].append(name)
    (out / OUT_MARK).write_text(json.dumps(m, indent=1) + "\n", encoding="utf-8")


def claim_out(out: Path, form: Form) -> None:
    preflight_out(out, form)
    out.mkdir(parents=True, exist_ok=True)
    if _manifest(out) is None:
        legacy = [n for n in OUTPUTS if (out / n).exists()] if _legacy_output(out) else []
        _own(out, OUT_MARK)
        for n in legacy:
            _own(out, n)


def preview(form: Form, out: Path) -> dict:
    """Portrait, profile, asset pack and previews, with the text-fit and QR
    gates. Raises FormError with field names when something would not show."""
    import render_previews as rp
    claim_out(out, form)
    for name in ("portrait.png", "portrait-methods_x3.png", "profile.json", "assets.bin", "previews"):
        _own(out, name)
    portrait = prepare_portrait(form, out)
    profile = write_profile(form, out)
    pack = out / "assets.bin"
    res = _run([sys.executable, TOOLS / "assetpack.py", "build", "--target", form.target, "--portrait", portrait,
                "--out", pack], capture_output=True, text=True)
    if res.returncode:
        raise FormError([f"portrait: asset pack: {res.stderr.strip() or res.stdout.strip()}"])
    pairs = prof.flatten(form.doc)
    problems = [f"{form_field(k, form.doc, form.blocks)}: {why}" for k, why in prof.text_problems(pairs)]
    exe = host_preview_binary(form.target)
    screens, projects = rp.fit_reports(exe, pairs, pack)
    for key, where in rp.fit_problems(screens, projects, pairs):
        problems.append(f"{form_field(key, form.doc, form.blocks)}: does not fit on the {where} (it would be cut "
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
        raise FormError(qr_failures(form, bad) or [f"previews failed:\n{res.stdout[-2000:]}{res.stderr[-2000:]}"])
    return {"profile": profile, "portrait": portrait, "pack": pack, "previews": previews,
            "sheet": previews / "contact_sheet.png", "report": report}


def qr_failures(form: Form, screens: list[str]) -> list[str]:
    """Messages for QR screens that did not decode, naming the form block.
    `project-qr_N` is the N-th configured project (blank blocks left out)."""
    msgs = []
    for name in screens:
        if name.startswith("project-qr"):
            n = int(name.split("_")[1]) if "_" in name else 1
            shown = form.blocks.get("project", [])
            num = shown[n - 1] if n - 1 < len(shown) else n
            title = form.doc["profile"]["projects"][n - 1].get("title", "")
            msgs.append(f"projects #{num} ({title}).link: its QR code did not decode back to the link")
        else:
            msgs.append(f"qr: the {name} QR code did not decode back to its content (too long to draw?); "
                        "shorten the link or vCard")
    return msgs


def build_firmware(prepared: dict, out: Path, target: str = DEFAULT_TARGET) -> Path:
    for name in ("fw", "fw-build.log", "fw-verify.txt"):
        if (out / name).is_symlink():
            raise FormError([f"{out / name} is a symbolic link; writing there would change another file"])
        _own(out, name)
    fw = out / "fw"
    log = out / "fw-build.log"
    env = {**os.environ, "BUILD_DIR": str(fw)}
    with open(log, "w") as f:
        res = _run([ROOT / "scripts" / "build-firmware.sh", f"-DBHIHKH_TARGET={target}",
                    f"-DPython3_EXECUTABLE={sys.executable}",
                    f"-DBADGER_PROFILE={prepared['profile']}", f"-DBADGER_PORTRAIT={prepared['portrait']}"],
                   stdout=f, stderr=subprocess.STDOUT, env=env)
    if res.returncode:
        tail = log.read_text(errors="replace").splitlines()[-30:]
        raise SystemExit("firmware build failed (log: {}):\n{}".format(log, "\n".join(tail)))
    res = _run([sys.executable, ROOT / "scripts" / "verify_artifacts.py", fw, "--target", target],
               capture_output=True, text=True)
    (out / "fw-verify.txt").write_text(res.stdout + res.stderr)
    if res.returncode:
        raise SystemExit(f"artifact checks failed:\n{res.stdout}{res.stderr}")
    return fw


# ------------------------------------------------------------------ backup

# rsync --exclude patterns of scripts/private-backup.sh, relative to local/
# (a test keeps the two equal). Files there are not archived.
BACKUP_EXCLUDES = ("/backups/", "/out/*/fw/", "/out/*/fw-*", "/out/*/previews/")


def _excluded(rel: str) -> str | None:
    """The BACKUP_EXCLUDES pattern that leaves `rel` (a path inside local/) out."""
    import fnmatch
    parts = rel.split("/")
    for pat in BACKUP_EXCLUDES:
        pp = pat.strip("/").split("/")
        is_dir = pat.endswith("/")
        if len(parts) >= len(pp) + is_dir and all(fnmatch.fnmatchcase(a, b) for a, b in zip(parts, pp)):
            return pat
    return None


def _path_inside_local(path: Path, local: Path) -> str | None:
    """`path` relative to local/ (resolved) without resolving links at or
    below local/, or None. Measured from the topmost ancestor that resolves to
    local/, so a link above it (macOS: /var is /private/var; a symlinked
    checkout) does not count, whatever spelling the path arrives in."""
    lexical = Path(os.path.abspath(path))
    for ancestor in reversed(lexical.parents):
        if ancestor.resolve() == local:
            return lexical.relative_to(ancestor).as_posix()
    return None


def backup_problems(form: Form, root: Path = ROOT) -> list[str]:
    """Why scripts/private-backup.sh could not restore this form exactly:
    every file it reads must be archived (inside local/, outside the excluded
    directories) and referenced relative to the form, so a restored copy reads
    the archived files rather than the original ones."""
    local = (root / "local").resolve()
    out = []
    for field_, raw in form.refs:
        if Path(raw).expanduser().is_absolute() or raw.startswith("~") or re.match(r"^[A-Za-z]:[\\/]", raw):
            out.append(f"{field_}: {raw!r} is an absolute path; write it relative to the form "
                       "(e.g. \"photo.jpg\") so a restored backup uses its own copy")
    for f in [form.path] + form.inputs[1:]:
        try:
            rel = f.resolve().relative_to(local).as_posix()
        except ValueError:
            out.append(f"{f}: outside local/; copy it into local/ so the backup holds it")
            continue
        lex = _path_inside_local(f, local)
        if lex != rel:  # a symlink on the way: the archive would hold the link, not the file
            out.append(f"{f}: reached through a symbolic link ({lex or f} -> {rel}); backups copy links, "
                       "not their targets. Refer to the real file in local/ instead")
            continue
        if "\n" in rel:
            out.append(f"{f}: line break in the path")
        elif pat := _excluded(rel):
            out.append(f"{f}: inside local{pat.rstrip('*')}, which backups leave out; move it elsewhere in local/")
    return out


def write_backup_inputs(path: Path, form: str, out: str) -> None:
    """INPUTS.json of a form backup: the form and its output directory, as
    paths relative to the repository (any characters, including spaces)."""
    path.write_text(json.dumps({"mode": "form", "form": form, "out": out}, indent=1) + "\n", encoding="utf-8")


def read_backup_inputs(archive_dir: Path) -> dict:
    """{"mode": "json"} or {"mode": "form", "form": ..., "out": ...} for an
    extracted backup: INPUTS.json when present; otherwise BUILDINFO's
    "inputs" line (absent or "json": the JSON workflow; "form <form> <out>"
    from the first form backups, accepted only when exactly one split gives
    a .toml form and its default output directory local/out/<form name>)."""
    meta = archive_dir / "INPUTS.json"
    if meta.is_file():
        m = json.loads(meta.read_text(encoding="utf-8"))
        if m.get("mode") == "form" and isinstance(m.get("form"), str) and isinstance(m.get("out"), str):
            return m
        if m.get("mode") == "json":
            return {"mode": "json"}
        raise ValueError(f"{meta}: unknown backup metadata {m!r}")
    line = next((l for l in (archive_dir / "BUILDINFO").read_text(encoding="utf-8").splitlines()
                 if l.startswith("inputs ")), "inputs json")
    rest = line[len("inputs "):]
    if rest == "json":
        return {"mode": "json"}
    if rest.startswith("form "):
        words = rest[len("form "):].split(" ")
        splits = [(" ".join(words[:k]), " ".join(words[k:])) for k in range(1, len(words))]
        good = [(f, o) for f, o in splits if f.endswith(".toml") and o == f"local/out/{Path(f).stem}"]
        if len(good) == 1:
            return {"mode": "form", "form": good[0][0], "out": good[0][1]}
    raise ValueError(f"BUILDINFO: cannot tell the form and output paths apart in {line!r}")


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
    contacts = list(p.get("contacts", []))
    while contacts and not any(contacts[-1].get(k) for k in ("label", "value", "type")):
        contacts.pop()  # trailing empty slots: the same as no entry
    for c in contacts:
        out += ["", "[[contacts]]", f"type = {_toml_str(c.get('type') or 'text')}",
                f"label = {_toml_str(c.get('label', ''))}", f"value = {_toml_str(c.get('value', ''))}"]
        if not c.get("value"):  # kept in its slot, never drawn (as in the JSON)
            out.append("hidden = true")
    qr = p.get("qr", {})
    payload = qr.get("payload", "")
    out += ["", "[qr]"]
    if not payload:
        out.append('show = "none"')
    elif payload.startswith("BEGIN:VCARD"):
        out += ['show = "vcard"', f"vcard = {_toml_str(payload)}"]
    else:
        out += ['show = "link"', f"link = {_toml_str(payload)}"]
    if qr.get("caption") and payload:
        out.append(f"caption = {_toml_str(qr['caption'])}")
    elif qr.get("caption"):  # never shown without a code; keep it for later
        out.append(f"# caption = {_toml_str(qr['caption'])}   (not shown without a QR code)")
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
    i = sub.add_parser("inputs", help="list every file the form reads (for CMake)")
    i.add_argument("form", type=Path)
    b = sub.add_parser("backup-check", help="can scripts/private-backup.sh archive and restore this form?")
    b.add_argument("form", type=Path)
    b.add_argument("--root", type=Path, default=ROOT, help="repository holding local/ (e.g. a restored backup)")
    bm = sub.add_parser("backup-meta", help="read (DIR KEY) or write (--write FILE FORM OUT) backup metadata")
    bm.add_argument("args", nargs="+")
    bm.add_argument("--write", action="store_true")
    for name in ("check", "preview", "build"):
        s = sub.add_parser(name)
        s.add_argument("form", type=Path)
        s.add_argument("--out", type=Path, help="output directory (default: local/out/<form name>, "
                       "local/out/<form name>-<target> for targets other than badger2040)")
        s.add_argument("--target", choices=sorted(TARGETS), default=DEFAULT_TARGET,
                       help=f"hardware to build for (default {DEFAULT_TARGET})")
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
        rel = os.path.relpath(args.form)
        shown = args.form if rel.startswith("..") else rel
        print(f"created {shown}\nnext: fill it in, put your photo next to it, then run\n"
              f"  scripts/build-badge.sh --preview {shown}\n  scripts/build-badge.sh {shown}")
        return 0

    if args.cmd == "backup-meta":
        if args.write:
            dest, form_rel, out_rel = args.args
            write_backup_inputs(Path(dest), form_rel, out_rel)
            return 0
        archive_dir, key = args.args
        try:
            print(read_backup_inputs(Path(archive_dir)).get(key, ""))
        except ValueError as e:
            print(f"error: {e}", file=sys.stderr)
            return 1
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
        contacts = doc.get("profile", {}).get("contacts", [])
        untyped = [c.get("label") or c.get("value") for c in contacts if c.get("value") and not c.get("type")]
        unshown = [c.get("label") or "(empty)" for c in contacts if not c.get("value")
                   and any(c.get(k) for k in ("label", "value", "type"))]
        gaps = [i for i, pr in enumerate(doc.get("profile", {}).get("projects", []), 1) if not any(pr.values())]
        args.form.parent.mkdir(parents=True, exist_ok=True)
        args.form.write_text(export_form(doc, lines, args.profile.name), encoding="utf-8")
        print(f"wrote {args.form}")
        if untyped:
            print(f"note: untyped contacts {untyped} became type = \"text\" (drawn the same way)")
        if unshown:
            print(f"note: contacts {unshown} have no value: kept as hidden = true (never drawn, as before)")
        if gaps:
            print(f"note: empty project entries {gaps} were left out; later projects move up a slot "
                  "(the badge shows the same pages)")
        qr = doc.get("profile", {}).get("qr", {})
        if qr.get("caption") and not qr.get("payload"):
            print("note: the QR caption is commented out: without a QR code it is never shown")
        return 0

    try:
        form = load_form(args.form, target=getattr(args, "target", DEFAULT_TARGET))
    except FormError as err:
        _report(err, args.form)
        return 1
    if args.cmd == "inputs":
        for f in form.inputs:
            print(f.resolve())
        return 0
    if args.cmd == "backup-check":
        problems = backup_problems(form, args.root)
        if problems:
            _report(FormError(problems), args.form)
        return 1 if problems else 0
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
    out = (args.out or default_out(args.form, form.target)).resolve()
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
    t = TARGETS[form.target]
    fw = build_firmware(prepared, out, form.target)
    print(f"firmware OK for the {t.board} (artifact checks passed): {fw / (t.artifact + '.uf2')}")
    if form.target == "badger2040":
        print("flash: hold BOOT/USR, tap RST, copy the .uf2 onto the RPI-RP2 drive (see docs/INSTALL.md first)")
    else:
        print("flash: on the back, hold BOOT, tap RESET, copy the .uf2 onto the RP2350 drive (see docs/INSTALL.md first)")
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
