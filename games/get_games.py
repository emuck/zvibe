#!/usr/bin/env python3
# Copyright (c) 2025 Martin R. Raumann
# SPDX-License-Identifier: BSD-3-Clause
"""
ZVibe Game Manager - Download and manage Z-machine games

Two modes:
1. Registry mode: Work with games defined in registry.json
2. Browse mode: Interactive browsing of Infocom catalog

Supports all targets: same51, riscv, console, tests
"""

import argparse
import json
import re
import sys
import os
from pathlib import Path
from typing import Optional

try:
    import requests
except ImportError:
    print("Error: Missing required package: requests")
    print("Please install with: pip install requests")
    sys.exit(1)

# Paths
GAMES_DIR = Path(__file__).parent
CATALOG_DIR = GAMES_DIR / "catalog"
REGISTRY_FILE = GAMES_DIR / "registry.json"

# Infocom catalog
INFOCOM_BASE_URL = "https://eblong.com/infocom/"
INFOCOM_CATALOG_URL = INFOCOM_BASE_URL + "catalog.json"
SIZE_CACHE_FILE = GAMES_DIR / "size_cache.json"


def load_registry() -> dict:
    """Load game registry"""
    if not REGISTRY_FILE.exists():
        print(f"Error: Registry not found: {REGISTRY_FILE}")
        sys.exit(1)

    with open(REGISTRY_FILE, 'r') as f:
        return json.load(f)


def download_game(game: dict, force: bool = False) -> bool:
    """
    Download a single game to catalog

    Args:
        game: Game dictionary from registry
        force: Force re-download even if file exists

    Returns:
        True if download succeeded or file already exists
    """
    output_path = CATALOG_DIR / game['filename']

    # Check if already exists
    if output_path.exists() and not force:
        print(f"[ok] {game['name']:40} [already downloaded]")
        return True

    # Check if URL is provided
    if 'url' not in game:
        print(f"[skip] {game['name']:40} [no URL in registry]")
        return False

    # Download
    print(f"Downloading {game['name']:40} [{game['size'] // 1024}KB]", end='', flush=True)

    try:
        response = requests.get(game['url'], timeout=30)
        response.raise_for_status()

        # Ensure directory exists
        CATALOG_DIR.mkdir(parents=True, exist_ok=True)

        # Write file
        with open(output_path, 'wb') as f:
            f.write(response.content)

        # Verify size
        actual_size = len(response.content)
        expected_size = game.get('size', 0)

        if expected_size > 0 and abs(actual_size - expected_size) > 100:
            print(f" [warn] size mismatch (expected {expected_size}, got {actual_size})")
        else:
            print(" [ok]")

        return True

    except requests.exceptions.RequestException as e:
        print(f" [error] download failed: {e}")
        return False


def cmd_fetch(args):
    """Fetch games from registry (downloads all games with URLs)"""
    registry = load_registry()

    # Ensure catalog directory exists
    CATALOG_DIR.mkdir(parents=True, exist_ok=True)

    # Get all games with URLs
    games_to_fetch = [g for g in registry['games'] if 'url' in g]

    if not games_to_fetch:
        print(f"Error: No games in registry have download URLs")
        print(f"Tip: Use 'browse' to download games interactively")
        return

    print(f"Fetching {len(games_to_fetch)} games...\n")

    # Download games
    success_count = 0
    for game in games_to_fetch:
        if download_game(game, force=args.force):
            success_count += 1

    # Summary
    print(f"\n{'='*60}")
    print(f"Downloaded: {success_count}/{len(games_to_fetch)} games")
    print(f"Location: {CATALOG_DIR}")


