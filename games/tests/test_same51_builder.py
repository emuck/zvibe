# Copyright (c) 2025 Martin R. Raumann
# SPDX-License-Identifier: BSD-3-Clause
"""Tests of the SAM E51 menu generator (games/game_builder/same51.py, games/build_games.py same51)."""
import os
import re
import struct
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

GAMES = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(GAMES))
from game_builder import common, same51          # noqa: E402


def story(version=3, size=4096, dyn=1024, release=1, **over):
    """A story file whose header passes the loader's checks (the rest is zeros)."""
    d = bytearray(size)
    d[0] = version
    struct.pack_into(">H", d, 2, release)
    for at, val in ((6, 0x0400), (8, 0x0500), (10, 0x0600), (12, 0x0700), (14, dyn), (24, 0x0300)):
        struct.pack_into(">H", d, at, val)
    d[0x12:0x18] = b"260101"
    for at, val in over.items():
        struct.pack_into(">H", d, int(at[1:]), val)         # keys like w6, w14: a word at that offset
    return bytes(d)


class Env(unittest.TestCase):
    def setUp(self):
        self.tmp = Path(tempfile.mkdtemp())
        self.out = self.tmp / "out"
        self.out.mkdir()

    def file(self, name, data):
        p = self.tmp / name
        p.write_bytes(data)
        return str(p)

    def build(self, *specs):
        return same51.build_same51({}, self.out, same51.games_from_files(list(specs)))


class Validation(Env):
    def test_accepts_versions_3_and_4(self):
        self.build(self.file("a.z3", story(3)), self.file("b.z4", story(4)))
        c = (self.out / "game_registry.c").read_text()
        self.assertIn("const int num_games = 2;", c)

    def test_rejects_other_versions(self):
        for v in (1, 2, 5, 6, 8):
            with self.assertRaises(ValueError):
                self.build(self.file("x.z", story(v)))

    def test_rejects_short_and_truncated_files(self):
        with self.assertRaises(ValueError):
            self.build(self.file("x.z3", b"\x03" * 20))
        with self.assertRaises(ValueError):                                  # header addresses past the end
            self.build(self.file("x.z3", story(3, size=1024, dyn=512)[:700]))

    def test_rejects_each_bad_header_address(self):
        for field in ("w6", "w8", "w10", "w24"):                             # pc, dictionary, object table, abbreviations
            with self.assertRaises(ValueError, msg=field):
                self.build(self.file(f"{field}.z3", story(3, **{field: 0xF000})))
        with self.assertRaises(ValueError):                                  # globals' 480 bytes run past the end
            self.build(self.file("g.z3", story(3, size=4096, w12=4000)))
        with self.assertRaises(ValueError):                                  # static base inside the header, or beyond the file
            self.build(self.file("s1.z3", story(3, dyn=32)))
        with self.assertRaises(ValueError):
            self.build(self.file("s2.z3", story(3, size=4096, dyn=5000)))

    def test_size_limits(self):
        with self.assertRaises(ValueError):
            self.build(self.file("big.z3", story(3, size=128 * 1024 + 2)))
        self.build(self.file("ok.z4", story(4, size=200 * 1024)))             # a version 4 story may be larger

    def test_game_count_limits(self):
        with self.assertRaises(ValueError):
            same51.build_same51({}, self.out, [])
        files = [self.file(f"g{i}.z3", story(3)) for i in range(10)]
        with self.assertRaises(ValueError):
            self.build(*files)
        self.build(*files[:9])


class Names(Env):
    def test_menu_names_are_escaped_and_checked(self):
        f = self.file("a.z3", story(3))
        self.build(f'Say "hi" \\ what?=' + f)
        c = (self.out / "game_registry.c").read_text()
        self.assertIn(r'"Say \"hi\" \\ what\?"', c)
        for bad in ("a\nb", "tab\there", "café", "x" * 41):
            with self.assertRaises(ValueError, msg=repr(bad)):
                self.build(f"{bad}={f}")

    def test_names_stay_out_of_comments(self):
        self.build(f"Evil */ name={self.file('a.z3', story(3))}")
        c = (self.out / "game_registry.c").read_text()
        self.assertNotIn("Evil", c.split("const game_entry_t")[0])


class Output(Env):
    def test_unchanged_selection_leaves_the_files_alone(self):
        f = self.file("a.z3", story(3))
        self.build(f)
        before = {n: (self.out / n).stat().st_mtime_ns for n in ("game_registry.c", "game_registry.mk")}
        self.build(f)
        after = {n: (self.out / n).stat().st_mtime_ns for n in before}
        self.assertEqual(before, after)

    def test_the_two_files_share_a_generation_id(self):
        self.build(self.file("a.z3", story(3)), self.file("b.z4", story(4, dyn=2000)))
        c = (self.out / "game_registry.c").read_text().splitlines()[0]
        mk = (self.out / "game_registry.mk").read_text()
        gid = re.search(r"generation ([0-9a-f]+) \*/", c).group(1)
        self.assertIn(f"GEN_ID := {gid}", mk)
        self.assertIn("STORY_BUFFER := 3072", mk)                             # 2000 + the core's 1024, in whole 256 bytes

    def test_another_selection_gets_another_id(self):
        self.build(self.file("a.z3", story(3)))
        one = (self.out / "game_registry.mk").read_text()
        self.build(self.file("a.z3", story(3)), self.file("b.z3", story(3, release=2)))
        self.assertNotEqual(one, (self.out / "game_registry.mk").read_text())

    def test_save_areas(self):
        self.assertEqual(same51.save_area_size(1), 8192)
        self.assertEqual(same51.save_area_size(2), 5120)
        self.assertEqual(same51.save_area_size(5), 2560)
        self.assertEqual(same51.save_area_size(9), 1536)
        for n in range(1, 10):
            a = same51.save_area_size(n)
            self.assertEqual(a % 512, 0)
            self.assertLessEqual(a * (n + 1), 16384)

    def test_story_buffer(self):
        infos = [{"dynamic": 14852}, {"dynamic": 37726}]
        self.assertEqual(same51.story_buffer(infos), 38912)


class CommandLine(Env):
    def run_cli(self, *args):
        return subprocess.run([sys.executable, str(GAMES / "build_games.py"), "same51", "--out", str(self.out), *args],
                              capture_output=True, text=True)

    def test_story_and_only_are_refused_together(self):
        r = self.run_cli("--story", self.file("a.z3", story(3)), "--only", "zork1")
        self.assertNotEqual(r.returncode, 0)
        self.assertIn("cannot be combined", r.stdout + r.stderr)

    def test_missing_file(self):
        self.assertNotEqual(self.run_cli("--story", str(self.tmp / "nope.z3")).returncode, 0)

    def test_files_work(self):
        r = self.run_cli("--story", "Mine=" + self.file("a.z4", story(4)))
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        self.assertTrue((self.out / "game_registry.c").exists())


if __name__ == "__main__":
    unittest.main()
