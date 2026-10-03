"""Current documentation links and retired-route boundaries, no external I/O."""
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[2]


class CurrentDocumentationTests(unittest.TestCase):
    def test_local_markdown_links_resolve(self):
        files = list((ROOT / "docs").rglob("*.md"))
        files += [ROOT / "README.md", ROOT / "HANDOFF.md", ROOT / "pc-agent/README.md",
                  ROOT / "firmware/luna-panel/main/assets/README.md"]
        for file in files:
            for target in re.findall(r'\]\(([^)]+)\)', file.read_text(encoding="utf-8")):
                if "://" in target or target.startswith("#"):
                    continue
                target = target.split("#", 1)[0]
                with self.subTest(document=str(file.relative_to(ROOT)), target=target):
                    self.assertTrue((file.parent / target).resolve().exists())

    def test_retired_routes_have_no_active_docs_or_entrypoints(self):
        development = ROOT / "docs/development"
        for pattern in ("p0-*.md", "p1-*.md", "p2-*.md", "r[1-5]-*.md"):
            self.assertEqual(list(development.glob(pattern)), [])
        self.assertFalse((ROOT / "docs/ideas/luna-usb-v1-archive.md").exists())
        for name in ("luna_agent.py", "luna_usb_transport.py", "weather_adapter.py", "start-agent.ps1"):
            self.assertFalse((ROOT / "pc-agent" / name).exists())


if __name__ == "__main__":
    unittest.main()