def cmd_list(args):
    """List available games"""
    registry = load_registry()

    print(f"ZVibe Game Registry (version {registry.get('version', 'unknown')})\n")
    print("All games are universal .z3 files and work on all targets.\n")

    # Calculate total size
    total_size = sum(g.get('size', 0) for g in registry['games'])

    print(f"{'ID':<20} {'Name':<50} {'Size':>10}")
    print("=" * 85)

    for game in registry['games']:
        # Check if file exists
        game_path = CATALOG_DIR / game['filename']
        exists_marker = "*" if game_path.exists() else " "

        # Default marker
        default_marker = "D" if game.get('default', False) else " "

        # Format size
        size_kb = game.get('size', 0) // 1024
        size_str = f"{size_kb}KB" if size_kb > 0 else "?"

        print(f"{exists_marker}{default_marker} {game['id']:<18} {game['name']:<48} {size_str:>8}")

    print(f"\n{'='*85}")
    print(f"Total games: {len(registry['games'])}")
    print(f"Total size: {total_size // 1024}KB ({total_size} bytes)")

    # Show flash budgets per target
    print(f"\nFlash Budgets:")
    for target_name, target_info in registry.get('targets', {}).items():
        available = target_info.get('flash_available')
        if available:
            print(f"  {target_name:<16} {available // 1024:>6}KB")
        else:
            print(f"  {target_name:<16} unlimited")

    print("\n* = downloaded to catalog")
    print("D = default game for multi-game builds (set 'default: true' in registry)")


def read_z3_metadata(z3_path: Path) -> dict:
    """
    Extract metadata from Z3 file header

    Z-machine header format:
    - Byte 0: Version
    - Bytes 2-3: Release number (big-endian)
    - Bytes 18-23: Serial number (6 ASCII chars, typically YYMMDD)
    """
    try:
        with open(z3_path, 'rb') as f:
            header = f.read(64)

        if len(header) < 24:
            return {}

        version = header[0]
        release = (header[2] << 8) | header[3]
        serial = header[18:24].decode('ascii', errors='ignore')
        size = z3_path.stat().st_size

        return {
            'version': version,
            'release': release,
            'serial': serial,
            'size': size
        }
    except Exception as e:
        print(f"Warning: Could not read {z3_path.name}: {e}")
        return {}


def cmd_scan(args):
    """Scan catalog/ and add unregistered .z3 files to the registry."""
    registry = load_registry()

    if not CATALOG_DIR.exists():
        print(f"Error: Catalog directory not found: {CATALOG_DIR}")
        return

    z3_files = sorted(CATALOG_DIR.glob("*.z3"))
    if not z3_files:
        print(f"No .z3 files found in {CATALOG_DIR}")
        return

    existing_filenames = {g['filename'] for g in registry['games']}
    added_count = 0

    for z3_file in z3_files:
        filename = z3_file.name
        if filename in existing_filenames:
            continue

        metadata = read_z3_metadata(z3_file)
        if not metadata:
            print(f"Warning: Skipping {filename} (could not read metadata)")
            continue

        game_id = filename.replace('.z3', '')
        game_id = game_id.split('-r')[0]
        game_id = game_id.lower().replace(' ', '_')

        existing_ids = {g['id'] for g in registry['games']}
        if game_id in existing_ids:
            game_id = f"{game_id}_{metadata['serial']}"

        display_name = game_id.replace('_', ' ').title()

        registry['games'].append({
            'id': game_id,
            'name': display_name,
            'filename': filename,
            'size': metadata['size'],
            'release': metadata['release'],
            'serial': metadata['serial'],
            'tags': ['local'],
        })
        added_count += 1
        print(f"Added {game_id} ({filename})")

    if added_count == 0:
        print("No new games found.")
        return

    with open(REGISTRY_FILE, 'w') as f:
        json.dump(registry, f, indent=2)
        f.write('\n')

    print(f"\nAdded {added_count} game(s) to registry.json")


