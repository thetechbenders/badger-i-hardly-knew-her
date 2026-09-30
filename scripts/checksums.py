#!/usr/bin/env python3
"""Write SHA256SUMS for build artifacts: checksums.py <dir> <file>..."""
import hashlib
import sys
from pathlib import Path

d = Path(sys.argv[1])
lines = []
for name in sys.argv[2:]:
    p = d / name
    if p.exists():
        lines.append(f"{hashlib.sha256(p.read_bytes()).hexdigest()}  {name}")
(d / "SHA256SUMS").write_text("\n".join(lines) + "\n")
print("\n".join(lines))
