"""Check the in-repo wiki before publishing it from a stacked documentation PR."""
from __future__ import annotations

import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
WIKI = ROOT / "wiki"
LINK = re.compile(r"!?\[[^\]]+\]\(([^)]+)\)")


class WikiTests(unittest.TestCase):
    def test_all_relative_wiki_links_resolve_inside_the_repository(self):
        pages = sorted(WIKI.glob("*.md"))
        self.assertGreaterEqual(len(pages), 16, "Wiki navigation or content missing")
        for page in pages:
            content = page.read_text(encoding="utf-8")
            with self.subTest(page=page.name, kind="code fences"):
                self.assertEqual(len(re.findall(r"^\s*\x60\x60\x60", content, re.MULTILINE)) % 2, 0)
            for match in LINK.finditer(content):
                href = match.group(1).split("#", 1)[0].strip()
                if not href or href.startswith(("http://", "https://", "mailto:", "tel:")):
                    continue
                relative = href.split(" ", 1)[0]
                resolved = (page.parent / relative).resolve()
                with self.subTest(page=page.name, link=relative):
                    self.assertTrue(resolved.is_relative_to(ROOT), "Link escapes repository")
                    self.assertTrue(resolved.exists(), "Missing wiki/source link")

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