def cmd_info(args):
    """Show detailed information about a specific game"""
    registry = load_registry()

    # Find game
    game = next((g for g in registry['games'] if g['id'] == args.game_id), None)
    if not game:
        print(f"Error: Game not found: {args.game_id}")
        print(f"\nAvailable game IDs:")
        for g in registry['games']:
            print(f"  - {g['id']}")
        return

    # Display info
    print(f"{'='*60}")
    print(f"Game: {game['name']}")
    print(f"ID: {game['id']}")
    print(f"{'='*60}\n")

    print(f"Filename: {game['filename']}")
    if 'size' in game:
        print(f"Size: {game['size']} bytes ({game['size'] // 1024}KB)")
    if 'release' in game:
        print(f"Release: {game['release']}")
    if 'serial' in game:
        print(f"Serial: {game['serial']}")
    if 'url' in game:
        print(f"URL: {game['url']}")
    if 'description' in game:
        print(f"\nDescription:\n  {game['description']}")

    # Tags
    if 'tags' in game and game['tags']:
        print(f"\nTags: {', '.join(game['tags'])}")

    # Check if downloaded
    game_path = CATALOG_DIR / game['filename']
    if game_path.exists():
        print(f"\nStatus: Downloaded to {game_path}")
    else:
        print(f"\nStatus: Not downloaded")
        print(f"  Run: ./get_games.py fetch --game {args.game_id}")


def load_size_cache():
    """Load previously cached file sizes"""
    if SIZE_CACHE_FILE.exists():
        try:
            with open(SIZE_CACHE_FILE, 'r') as f:
                return json.load(f)
        except (json.JSONDecodeError, IOError):
            pass
    return {}


def save_size_cache(cache):
    """Save file sizes to cache for future runs"""
    try:
        with open(SIZE_CACHE_FILE, 'w') as f:
            json.dump(cache, f, indent=2)
    except IOError:
        pass


def get_file_size(url, filename, cache):
    """Get actual file size, using cache if available"""
    if filename in cache:
        return cache[filename]

    try:
        response = requests.head(url, timeout=10)
        if response.status_code == 200:
            content_length = response.headers.get('content-length')
            if content_length:
                size = int(content_length)
                cache[filename] = size
                return size
    except:
        pass

    # Fallback size
    fallback_size = 100 * 1024
    cache[filename] = fallback_size
    return fallback_size


def get_version_info(entry):
    """Get a readable version description for display"""
    comment = entry.get("comment", "")

    if "Masterpieces version" in comment:
        return "Masterpieces"
    elif "Final-dev" in comment:
        return "Final-dev"
    elif comment and comment not in ["", "."]:
        return comment[:30] + ("..." if len(comment) > 30 else "")
    else:
        release = entry.get("release", "")
        serial = entry.get("serial", "")
        if release and serial:
            return f"r{release} s{serial}"
        elif release:
            return f"r{release}"
        else:
            return "Standard"


def filter_games_by_quality(catalog, quiet=False):
    """
    Filter to only .z3 files with version precedence:
    Masterpieces > Final-dev > Latest

    This ensures we only show the best version of each game.
    Set quiet=True to suppress the per-game listing (e.g. when called from download).
    """
    entries = catalog if isinstance(catalog, list) else []
    z3_games = []
    size_cache = load_size_cache()
    cache_updated = False

    if not quiet:
        print("Processing games and selecting best versions...")

    # Group games by name
    games_by_name = {}
    for entry in entries:
        if not isinstance(entry, dict) or entry.get("type") != "z3":
            continue

        name = entry.get("title", entry.get("filename", "").replace(".z3", "")).strip()
        if name not in games_by_name:
            games_by_name[name] = []
        games_by_name[name].append(entry)

    # Apply precedence for each game (best version only)
    for name, game_entries in games_by_name.items():
        selected_entry = None

        # 1. Try Masterpieces version (highest priority)
        for entry in game_entries:
            if "Masterpieces version" in entry.get("comment", ""):
                selected_entry = entry
                break

        # 2. Try Final-dev version
        if not selected_entry:
            for entry in game_entries:
                if "Final-dev" in entry.get("comment", ""):
                    selected_entry = entry
                    break

        # 3. Use latest version
        if not selected_entry:
            selected_entry = game_entries[-1]

        # Build game data
        dir_name = selected_entry.get("dir", "")
        filename = selected_entry.get("filename", "")
        href = f"{dir_name}/{filename}" if dir_name and filename else filename
        full_url = INFOCOM_BASE_URL + href if not href.startswith("http") else href
        version_info = get_version_info(selected_entry)

        # Get size (cached or fetch)
        if filename in size_cache:
            actual_size = size_cache[filename]
            if not quiet:
                print(f"  {len(z3_games)+1:2}. {name:<45} {actual_size//1024:4}KB  [{version_info}]")
        else:
            actual_size = get_file_size(full_url, filename, size_cache)
            cache_updated = True
            if not quiet:
                print(f"  {len(z3_games)+1:2}. {name:<45} {actual_size//1024:4}KB  [{version_info}] *")

        z3_games.append({
            "name": name,
            "href": href,
            "size": actual_size,
            "filename": filename,
            "title": selected_entry.get("title", name),
            "version_info": version_info
        })

    if cache_updated:
        save_size_cache(size_cache)
        if not quiet:
            print("\n  * = size fetched (cached for next time)")

    return z3_games


