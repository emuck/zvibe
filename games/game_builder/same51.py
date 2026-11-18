# Copyright (c) 2025 Martin R. Raumann
# SPDX-License-Identifier: BSD-3-Clause
"""
SAM E51 game builder: the one generator for the multi-game menu firmware.

The firmware (target/same51/src/zvibe_same51.c built with MULTI_GAME=1) plays version 3 and
version 4 stories alike, so one list of games feeds one menu. This module writes, into target/same51/src:

- game_registry.c   the story arrays and the table the menu reads (games[], num_games)
- game_registry.mk  what the Makefile needs to size the firmware (STORY_BUFFER, STORY_COUNT)

A file is rewritten only when its content changed, so an unchanged selection does not recompile.

The games come from games/registry.json (the entries that are not excluded and whose story file is
present, packed into the target's flash budget), or from an explicit list given to build_games.py
(--story [Name=]file.z3|.z4).
"""

import hashlib
import os
import re
from pathlib import Path
from typing import Dict, List, Optional

from . import common

MAX_GAMES = 9                # the menu takes one digit
CORE_RESERVE = 1024          # the core needs this much beyond a story's dynamic memory (Z_LEGACY_STACK_RESERVE_BYTES)
SMARTEEPROM = 16384          # the save store (target/same51/include/save_store.h) divides this ...
SEE_PAGE = 512
MIN_AREA = 1024              # ... into one area per game and one more, the write-ahead scratch, in whole pages
VERSIONS = (3, 4)
STORY_LIMIT = {3: 128 * 1024, 4: 256 * 1024}        # the core's Z3_STORY_LIMIT and Z4_STORY_LIMIT (include/zvibe_config.h)
NAME_MAX = 40

_KNOWN_NAMES = {"amfv": "A Mind Forever Voyaging", "nordandbert": "Nord and Bert",
                "trinity": "Trinity", "bureaucracy": "Bureaucracy"}


def default_name(path: Path) -> str:
    """A menu name from a story file name: 'bureaucracy-r116-s870602.z4' gives 'Bureaucracy'."""
    word = path.stem.split('-')[0]
    return _KNOWN_NAMES.get(word.lower(), word.capitalize())


def games_from_files(specs: List[str]) -> List[Dict]:
    """Game entries for '[Menu name=]file' arguments."""
    games = []
    for spec in specs:
        if '=' in spec and not os.path.exists(spec):
            name, path = spec.split('=', 1)
        else:
            name, path = None, spec
        p = Path(path).expanduser()
        if not p.exists():
            raise FileNotFoundError(f"Story file not found: {p}")
        games.append({'id': common.sanitize_identifier(p.name), 'name': name or default_name(p),
                      'path': str(p), 'size': p.stat().st_size})
    return games


def _word(data: bytes, at: int) -> int:
    return (data[at] << 8) | data[at + 1]


def check_story(game: Dict, data: bytes) -> Dict:
    """
    The story must be one the firmware's loader will accept: a version 3 or 4 story file within the core's size limit,
    whose header addresses (start, dictionary, object table, globals, abbreviations) and static memory base lie inside
    the file. These are the checks of z_load_story_data (src/core/zvibe_core.c); the loader makes them again at run time.
    """
    path = common.story_path(game)
    info = common.get_z3_info(data)
    size, version = len(data), info['version']

    def bad(why):
        return ValueError(f"{path}: {why}")

    if version not in VERSIONS:
        raise bad(f"a version {version} story; the firmware plays versions 3 and 4")
    if size > STORY_LIMIT[version]:
        raise bad(f"{size} bytes: over the {STORY_LIMIT[version]} bytes the core takes for a version {version} story")
    start, dictionary, objects, globals_ = _word(data, 6), _word(data, 8), _word(data, 10), _word(data, 12)
    static_base, abbreviations = _word(data, 14), _word(data, 24)
    # version 4 stories start at a routine's address, version 3 at the first instruction: both lie inside the file
    for what, value in (("initial pc", start), ("dictionary", dictionary), ("object table", objects), ("abbreviations", abbreviations)):
        if value >= size:
            raise bad(f"the {what} address {value:#x} is outside the file")
    if globals_ + 480 > size:
        raise bad(f"the global variables ({globals_:#x} + 480 bytes) run past the end of the file")
    if static_base < 64 or static_base >= size:
        raise bad(f"static memory base {static_base:#x} is not inside the file")
    if min(dictionary, objects, globals_) < 64:
        raise bad("a table starts inside the header")
    info['dynamic'] = static_base
    return info


def story_buffer(infos: List[Dict]) -> int:
    """RAM for the largest story's dynamic memory plus the core's reserve, in whole 256 bytes."""
    need = max(i['dynamic'] for i in infos) + CORE_RESERVE
    return (need + 255) // 256 * 256


