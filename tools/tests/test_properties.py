import tempfile
import unittest
from pathlib import Path

import fakes  # noqa: F401  (puts tools/ on sys.path)
from mchost import properties


class PropertiesTest(unittest.TestCase):
    def test_parse_vanilla_lines(self):
        text = ("#Minecraft server properties\n#Sat Oct 24 02:35:19 BST 2026\n"
                "level-type=minecraft\\:normal\nmotd=A Minecraft Server\ngenerator-settings={}\n"
                "level-seed=\n  spaced : value\n! bang comment\nunicode=caf\\u00e9\n")
        p = properties.parse(text)
        self.assertEqual(p["level-type"], "minecraft:normal")
        self.assertEqual(p["motd"], "A Minecraft Server")
        self.assertEqual(p["generator-settings"], "{}")
        self.assertEqual(p["level-seed"], "")
        self.assertEqual(p["spaced"], "value")
        self.assertEqual(p["unicode"], "café")
        self.assertNotIn("! bang comment", p)

    def test_escape_round_trip(self):
        for value in ["minecraft:normal", "a=b", " leading space", "tab\there", "back\\slash", "naïve #1!", ""]:
            line = f"k={properties.escape(value)}"
            self.assertEqual(properties.parse(line)["k"], value, line)

    def test_update_keeps_comments_order_and_unknown_keys(self):
        with tempfile.TemporaryDirectory() as d:
            path = Path(d) / "server.properties"
            path.write_text("#header\nb=1\na=2\n#mid\nc=3\n", encoding="utf-8")
            properties.update(path, {"a": "20", "d": "minecraft:flat"})
            lines = path.read_text(encoding="utf-8").splitlines()
            self.assertEqual(lines, ["#header", "b=1", "a=20", "#mid", "c=3", "d=minecraft\\:flat"])
            self.assertEqual(properties.read(path)["d"], "minecraft:flat")

    def test_update_creates_file(self):
        with tempfile.TemporaryDirectory() as d:
            path = Path(d) / "sub" / "server.properties"
            properties.update(path, {"server-port": "25566"})
            self.assertEqual(properties.read(path), {"server-port": "25566"})

    def test_read_missing_is_empty(self):
        self.assertEqual(properties.read(Path(tempfile.gettempdir()) / "no-such-file.properties"), {})


if __name__ == "__main__":
    unittest.main()
