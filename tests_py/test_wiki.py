"""Check wiki links, GitHub anchors, commands and navigation before publication."""
from __future__ import annotations

import re
import unittest
from pathlib import Path
from urllib.parse import unquote

ROOT = Path(__file__).resolve().parents[1]
WIKI = ROOT / "wiki"
LINK = re.compile(r"!?\[[^\]\n]+\]\(([^)\n]+)\)")
REFERENCE = re.compile(r"^\s*\[[^\]]+\]:\s*(\S+)", re.MULTILINE)
HTML_HREF = re.compile(r"""href=["']([^"']+)["']""")
FENCE = re.compile(r"^[ \t]*\x60{3}[^\n]*\n(.*?)^[ \t]*\x60{3}[ \t]*$", re.MULTILINE | re.DOTALL)
SCRIPT = re.compile(r"(?<![\w/])((?:scripts|tools)/[\w./-]+)")
CLI = re.compile(r"(?:^|\s)(?:python3\s+)?tools/badgerctl\.py\b")


def heading_anchors(text: str) -> set[str]:
    """Match GitHub-style heading slugs, including duplicate-heading suffixes."""
    seen: dict[str, int] = {}
    anchors: set[str] = set()
    for line in text.splitlines():
        match = re.match(r"^#{1,6}\s+(.+?)\s*#*\s*$", line)
        if not match:
            continue
        title = match.group(1)
        title = re.sub(r"\[([^\]]+)\]\([^)]+\)", r"\1", title)
        title = re.sub(r"<[^>]+>", "", title).replace(chr(96), "")
        slug = re.sub(r"[^\w -]", "", title.lower()).replace(" ", "-")
        index = seen.get(slug, 0)
        seen[slug] = index + 1
        anchors.add(f"{slug}-{index}" if index else slug)
    return anchors


def validate_page(page: Path, content: str) -> None:
    fences = re.findall(r"^[ \t]*\x60{3}", content, re.MULTILINE)
    if len(fences) % 2:
        raise AssertionError(f"{page.name}: unclosed Markdown fence")
    code_blocks = list(FENCE.finditer(content))
    prose = FENCE.sub("", content)
    for href in (LINK.findall(prose) + REFERENCE.findall(prose) +
                 HTML_HREF.findall(prose)):
        href = unquote(href.strip().strip("<>").split(' "', 1)[0])
        if not href or href.startswith(("http://", "https://", "mailto:", "tel:")):
            continue
        path_part, sep, fragment = href.partition("#")
        target = (page.parent / path_part).resolve() if path_part else page.resolve()
        if not target.is_relative_to(ROOT):
            raise AssertionError(f"{page.name}: link escapes repository: {href}")
        if not target.exists():
            raise AssertionError(f"{page.name}: missing link target: {href}")
        if sep and fragment and target.suffix.lower() == ".md":
            target_prose = FENCE.sub("", target.read_text(encoding="utf-8"))
            if fragment not in heading_anchors(target_prose):
                raise AssertionError(f"{page.name}: missing anchor: {href}")
    for block in code_blocks:
        for line in block.group(1).splitlines():
            for match in SCRIPT.finditer(line):
                candidate = match.group(1).rstrip(".,:;)]}")
                if not (ROOT / candidate).is_file():
                    raise AssertionError(f"{page.name}: missing command path: {candidate}")
            cli = CLI.search(line)
            if cli and "--port" in line:
                option = line.find("--port", cli.end())
                command = re.search(r"\b(cmd|push|backup|restore|shell)\b", line[cli.end():])
                if command and option > cli.end() + command.start():
                    raise AssertionError(f"{page.name}: --port must precede subcommand")


class WikiTests(unittest.TestCase):
    def test_all_relative_wiki_links_anchors_and_commands_resolve(self):
        pages = sorted(WIKI.glob("*.md"))
        self.assertGreaterEqual(len(pages), 16, "Wiki navigation or content missing")
        for page in pages:
            with self.subTest(page=page.name):
                validate_page(page, page.read_text(encoding="utf-8"))

    def test_validation_catches_broken_anchor_and_command_mutations(self):
        page = WIKI / "USB-Command-Line.md"
        source = page.read_text(encoding="utf-8")
        self.assertIn("../docs/USB_CLI.md#commands", source)
        with self.assertRaisesRegex(AssertionError, "missing anchor"):
            validate_page(page, source.replace("../docs/USB_CLI.md#commands",
                                               "../docs/USB_CLI.md#no-such-anchor", 1))
        self.assertIn("tools/badgerctl.py cmd", source)
        with self.assertRaisesRegex(AssertionError, "missing command path"):
            validate_page(page, source.replace("tools/badgerctl.py cmd",
                                               "tools/badgerctrl.py cmd", 1))
        example = 'tools/badgerctl.py --port /dev/ttyACM0 cmd "version"'
        self.assertIn(example, source)
        with self.assertRaisesRegex(AssertionError, "--port must precede"):
            validate_page(page, source.replace(example,
                 'tools/badgerctl.py cmd "version" --port /dev/ttyACM0', 1))

    def test_usb_identifiers_and_port_example(self):
        usb = (WIKI / "USB-Command-Line.md").read_text(encoding="utf-8")
        self.assertIn("2E8A:000A", usb)
        self.assertIn("2E8A:0009", usb)
        self.assertIn('tools/badgerctl.py --port /dev/ttyACM0 cmd "version"', usb)

    def test_navigation_and_readme_point_to_the_wiki(self):
        home = (WIKI / "Home.md").read_text(encoding="utf-8")
        sidebar = (WIKI / "_Sidebar.md").read_text(encoding="utf-8")
        readme = (ROOT / "README.md").read_text(encoding="utf-8")
        self.assertIn("[versioned, task-oriented wiki](wiki/Home.md)", readme)
        for page in sorted(WIKI.glob("*.md")):
            if page.name in {"README.md", "_Sidebar.md"}:
                continue
            with self.subTest(page=page.name):
                self.assertIn(f"({page.name})", sidebar)
        self.assertIn("Testing-and-Validation.md", home)


if __name__ == "__main__":
    unittest.main()
