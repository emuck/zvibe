# Copyright (c) 2025 Martin R. Raumann
# SPDX-License-Identifier: BSD-3-Clause
"""
Common utilities for game building across all targets
"""

import json
from pathlib import Path
from typing import Dict, List, Optional

# Paths relative to this file (game_builder/ lives inside games/)
GAMES_DIR = Path(__file__).parent.parent
PROJECT_ROOT = GAMES_DIR.parent
CATALOG_DIR = GAMES_DIR / "catalog"
REGISTRY_FILE = GAMES_DIR / "registry.json"


def load_registry(registry_path: Optional[Path] = None) -> Dict:
    """
    Load game registry from JSON file

    Args:
        registry_path: Optional path to registry file (defaults to games/registry.json)

    Returns:
        Dictionary containing registry data
    """
    if registry_path is None:
        registry_path = REGISTRY_FILE

    if not registry_path.exists():
        raise FileNotFoundError(f"Registry not found: {registry_path}")

    with open(registry_path, 'r') as f:
        return json.load(f)


def get_games(registry: Optional[Dict] = None) -> List[Dict]:
    """
    Get list of all games in registry order.

    All games are universal .z3 files and work on all targets.
    """
    if registry is None:
        registry = load_registry()
    return list(registry['games'])


def get_default_game(registry: Optional[Dict] = None) -> Optional[Dict]:
    """
    Get the default game (for single-game builds).

    Returns game with 'default' field set, or first game if none marked.
    """
    if registry is None:
        registry = load_registry()

    for game in registry['games']:
        if game.get('default', False):
            return game

    games = registry['games']
    return games[0] if games else None


def get_downloaded_games(target: str, registry: Dict) -> List[Dict]:
    """
    Get downloaded games that fit in the target's flash budget.

    Filters to games whose .z3 files exist in catalog/, then packs
    in registry order until flash_available is reached.
    """
    all_games = get_games(registry)
    available = [g for g in all_games if (CATALOG_DIR / g['filename']).exists()]
    available = [g for g in available if not g.get('exclude', False)]

    if not available:
        raise ValueError("No game files found. Run get_games.py to download games.")

    flash_available = registry.get('targets', {}).get(target, {}).get('flash_available')
    if not flash_available:
        return available

    fitted = []
    total = 0
    for game in available:
        if total + game['size'] <= flash_available:
            fitted.append(game)
            total += game['size']
        else:
            print(f"[skip] {game['name']} ({game['size'] // 1024}KB) — exceeds flash limit")

    if not fitted:
        raise ValueError(f"No games fit within {target} flash budget ({flash_available // 1024}KB)")

    print(f"Flash usage: {total // 1024}KB / {flash_available // 1024}KB")
    return fitted


def compute_flash_budgets(registry: Dict) -> Dict:
    """
    For each target, compute total size of selected (non-excluded, downloaded) games.

    Returns {target: {'used': bytes, 'available': bytes_or_None}}
    """
    selected = [g for g in registry['games']
                if (CATALOG_DIR / g['filename']).exists()
                and not g.get('exclude', False)]
    used = sum(g['size'] for g in selected)

    budgets = {}
    for target, info in registry.get('targets', {}).items():
        budgets[target] = {
            'used': used,
            'available': info.get('flash_available'),
        }
    return budgets


def z3_to_bytes(z3_path: Path) -> bytes:
    """
    Read Z3 file as raw bytes

    Args:
        z3_path: Path to Z3 story file

    Returns:
        Bytes content of the file
    """
    if not z3_path.exists():
        raise FileNotFoundError(f"Game file not found: {z3_path}")

    with open(z3_path, 'rb') as f:
        return f.read()


def get_z3_info(data: bytes) -> Dict:
    """
    Extract Z-machine header information

    Args:
        data: Z3 file data as bytes

    Returns:
        Dictionary with version, release, serial, size
    """
    if len(data) < 64:
        raise ValueError("Invalid Z3 file: too short")

    version = data[0]
    release = (data[2] << 8) | data[3]

    # Serial number is at offset 18-23 (6 bytes)
    try:
        serial = data[18:24].decode('ascii', errors='ignore')
    except:
        serial = "unknown"

    return {
        'version': version,
        'release': release,
        'serial': serial,
        'size': len(data)
    }


def sanitize_identifier(name: str) -> str:
    """
    Convert a string to a valid C identifier

    Args:
        name: Input string (filename, game name, etc.)

    Returns:
        Valid C identifier (lowercase, underscores)
    """
    # Remove extension if present
    if '.' in name:
        name = name.rsplit('.', 1)[0]

    # Convert to lowercase
    name = name.lower()

    # Replace problematic characters with underscores
    name = name.replace('-', '_').replace(' ', '_').replace('.', '_')

    # Remove any non-alphanumeric characters except underscores
    name = ''.join(c for c in name if c.isalnum() or c == '_')

    # Ensure it starts with a letter or underscore
    if name and not (name[0].isalpha() or name[0] == '_'):
        name = 'game_' + name

    # Ensure it's not empty
    if not name:
        name = 'game'

    return name


def format_bytes_per_line(data: bytes, bytes_per_line: int = 12, indent: str = "    ") -> str:
    """
    Format binary data as hex bytes for C array

    Args:
        data: Binary data
        bytes_per_line: Number of bytes per line
        indent: Indentation string

    Returns:
        Formatted string with hex bytes
    """
    lines = []
    for i in range(0, len(data), bytes_per_line):
        chunk = data[i:i+bytes_per_line]
        hex_bytes = ', '.join(f'0x{b:02x}' for b in chunk)
        lines.append(f"{indent}{hex_bytes},")

    return '\n'.join(lines)
