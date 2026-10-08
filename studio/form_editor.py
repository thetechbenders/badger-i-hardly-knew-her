"""Loss-conscious editing of the existing TOML form, never a second validator.

Known fields are updated in place using tomlkit. Unrelated sections, formatting,
comments and preferences survive. Replacing a contact/project list deliberately
replaces comments *inside* those array-of-table entries; other comments remain.
"""
from __future__ import annotations

import tomllib

import tomlkit

PERSON = ("name", "title", "affiliation", "event", "interests")
QR = ("show", "link", "caption")
CONTACT = ("type", "label", "value", "hidden")
PROJECT = ("title", "tagline", "description", "status", "banner", "link")
CONTACT_TYPES = ("email", "phone", "web", "github", "discord", "text")
QR_MODES = ("none", "link", "vcard", "vcard-from-contacts")


class EditorError(ValueError):
    """A proposed visual edit does not conform to the Studio editing contract."""


def view(toml_text: str) -> dict:
    """Extract only fields managed by the visual editor without mutating TOML."""
    data = tomllib.loads(toml_text)
    person = data.get("person", {})
    qr = data.get("qr", {})
    return {
        "person": {k: person.get(k, [] if k == "interests" else "") for k in PERSON},
        "qr": {k: qr.get(k, "") for k in QR},
        "contacts": [{k: row.get(k, False if k == "hidden" else "") for k in CONTACT}
                     for row in data.get("contacts", [])],
        "projects": [{k: row.get(k, "") for k in PROJECT}
                     for row in data.get("projects", [])],
    }


def apply(toml_text: str, changes: dict) -> str:
    """Apply a complete visual-editor model while preserving other TOML content.

    Only explicitly supported fields are touched. The canonical badge_form
    validator is still mandatory before any resulting document can be saved.
    """
    if not isinstance(changes, dict) or set(changes) != {"person", "qr", "contacts", "projects"}:
        raise EditorError("visual editor requires person, qr, contacts and projects")
    for sec in ("person", "qr"):
        row = changes[sec]
        allowed = PERSON if sec == "person" else QR
        if not isinstance(row, dict) or set(row) != set(allowed):
            raise EditorError(f"{sec}: unsupported or missing fields")
        for k, val in row.items():
            if sec == "person" and k == "interests":
                if not isinstance(val, str) and not (
                    isinstance(val, list) and all(isinstance(item, str) for item in val)
                ):
                    raise EditorError("person.interests must be text or a list of strings")
            elif not isinstance(val, str):
                raise EditorError(f"{sec}.{k}: must be text")
    if changes["qr"]["show"] not in QR_MODES:
        raise EditorError("qr.show: invalid QR mode")
    for sec, fields, cap in (("contacts", CONTACT, 6), ("projects", PROJECT, 12)):
        rows = changes[sec]
        if not isinstance(rows, list) or len(rows) > cap:
            raise EditorError(f"{sec}: maximum {cap} entries")
        for i, row in enumerate(rows, 1):
            if not isinstance(row, dict) or set(row) != set(fields):
                raise EditorError(f"{sec} #{i}: unsupported or missing fields")
            for k, val in row.items():
                if sec == "contacts" and k == "hidden":
                    if not isinstance(val, bool):
                        raise EditorError(f"{sec} #{i}.hidden: must be true/false")
                elif not isinstance(val, str):
                    raise EditorError(f"{sec} #{i}.{k}: must be text")
            if sec == "contacts" and row["type"] not in CONTACT_TYPES:
                raise EditorError(f"contacts #{i}.type: invalid contact type")
    # Do not silently delete unknown fields stored in an array-of-tables.
    # No stable per-row IDs exist yet, so preserve safety over convenience.
    source = tomllib.loads(toml_text)
    for section, allowed in (("contacts", set(CONTACT)), ("projects", set(PROJECT))):
        for index, row in enumerate(source.get(section, []), 1):
            extra = set(row) - allowed
            if extra:
                raise EditorError(f"{section} #{index}: unsupported keys {', '.join(sorted(extra))}; "
                                  "edit them in Advanced TOML until visual support is added")
    document = tomlkit.parse(toml_text)
    for sec in ("person", "qr"):
        table = document.get(sec)
        if table is None:
            table = tomlkit.table()
            document[sec] = table
        for key, value in changes[sec].items():
            # The existing key is replaced, not moved. In particular, notes
            # and fields such as qr.vcard are never deleted.
            table[key] = value
    for sec, fields in (("contacts", CONTACT), ("projects", PROJECT)):
        existing = document.get(sec)
        fresh = tomlkit.aot()
        for i, row in enumerate(changes[sec]):
            tab = tomlkit.table()
            # Preserve unsupported fields in the same row when it survives,
            # but never adopt fields from a *different* row after reordering.
            # The visual editor exposes only known row fields. Any unknown
            # row fields trigger a refusal rather than silent data loss.
            for key in fields:
                tab[key] = row[key]
            fresh.append(tab)
        document[sec] = fresh
    return tomlkit.dumps(document)