def cmd_download(args):
    """Download a specific game by name/ID from eblong.com (non-interactive)."""
    query = args.game.lower()
    print(f"Fetching catalog from eblong.com...")

    try:
        response = requests.get(INFOCOM_CATALOG_URL, timeout=30)
        response.raise_for_status()
        catalog = response.json()
    except Exception as e:
        print(f"Error: Could not fetch catalog: {e}")
        sys.exit(1)

    z3_games = filter_games_by_quality(catalog, quiet=True)

    # Match by name or filename prefix (case-insensitive)
    matches = [g for g in z3_games
               if query in g['name'].lower() or query in g['filename'].lower()]

    if not matches:
        print(f"Error: No game found matching '{args.game}'")
        print("Use 'get_games.py browse' to see available games.")
        sys.exit(1)

    if len(matches) > 1:
        print(f"\nAmbiguous match — {len(matches)} games match '{args.game}':")
        for g in matches:
            print(f"  {g['name']:<45} {g['filename']}")
        print("Use a more specific name.")
        sys.exit(1)

    game = matches[0]
    output_path = CATALOG_DIR / game['filename']

    if output_path.exists() and not args.force:
        print(f"\n[ok] {game['name']} already in catalog: {output_path}")
        return

    url = INFOCOM_BASE_URL + game['href'] if not game['href'].startswith("http") else game['href']
    print(f"\nDownloading {game['name']} ({game['size'] // 1024}KB)...", end='', flush=True)

    try:
        r = requests.get(url, timeout=30)
        r.raise_for_status()
        CATALOG_DIR.mkdir(parents=True, exist_ok=True)
        with open(output_path, 'wb') as f:
            f.write(r.content)
        print(f" [ok]")
        print(f"Saved to: {output_path}")
    except Exception as e:
        print(f" [error] {e}")
        sys.exit(1)