def save_area_size(count: int) -> int:
    """Bytes of SmartEEPROM per game: count areas and the scratch, equal, in whole pages."""
    return (SMARTEEPROM // (count + 1)) // SEE_PAGE * SEE_PAGE


def _write_if_changed(path: Path, text: str) -> bool:
    """Write text to path unless it is already there; through a temporary file, so a failure leaves the old file whole."""
    if path.exists() and path.read_text() == text:
        return False
    tmp = path.with_name(path.name + ".tmp")
    tmp.write_text(text)
    os.replace(tmp, path)
    return True


def check_name(name: str) -> str:
    """A menu name: printable ASCII, at most NAME_MAX characters (it is typed on a terminal and written into C)."""
    if not name or len(name) > NAME_MAX or not re.fullmatch(r"[\x20-\x7e]+", name):
        raise ValueError(f"menu name {name!r}: use 1 to {NAME_MAX} printable ASCII characters")
    return name


def c_string(s: str) -> str:
    """A C string literal for a checked name: backslash, quote and '?' (so no trigraph can form) are escaped."""
    return '"' + "".join("\\" + c if c in '\\"?' else c for c in s) + '"'


def generation_id(body: str, buffer_size: int) -> str:
    """Names one generation of the two generated files: the C file's content and the buffer size derived from it."""
    return hashlib.sha256(f"{buffer_size}\n{body}".encode()).hexdigest()[:12]


def render_registry(games: List[Dict], datas: List[bytes], infos: List[Dict]) -> str:
    """The C file; the first line is its generation id (the Makefile checks it against game_registry.mk)."""
    out = ["#include <stdint.h>\n#include <stddef.h>\n#include \"game_registry.h\"\n\n"]
    for n, (d, i) in enumerate(zip(datas, infos)):
        out.append(f"/* game {n + 1}: version {i['version']}, release {i['release']}, {len(d)} bytes */\n")
        out.append(f"static const uint8_t story_{n}[] = {{\n")
        for k in range(0, len(d), 16):
            out.append("    " + ", ".join(f"0x{b:02x}" for b in d[k:k + 16]) + ",\n")
        out.append("};\n\n")
    out.append("const game_entry_t games[] = {\n")
    for n, (g, d) in enumerate(zip(games, datas)):
        out.append(f"    {{ {c_string(g['name'])}, story_{n}, {len(d)} }},\n")
    out.append("};\n")
    out.append(f"const int num_games = {len(games)};\n")
    out.append(f"const uint32_t save_area_size = {save_area_size(len(games))};\n")
    body = "".join(out)
    gid = generation_id(body, story_buffer(infos))
    return f"/* Generated by games/game_builder/same51.py. generation {gid} */\n" + body


def render_makefile_fragment(c_text: str, infos: List[Dict]) -> str:
    gid = re.match(r"/\* .*generation ([0-9a-f]+) \*/", c_text).group(1)
    return ("# Generated by games/game_builder/same51.py\n"
            f"GEN_ID := {gid}\n"
            f"STORY_BUFFER := {story_buffer(infos)}\n"
            f"STORY_COUNT := {len(infos)}\n")


def build_same51(registry: Dict, output_dir: Path, games: Optional[List[Dict]] = None,
                 only: Optional[List[str]] = None):
    """
    Generate game_registry.c and game_registry.mk in output_dir.

    games: an explicit list (from games_from_files); otherwise the registry's selection, packed into the
           target's flash budget. only: use exactly these registry ids, in this order (even ones the registry
           excludes by default).
    """
    if games is None and only:
        # An explicit choice by id, in that order: it may name games the registry excludes by default
        by_id = {g['id']: g for g in common.get_games(registry)}
        wanted = [x.strip() for x in only]
        unknown = [x for x in wanted if x not in by_id]
        absent = [x for x in wanted if x in by_id and not common.story_exists(by_id[x])]
        if unknown or absent:
            raise ValueError("; ".join(filter(None, [
                f"not in the registry: {', '.join(unknown)}" if unknown else "",
                f"story file missing: {', '.join(absent)}" if absent else ""])))
        games = [by_id[x] for x in wanted]
        budget = registry.get('targets', {}).get('same51', {}).get('flash_available')
        total = sum(common.story_size(g) for g in games)
        if budget and total > budget:
            raise ValueError(f"the games total {total} bytes, over the SAM E51 budget of {budget}")
    elif games is None:
        games = common.get_downloaded_games('same51', registry)
    if not games:
        raise ValueError("no games to build")
    if len(games) > MAX_GAMES:
        raise ValueError(f"{len(games)} games: the menu takes at most {MAX_GAMES}")

    for g in games:
        check_name(g['name'])
    datas, infos = [], []
    for g in games:
        d = common.z3_to_bytes(common.story_path(g))
        infos.append(check_story(g, d))
        datas.append(d)

    print(f"Building {len(games)} games for SAM E51:\n")
    for g, d, i in zip(games, datas, infos):
        print(f"  {i['version']}  {len(d):8d}  {g['name']}")
    total = sum(len(d) for d in datas)
    print(f"\n  {total} bytes of stories, story buffer {story_buffer(infos)}, "
          f"save area {save_area_size(len(games))} bytes each (largest save {save_area_size(len(games)) - 28})")
    if save_area_size(len(games)) < MIN_AREA:
        raise ValueError(f"{len(games)} games leave save areas of {save_area_size(len(games))} bytes")

    c_text = render_registry(games, datas, infos)
    changed = _write_if_changed(output_dir / "game_registry.c", c_text)
    changed |= _write_if_changed(output_dir / "game_registry.mk", render_makefile_fragment(c_text, infos))
    print(f"\n[ok] game_registry.c, game_registry.mk in {output_dir}" + ("" if changed else " (unchanged)"))
    return games
