"""Private admission compiler. Diagnostics deliberately contain no input values."""
import argparse
from pathlib import Path
import tomllib
import re


def load(path):
    # Reading bytes before TOML parsing avoids universal-newline translation.
    value = tomllib.loads(Path(path).read_bytes().decode("utf-8"))
    if set(value) != {"payload", "event", "holder", "timeout_s"}:
        raise ValueError()
    payload = value["payload"]
    # ASCII vCard, CR/LF allowed and preserved. 520 byte-mode bytes always
    # fit Nayuki version 15-L (including the 4-module quiet zone at 2 px).
    if not isinstance(payload, str) or not 1 <= len(payload.encode("utf-8")) <= 520:
        raise ValueError()
    if any(not (32 <= ord(c) <= 126 or c in "\r\n") for c in payload):
        raise ValueError()
    for name in ("event", "holder"):
        label = value[name]
        # Conservative width bound for sans_11; renderer also checks pixels.
        if not isinstance(label, str) or not 1 <= len(label) <= 32:
            raise ValueError()
        if any(not 32 <= ord(c) <= 126 for c in label):
            raise ValueError()
    font_source = (Path(__file__).resolve().parents[1] / "firmware/generated/fonts.cpp").read_text()
    table = font_source.split("const Glyph sans_11_glyphs[] = {", 1)[1].split("};", 1)[0]
    advances = {}
    for row in re.findall(r"\{([^}]+)\}", table):
        fields = [int(x.strip(), 0) for x in row.split(",")]
        advances[fields[0]] = fields[5]
    if any(sum(advances[ord(c)] for c in value[k]) > 82 for k in ("event", "holder")):
        raise ValueError()
    if type(value["timeout_s"]) is not int or not 1 <= value["timeout_s"] <= 15:
        raise ValueError()
    return value


def generate(value):
    def literal(text):
        # Fixed-width octal escapes preserve every decoded byte, incl. CRLF.
        return '"' + ''.join("\\%03o" % b for b in text.encode("ascii")) + '"'
    return ('#include "admission.hpp"\nnamespace badge {\n'
            'static const Admission admission = {' +
            ','.join(literal(value[k]) for k in ("payload", "event", "holder")) +
            ',' + str(value["timeout_s"]) + '};\n'
            'const Admission *const kPrivateAdmission = &admission;\n}\n')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input")
    parser.add_argument("--out", required=True)
    args = parser.parse_args()
    try:
        source = generate(load(args.input))
        Path(args.out).parent.mkdir(parents=True, exist_ok=True)
        Path(args.out).write_text(source, encoding="ascii")
    except Exception:
        parser.exit(1, "Private admission input invalid or unavailable; no values logged.\n")


if __name__ == "__main__":
    main()