def cmd_sync(args):
    """Generate registry.json from eblong.com catalog.

    Fetches the live Infocom catalog, selects the best version of each Z3
    game, and writes registry.json.  The version/description/targets header
    is preserved from the existing registry if present.  The czech test-suite
    entry is always kept unchanged (it is not on eblong).

    Run this once after cloning, or whenever you want to pick up new catalog
    entries from eblong.
    """
    print("Fetching catalog from eblong.com...")
    try:
        response = requests.get(INFOCOM_CATALOG_URL, timeout=30)
        response.raise_for_status()
        catalog = response.json()
    except Exception as e:
        print(f"Error: Could not fetch catalog: {e}")
        sys.exit(1)

    z3_games = filter_games_by_quality(catalog, quiet=False)

    # Load existing registry to preserve the header and any
    # hand-curated per-game metadata (names, tags, default flag).
    existing_registry = {}
    if REGISTRY_FILE.exists():
        try:
            with open(REGISTRY_FILE, 'r') as f:
                existing_registry = json.load(f)
        except (json.JSONDecodeError, IOError):
            pass

    existing_by_id = {g['id']: g for g in existing_registry.get('games', [])}

    # Collect IDs from eblong so we know which existing entries are local-only
    eblong_ids = set()

    # Preserve local-only entries (not from eblong) at the front in their
    # original order.  These are games like czech.z3 or restaurant.z3 that
    # ship with the repo or were added manually.
    new_games = []

    for g in z3_games:
        filename = g['filename']

        # Derive a short ID: "zork1-r88-s840726.z3" → "zork1"
        game_id = re.sub(r'-r\d+.*', '', filename.replace('.z3', '')).lower()
        eblong_ids.add(game_id)

        # Extract release / serial from filename
        m = re.search(r'-r(\d+)-s(\w+)\.z3$', filename)
        release = int(m.group(1)) if m else None
        serial = m.group(2) if m else None

        url = (INFOCOM_BASE_URL + g['href']
               if not g['href'].startswith('http') else g['href'])

        entry = {
            'id': game_id,
            'name': g['name'],
            'filename': filename,
            'size': g['size'],
            'release': release,
            'serial': serial,
            'url': url,
            'tags': ['infocom'],
        }

        # Overlay any hand-curated fields from the existing registry entry.
        if game_id in existing_by_id:
            for field in ('name', 'tags', 'default', 'description', 'exclude'):
                if field in existing_by_id[game_id]:
                    entry[field] = existing_by_id[game_id][field]

        new_games.append(entry)

    # Prepend local-only games (not from eblong) in their original order
    local_games = [g for g in existing_registry.get('games', [])
                   if g['id'] not in eblong_ids]
    new_games = local_games + new_games

    registry = {
        'version': existing_registry.get('version', '1.0'),
        'description': existing_registry.get(
            'description', 'ZVibe game registry - manages games for all targets'),
        'targets': existing_registry.get('targets', {}),
        'games': new_games,
    }

    GAMES_DIR.mkdir(parents=True, exist_ok=True)
    with open(REGISTRY_FILE, 'w') as f:
        json.dump(registry, f, indent=2)
        f.write('\n')

    local_count = len(local_games)
    infocom_count = len(new_games) - local_count
    local_str = f" + {local_count} local" if local_count else ""
    print(f"\nWrote registry.json: {infocom_count} Infocom games{local_str}")
    print("Run 'get_games.py list' to see all games.")
    print("Run 'get_games.py download <name>' to download a game.")


def cmd_browse(args):
    """Interactive browse of Infocom catalog"""
    # Try to import fancy menu, fallback to simple mode
    try:
        from simple_term_menu import TerminalMenu
        use_fancy_menu = True
    except ImportError:
        use_fancy_menu = False
        print("Using simple text mode (install simple-term-menu for fancy UI)")
        print()

    print("Interactive Infocom Game Browser")
    print("=" * 50)
    print("Fetching catalog from eblong.com...")

    try:
        # Fetch Infocom catalog
        response = requests.get(INFOCOM_CATALOG_URL, timeout=30)
        response.raise_for_status()
        catalog = response.json()

        # Filter to best version of each Z3 game (Masterpieces > Final-dev > Latest)
        z3_games = filter_games_by_quality(catalog)

        if not z3_games:
            print("Error: No Z3 games found in catalog")
            return

        print(f"\nFound {len(z3_games)} Z3 games (best version of each)\n")

        # Use simple text mode if fancy menu not available
        if not use_fancy_menu:
            selected_games = browse_simple_mode(z3_games)
        else:
            selected_games = browse_fancy_mode(z3_games)

        if not selected_games:
            print("No games selected.")
            return

        # Download selected games
        print(f"\nDownloading {len(selected_games)} games...")
        CATALOG_DIR.mkdir(parents=True, exist_ok=True)

        success_count = 0
        for game in selected_games:
            output_path = CATALOG_DIR / game['filename']
            already_exists = output_path.exists()

            url = INFOCOM_BASE_URL + game['href'] if not game['href'].startswith("http") else game['href']

            if already_exists:
                print(f"Downloading {game['name']:50} [re-downloading] ", end='', flush=True)
            else:
                print(f"Downloading {game['name']:50} ", end='', flush=True)

            try:
                response = requests.get(url, timeout=30)
                response.raise_for_status()

                with open(output_path, 'wb') as f:
                    f.write(response.content)

                print(f"[ok] ({len(response.content) // 1024}KB)")
                success_count += 1

            except Exception as e:
                print(f"[error] {e}")

        print(f"\nDownloaded {success_count}/{len(selected_games)} games to {CATALOG_DIR}")

    except KeyboardInterrupt:
        print("\n\nCancelled")
    except Exception as e:
        print(f"\nError: {e}")
        sys.exit(1)


