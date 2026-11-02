#!/usr/bin/env python3
"""Private Studio badge package v1: strict, portable, single-target build input.

Package contains a saved TOML form, one managed 1-bit portrait, and hashes.
It deliberately contains no admission build inputs or firmware images.
"""
from __future__ import annotations

import argparse
import hashlib
import io
import json
import os
from pathlib import Path
import re
import stat
import subprocess
import sys
import tempfile
import zipfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from bhihkh_targets import TARGETS  # noqa: E402
import badge_form  # noqa: E402

MAX_PACKAGE = 3 * 1024 * 1024
MAX_TOML = 100_000
MAX_PORTRAIT = 1024 * 1024
PORTRAIT_NAME = re.compile(r"(?:sample|portrait)-(?:badger2040|badger2350)(?:-[0-9a-f]{64})?\.png\Z")
SCHEMA = "bhihkh.badgerpack"
VERSION = 1


class PackageError(ValueError):
    pass


def digest(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def _portrait_name(toml: bytes, target: str) -> str:
    try:
        doc = badge_form.parse_form(Path("badge.toml"), toml.decode("utf-8"))
        p = doc["portrait"]
        name = p["processed"]
    except (UnicodeError, KeyError, TypeError, badge_form.FormError):
        raise PackageError("Package requires a valid saved form with a managed processed portrait") from None
    if (not isinstance(name, str) or not PORTRAIT_NAME.fullmatch(name)
            or not (name.startswith("sample-" + target) or name.startswith("portrait-" + target))):
        raise PackageError("Portrait must be a managed asset matching the target")
    for forbidden in ("photo", "settings"):
        if p.get(forbidden):
            raise PackageError("Unmanaged portrait inputs are not permitted")
    qr = doc.get("qr", {})
    if isinstance(qr, dict) and qr.get("vcard_file"):
        raise PackageError("External vCard files are not permitted")
    return name


def make_package(form: bytes, portrait: bytes, target: str) -> bytes:
    if target not in TARGETS:
        raise PackageError("Unsupported board target")
    if len(form) > MAX_TOML or len(portrait) > MAX_PORTRAIT:
        raise PackageError("Package input too large")
    name = _portrait_name(form, target)
    if re.search(r"-[0-9a-f]{64}\.png$", name):
        expected = name[-68:-4]
        if digest(portrait) != expected:
            raise PackageError("Managed portrait hash mismatch")
    files = {"badge.toml": form, name: portrait}
    manifest = {"format": SCHEMA, "version": VERSION, "target": target,
                "files": {k: digest(v) for k, v in sorted(files.items())}}
    buf = io.BytesIO()
    with zipfile.ZipFile(buf, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
        archive.writestr("manifest.json", json.dumps(manifest, sort_keys=True, separators=(",", ":")) + "\n")
        for name, data in sorted(files.items()):
            archive.writestr(name, data)
    result = buf.getvalue()
    if len(result) > MAX_PACKAGE:
        raise PackageError("Package exceeds size limit")
    return result


def read_package(raw: bytes) -> tuple[str, dict[str, bytes]]:
    if len(raw) > MAX_PACKAGE:
        raise PackageError("Package exceeds size limit")
    try:
        with zipfile.ZipFile(io.BytesIO(raw)) as archive:
            entries = archive.infolist()
            names = [e.filename for e in entries]
            if len(entries) != 3 or len(set(names)) != 3 or "manifest.json" not in names or "badge.toml" not in names:
                raise PackageError("Unexpected or duplicate archive members")
            for info in entries:
                path = info.filename
                mode = info.external_attr >> 16
                if (path.startswith("/") or "\\" in path or "/" in path or path in (".", "..")
                        or info.flag_bits & 1 or (mode and stat.S_IFMT(mode) not in (0, stat.S_IFREG))
                        or info.file_size > MAX_PORTRAIT or info.compress_size > MAX_PACKAGE):
                    raise PackageError("Unsafe archive member")
            try:
                manifest = json.loads(archive.read("manifest.json", pwd=None))
            except (ValueError, RuntimeError, KeyError):
                raise PackageError("Invalid package manifest") from None
            if not isinstance(manifest, dict) or set(manifest) != {"format", "version", "target", "files"}:
                raise PackageError("Invalid manifest fields")
            if manifest["format"] != SCHEMA or type(manifest["version"]) is not int or manifest["version"] != VERSION:
                raise PackageError("Unsupported package format/version")
            target = manifest["target"]
            if not isinstance(target, str) or target not in TARGETS:
                raise PackageError("Unsupported board target")
            if not isinstance(manifest["files"], dict):
                raise PackageError("Invalid file manifest")
            data = {}
            for info in entries:
                if info.filename == "manifest.json":
                    continue
                with archive.open(info) as stream:
                    chunk = stream.read(MAX_PORTRAIT + 1)
                if len(chunk) > MAX_PORTRAIT:
                    raise PackageError("Oversized decompressed member")
                data[info.filename] = chunk
            if len(data["badge.toml"]) > MAX_TOML:
                raise PackageError("Form too large")
            if set(manifest["files"]) != set(data):
                raise PackageError("Manifest member mismatch")
            if any(not isinstance(manifest["files"][name], str) or
                   not re.fullmatch(r"[0-9a-f]{64}", manifest["files"][name]) or
                   digest(contents) != manifest["files"][name] for name, contents in data.items()):
                raise PackageError("Package checksum mismatch")
            name = _portrait_name(data["badge.toml"], target)
            if set(data) != {"badge.toml", name}:
                raise PackageError("Missing or unexpected portrait")
            if re.search(r"-[0-9a-f]{64}\.png$", name) and digest(data[name]) != name[-68:-4]:
                raise PackageError("Managed portrait hash mismatch")
            return target, data
    except (zipfile.BadZipFile, zipfile.LargeZipFile, EOFError, KeyError) as exc:
        raise PackageError("Invalid package archive") from exc


def build(path: Path, out: Path | None = None) -> int:
    if path.is_symlink() or not path.is_file():
        raise PackageError("Package must be a regular file")
    with path.open("rb") as inp:
        raw = inp.read(MAX_PACKAGE + 1)
    target, data = read_package(raw)
    staging = ROOT / "local" / "badgerpack-staging"
    staging.mkdir(parents=True, exist_ok=True)
    if staging.is_symlink():
        raise PackageError("Unsafe private staging directory")
    with tempfile.TemporaryDirectory(prefix="import-", dir=staging) as temp:
        folder = Path(temp)
        for name, contents in data.items():
            (folder / name).write_bytes(contents)
        form = folder / "badge.toml"
        # Canonical form validator must accept the imported exact inputs before building.
        try:
            badge_form.load_form(form, target=target)
        except badge_form.FormError:
            raise PackageError("Imported profile failed canonical validation") from None
        output = out or (ROOT / "local" / "out" / ("badgerpack-" + digest(raw)[:12]))
        if not output.resolve().is_relative_to((ROOT / "local").resolve()):
            raise PackageError("Build output must remain under ignored local/")
        command = [str(ROOT / "scripts" / "build-badge.sh"), str(form),
                   "--target", target, "--out", str(output)]
        return subprocess.call(command, cwd=ROOT)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("package", type=Path)
    parser.add_argument("--out", type=Path, help="private output directory under local/")
    args = parser.parse_args(argv)
    try:
        return build(args.package, args.out)
    except (PackageError, OSError) as error:
        print("badgerpack: " + str(error), file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
