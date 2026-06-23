#!/usr/bin/env python3
# Copyright (c) 2025 Martin R. Raumann
# SPDX-License-Identifier: BSD-3-Clause
"""
ZVibe Game Manager - Interactive TUI for game selection and building
"""

import json
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from game_builder import common, same51
from get_games import download_game

try:
    from simple_term_menu import TerminalMenu
    HAS_FANCY_MENU = True
except ImportError:
    HAS_FANCY_MENU = False


def save_registry(registry):
    with open(common.REGISTRY_FILE, 'w') as f:
        json.dump(registry, f, indent=2)
        f.write('\n')


def is_downloaded(game):
    return (common.CATALOG_DIR / game['filename']).exists()


def is_selected(game):
    return is_downloaded(game) and not game.get('exclude', False)


def format_budget_line(registry):
    budgets = common.compute_flash_budgets(registry)
    parts = []
    for target, b in budgets.items():
        used_kb = b['used'] // 1024
        avail = b['available']
        if avail:
            avail_kb = avail // 1024
            warn = " !!" if b['used'] > avail else ""
            parts.append(f"{target} {used_kb}/{avail_kb}KB{warn}")
        else:
            parts.append(f"{target} {used_kb}KB")
    return "  ".join(parts)


def format_game_item(game):
    if is_selected(game):
        mark = "X"
    elif is_downloaded(game):
        mark = "-"
    else:
        mark = "·"

    default = "  (default)" if game.get('default', False) else ""
    status = "downloaded" if is_downloaded(game) else "not downloaded"
    return f"[{mark}]  {game['name']:<38} {game['size'] // 1024:>4}KB   {status}{default}"


def toggle_game(game):
    if not is_downloaded(game):
        return
    if game.get('exclude', False):
        del game['exclude']
    else:
        game['exclude'] = True


def do_download(registry):
    print("\nDownloading games...\n")
    count = 0
    for game in registry['games']:
        if 'url' in game and not is_downloaded(game):
            if download_game(game):
                count += 1
    if count == 0:
        print("All games already downloaded.")
    else:
        print(f"\nDownloaded {count} game(s).")
    input("\nPress ENTER to continue...")


def do_sync():
    print("\nRunning sync...")
    subprocess.run(
        [sys.executable, str(common.GAMES_DIR / 'get_games.py'), 'sync'],
    )
    input("\nPress ENTER to continue...")
    return common.load_registry()


def do_build(registry):
    budgets = common.compute_flash_budgets(registry)
    valid_targets = []
    for target, b in budgets.items():
        if b['available'] is None or b['used'] <= b['available']:
            used_kb = b['used'] // 1024
            avail = b['available']
            if avail:
                label = f"{target} ({used_kb}/{avail // 1024}KB)"
            else:
                label = f"{target} ({used_kb}KB, unlimited)"
            valid_targets.append((target, label))

    if not valid_targets:
        print("\nNo targets fit the current selection.")
        input("Press ENTER to continue...")
        return

    print("\nSelect build target:\n")
    for i, (_, label) in enumerate(valid_targets, 1):
        print(f"  {i}. {label}")
    print(f"  q. Cancel")

    choice = input("\nYour choice: ").strip().lower()
    if choice == 'q' or not choice:
        return

    try:
        idx = int(choice) - 1
        if idx < 0 or idx >= len(valid_targets):
            print("Invalid choice.")
            input("Press ENTER to continue...")
            return
    except ValueError:
        print("Invalid choice.")
        input("Press ENTER to continue...")
        return

    target = valid_targets[idx][0]
    print(f"\nBuilding for {target}...\n")

    if target == 'same51':
        output_dir = common.PROJECT_ROOT / "target" / "same51" / "src"
        try:
            same51.build_same51(registry, output_dir)
        except Exception as e:
            print(f"\nError: {e}")
    else:
        try:
            games = common.get_downloaded_games(target, registry)
            print(f"{len(games)} games selected for {target}:")
            for g in games:
                print(f"  {g['id']:<20} {g['size'] // 1024:>4}KB  {g['name']}")
        except Exception as e:
            print(f"\nError: {e}")

    input("\nPress ENTER to continue...")


# --- Fancy TUI mode (simple-term-menu) ---

def main_fancy(registry):
    cursor_index = 0
    games = registry['games']

    while True:
        budget_line = format_budget_line(registry)
        title = (
            "\n  ZVibe Game Manager\n"
            f"  {'─' * 60}\n"
            f"  {budget_line}\n"
            f"  {'─' * 60}\n"
        )

        menu_items = [format_game_item(g) for g in games]
        menu_items.append(f"{'─' * 60}")
        menu_items.append("[D] Download    [B] Build    [S] Sync    [Q] Quit")

        menu = TerminalMenu(
            menu_items,
            title=title,
            menu_cursor="> ",
            menu_cursor_style=("fg_red", "bold"),
            menu_highlight_style=("bg_blue", "fg_yellow"),
            cycle_cursor=True,
            clear_screen=True,
            cursor_index=cursor_index,
            accept_keys=("enter", " ", "d", "b", "s", "q"),
        )

        choice = menu.show()
        key = menu.chosen_accept_key

        if choice is None:
            break

        if key == "q":
            break
        elif key == "d":
            do_download(registry)
        elif key == "b":
            do_build(registry)
        elif key == "s":
            registry = do_sync()
            games = registry['games']
        elif choice < len(games):
            toggle_game(games[choice])
            cursor_index = choice
        else:
            cursor_index = choice

    return registry


# --- Simple text mode fallback ---

def main_simple(registry):
    games = registry['games']

    while True:
        print(f"\n{'=' * 65}")
        print("ZVibe Game Manager")
        print(f"{'=' * 65}")
        print(f"  {format_budget_line(registry)}")
        print(f"{'─' * 65}")

        for i, game in enumerate(games, 1):
            print(f"  {i:3}. {format_game_item(game)}")

        print(f"{'─' * 65}")
        print("  Commands: <number> toggle | d download | b build | s sync | q quit")
        print(f"{'=' * 65}")

        choice = input("\nYour choice: ").strip().lower()

        if choice == 'q':
            break
        elif choice == 'd':
            do_download(registry)
        elif choice == 'b':
            do_build(registry)
        elif choice == 's':
            registry = do_sync()
            games = registry['games']
        elif choice.isdigit():
            idx = int(choice) - 1
            if 0 <= idx < len(games):
                toggle_game(games[idx])
            else:
                print(f"Invalid number (1-{len(games)})")
        else:
            print(f"Unknown command: {choice}")

    return registry


def main():
    registry = common.load_registry()

    try:
        if HAS_FANCY_MENU:
            registry = main_fancy(registry)
        else:
            print("(install simple-term-menu for fancy UI)\n")
            registry = main_simple(registry)
    except KeyboardInterrupt:
        print("\n\nCancelled (changes not saved)")
        return

    save_registry(registry)
    print("Saved selections to registry.json")


if __name__ == '__main__':
    main()