def browse_simple_mode(z3_games):
    """Simple text-based game browser (no external dependencies)"""
    selected_indices = set()

    while True:
        # Clear screen (simple version)
        print("\n" * 2)
        print("=" * 70)
        print("Infocom Game Browser - Simple Mode")
        print("=" * 70)

        # Show games with selection marks
        for i, game in enumerate(z3_games, 1):
            # Check if already downloaded
            already_downloaded = (CATALOG_DIR / game['filename']).exists()

            # Selection mark: +=selected, X=already downloaded, space=neither
            if (i-1) in selected_indices:
                mark = "+"
            elif already_downloaded:
                mark = "X"
            else:
                mark = " "

            size_kb = game['size'] // 1024
            version = game.get('version_info', '')
            version_str = f" [{version}]" if version else ""
            print(f"[{mark}] {i:3}. {game['name']:<40} ({size_kb:4}KB){version_str}")

        # Show summary
        total_size = sum(z3_games[i]['size'] for i in selected_indices)
        already_downloaded_count = sum(1 for g in z3_games if (CATALOG_DIR / g['filename']).exists())
        print("=" * 70)
        print(f"Selected: {len(selected_indices)} games, {total_size // 1024}KB total")
        print(f"Already downloaded: {already_downloaded_count} games")
        print()
        print("Commands:")
        print("  <number>     Toggle selection for game")
        print("  a            Select all")
        print("  c            Clear all selections")
        print("  d            Download selected games")
        print("  q            Quit without downloading")
        print()
        print("Legend: [+]=selected (will download)  [X]=in catalog (select to re-download)  [ ]=available")
        print("=" * 70)

        choice = input("\nYour choice: ").strip().lower()

        if choice == 'q':
            return []
        elif choice == 'd':
            return [z3_games[i] for i in selected_indices]
        elif choice == 'a':
            selected_indices = set(range(len(z3_games)))
        elif choice == 'c':
            selected_indices.clear()
        elif choice.isdigit():
            idx = int(choice) - 1
            if 0 <= idx < len(z3_games):
                if idx in selected_indices:
                    selected_indices.remove(idx)
                else:
                    selected_indices.add(idx)
            else:
                print(f"Invalid number: {choice}")
                input("Press ENTER to continue...")
        else:
            print(f"Unknown command: {choice}")
            input("Press ENTER to continue...")


