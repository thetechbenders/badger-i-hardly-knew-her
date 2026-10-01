#!/usr/bin/env python3
"""Package firmware build artifacts as a ZIP with a matching manifest.

  scripts/package_firmware.py build/fw-private out.zip [--name DIR] [--only FILE ...]
  scripts/package_firmware.py --verify out.zip

The build directory's SHA256SUMS is checked first: every file it lists must
exist and match. The package then contains the chosen artifacts (default:
everything SHA256SUMS lists) under DIR/, plus DIR/SHA256SUMS written for
exactly those files, so the manifest never names a file the ZIP lacks and
every packaged artifact is covered. The ZIP is re-read and verified after
writing. Entries are sorted with fixed timestamps and modes, so the same
inputs give a byte-identical ZIP.

--verify checks an existing ZIP: one top-level directory, a SHA256SUMS in
it, every listed file present with a matching hash, and no unlisted file.
"""
from __future__ import annotations

import argparse
import hashlib
import io
import sys
import zipfile
from pathlib import Path

MANIFEST = "SHA256SUMS"
_EPOCH = (1980, 1, 1, 0, 0, 0)


class PackageError(Exception):
    pass


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def parse_manifest(text: str, where: str) -> dict[str, str]:
    out: dict[str, str] = {}
    for n, line in enumerate(text.splitlines(), 1):
        if not line.strip():
            continue
        parts = line.split(None, 1)
        if len(parts) != 2 or len(parts[0]) != 64:
            raise PackageError(f"{where}:{n}: malformed line {line!r}")
        name = parts[1].lstrip("*").strip()
        if "/" in name or name in ("", ".", "..") or name == MANIFEST:
            raise PackageError(f"{where}:{n}: unexpected file name {name!r}")
        if name in out:
            raise PackageError(f"{where}:{n}: {name} listed twice")
        out[name] = parts[0].lower()
    if not out:
        raise PackageError(f"{where}: empty manifest")
    return out


def check_build(build: Path) -> dict[str, str]:
    """Every file the build's SHA256SUMS lists exists and matches."""
    mf = build / MANIFEST
    if not mf.is_file():
        raise PackageError(f"{mf}: missing")
    listed = parse_manifest(mf.read_text(), str(mf))
    for name, want in listed.items():
        p = build / name
        if not p.is_file():
            raise PackageError(f"{p}: listed in {MANIFEST} but missing")
        if sha256(p.read_bytes()) != want:
            raise PackageError(f"{p}: checksum does not match {MANIFEST}")
    return listed


def verify_zip(data: bytes) -> dict[str, str]:
    """Returns {artifact: sha256} after checking manifest <-> contents both ways."""
    with zipfile.ZipFile(io.BytesIO(data)) as z:
        names = [i.filename for i in z.infolist() if not i.is_dir()]
        tops = {n.split("/", 1)[0] for n in names}
        if len(tops) != 1 or any(n.count("/") != 1 for n in names):
            raise PackageError(f"expected one flat top-level directory, got {sorted(names)}")
        top = tops.pop()
        if f"{top}/{MANIFEST}" not in names:
            raise PackageError(f"{top}/{MANIFEST} missing")
        listed = parse_manifest(z.read(f"{top}/{MANIFEST}").decode(), f"{top}/{MANIFEST}")
        packaged = {n.split("/", 1)[1] for n in names} - {MANIFEST}
        missing = sorted(set(listed) - packaged)
        unlisted = sorted(packaged - set(listed))
        if missing:
            raise PackageError(f"listed in {MANIFEST} but not packaged: {missing}")
        if unlisted:
            raise PackageError(f"packaged but not in {MANIFEST}: {unlisted}")
        for name, want in listed.items():
            if sha256(z.read(f"{top}/{name}")) != want:
                raise PackageError(f"{top}/{name}: checksum does not match {MANIFEST}")
    return listed


def package(build: Path, out: Path, name: str, only: list[str] | None = None) -> dict[str, str]:
    listed = check_build(build)
    files = sorted(only) if only else sorted(listed)
    for f in files:
        if f not in listed:
            raise PackageError(f"{f}: not an artifact listed in {build / MANIFEST}")
    manifest = "".join(f"{listed[f]}  {f}\n" for f in files).encode()
    buf = io.BytesIO()
    with zipfile.ZipFile(buf, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as z:
        for f, data in [(f, (build / f).read_bytes()) for f in files] + [(MANIFEST, manifest)]:
            info = zipfile.ZipInfo(f"{name}/{f}", _EPOCH)
            info.compress_type = zipfile.ZIP_DEFLATED
            info.external_attr = 0o644 << 16
            info.create_system = 3
            z.writestr(info, data)
    data = buf.getvalue()
    got = verify_zip(data)
    if got != {f: listed[f] for f in files}:
        raise PackageError("packaged manifest differs from the build's checksums")
    out.write_bytes(data)
    return got


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("build", nargs="?", type=Path, help="build directory with SHA256SUMS")
    ap.add_argument("out", nargs="?", type=Path, help="ZIP to write")
    ap.add_argument("--name", help="top-level directory in the ZIP (default: ZIP name without .zip)")
    ap.add_argument("--only", nargs="+", metavar="FILE", help="package only these artifacts")
    ap.add_argument("--verify", type=Path, metavar="ZIP", help="verify an existing package")
    args = ap.parse_args(argv)
    try:
        if args.verify:
            files = verify_zip(args.verify.read_bytes())
            print(f"ok    {args.verify}: {len(files)} artifacts, {MANIFEST} matches contents both ways")
            return 0
        if not args.build or not args.out:
            ap.error("build directory and output ZIP required")
        name = args.name or args.out.name.removesuffix(".zip")
        files = package(args.build, args.out, name, args.only)
    except (PackageError, zipfile.BadZipFile, OSError) as e:
        print(f"FAIL  {e}", file=sys.stderr)
        return 1
    for f, h in files.items():
        print(f"ok    {h}  {f}")
    print(f"ok    {args.out}: {len(files)} artifacts + {MANIFEST}, verified; sha256 {sha256(args.out.read_bytes())}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