def browse_fancy_mode(z3_games):
    """Fancy terminal menu browser (requires simple-term-menu)"""
    from simple_term_menu import TerminalMenu

    selected_indices = set()
    total_size = 0
    current_cursor_index = 0

    while True:
        # Create menu items
        menu_items = []
        already_downloaded_count = 0
        for i, game in enumerate(z3_games):
            # Check if already downloaded
            already_downloaded = (CATALOG_DIR / game['filename']).exists()
            if already_downloaded:
                already_downloaded_count += 1

            # Mark: +=selected, X=already downloaded, o=available
            if i in selected_indices:
                mark = "+"
            elif already_downloaded:
                mark = "X"
            else:
                mark = "o"

            size_kb = game['size'] // 1024
            version = game.get('version_info', '')
            version_str = f" [{version}]" if version else ""
            menu_items.append(f"{mark} {game['name']:<40} ({size_kb:3}KB){version_str}")

        menu_items.extend([
            "─" * 70,
            f"Selected: {len(selected_indices)} games, {total_size // 1024}KB | Already downloaded: {already_downloaded_count}",
            "─" * 70,
            "DOWNLOAD SELECTED GAMES",
            "Exit without downloading"
        ])

        title = (
            "Interactive Fiction Game Selector\n"
            f"Selected: {len(selected_indices)} games | Size: {total_size // 1024}KB | Downloaded: {already_downloaded_count}\n"
            "Use ↑↓ arrows, SPACE or ENTER to select games\n"
            "Legend: +=selected (will download)  X=in catalog (select to re-download)  o=available"
        )

        terminal_menu = TerminalMenu(
            menu_items,
            title=title,
            menu_cursor="> ",
            menu_cursor_style=("fg_red", "bold"),
            menu_highlight_style=("bg_blue", "fg_yellow"),
            cycle_cursor=True,
            clear_screen=True,
            cursor_index=current_cursor_index,
            accept_keys=("enter", " ")
        )

        menu_entry_index = terminal_menu.show()

        if menu_entry_index is None:
            return []

        current_cursor_index = menu_entry_index

        # Handle selection
        if menu_entry_index < len(z3_games):
            game_idx = menu_entry_index
            game = z3_games[game_idx]

            if game_idx in selected_indices:
                selected_indices.remove(game_idx)
                total_size -= game["size"]
            else:
                selected_indices.add(game_idx)
                total_size += game["size"]

        elif menu_entry_index == len(menu_items) - 2:
            # Download selected
            return [z3_games[i] for i in selected_indices]
        elif menu_entry_index == len(menu_items) - 1:
            # Exit
            return []


if __name__ == '__main__':
    parser = argparse.ArgumentParser(
        description='ZVibe Game Manager - Download and manage Z-machine games',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  ./get_games.py sync                    # Generate registry.json from eblong.com (run once after clone)
  ./get_games.py list                    # List games in registry
  ./get_games.py download plundered      # Download Plundered Hearts from eblong.com
  ./get_games.py download zork --force   # Force re-download Zork I
  ./get_games.py browse                  # Interactive browse of Infocom catalog
  ./get_games.py fetch                   # Download all games with URLs in registry
  ./get_games.py fetch --force           # Force re-download all
  ./get_games.py info zork1              # Show detailed info about Zork I
        """
    )

    subparsers = parser.add_subparsers(dest='command', help='Command to execute')

    # sync command — generate registry from eblong catalog
    sync_parser = subparsers.add_parser('sync',
                                        help='Generate registry.json from eblong.com catalog')
    sync_parser.set_defaults(func=cmd_sync)

    # browse command (new interactive mode)
    browse_parser = subparsers.add_parser('browse',
                                          help='Interactive browse of Infocom catalog')
    browse_parser.set_defaults(func=cmd_browse)

    # scan command
    scan_parser = subparsers.add_parser('scan',
                                        help='Scan catalog/ and add unregistered games to registry')
    scan_parser.set_defaults(func=cmd_scan)

    # fetch command
    fetch_parser = subparsers.add_parser('fetch', help='Download all games with URLs in registry')
    fetch_parser.add_argument('--force', action='store_true',
                               help='Force re-download even if file exists')
    fetch_parser.set_defaults(func=cmd_fetch)

    # list command
    list_parser = subparsers.add_parser('list', help='List games in registry')
    list_parser.set_defaults(func=cmd_list)

    # download command (non-interactive, by name)
    download_parser = subparsers.add_parser('download',
                                            help='Download a specific game from eblong.com by name')
    download_parser.add_argument('game', help='Game name or filename to download (partial match ok)')
    download_parser.add_argument('--force', action='store_true',
                                 help='Force re-download even if file exists')
    download_parser.set_defaults(func=cmd_download)

    # info command
    info_parser = subparsers.add_parser('info', help='Show detailed game information')
    info_parser.add_argument('game_id', help='Game ID to show info for')
    info_parser.set_defaults(func=cmd_info)

    # Parse and execute
    args = parser.parse_args()

    if not args.command:
        parser.print_help()
        sys.exit(1)

    args.func(args)
