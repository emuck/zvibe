# User Guide

Command-line usage and interpreter behavior.

## Console Applications

### zvibe_console

Full-featured interpreter with status bar and saves.

```bash
./zvibe_console [options] <story.z3>
```

**Features:**
- Status bar display (location, score, turns)
- Persistent saves to filesystem
- Terminal control sequences for formatting

### zvibe_minimal

Minimal interpreter for testing and scripted runs.

```bash
./zvibe_minimal <story.z3>
```

**Features:**
- Plain text output
- No save/restore support
- Suitable for automated testing

## Input and Output

### Input

- Type commands at the prompt and press Enter
- Standard Infocom commands: `LOOK`, `INVENTORY`, `GO NORTH`, `TAKE LAMP`, etc.
- Game-specific commands vary by title

### Output

- Room descriptions and responses appear as text
- Status bar (zvibe_console only) shows current location and score

### Special Commands

| Command | Behavior |
|---------|----------|
| `SAVE` | Save game state (zvibe_console only) |
| `RESTORE` | Load saved game state |
| `RESTART` | Reset to initial game state |
| `QUIT` | Exit interpreter |

## Save and Restore

### Console Target

Saves are stored as files in the current directory.

**Save:**
```
>SAVE
Enter filename: mysave.sav
Saved.
```

**Restore:**
```
>RESTORE
Enter filename: mysave.sav
Restored.
```

### Embedded Targets

Save behavior varies by target:

| Target | Storage | Slots | Notes |
|--------|---------|-------|-------|
| same51 | SmartEEPROM | 3 per game | Wear-leveled |
| riscv | QSPI Flash | 3 per game | Delta compression, wear-leveled |

On embedded targets, SAVE/RESTORE prompts for a slot number (1-3).

### RESTART

The `RESTART` command resets the game to its initial state:
- Dynamic memory restored from original story data
- Stack cleared
- PC reset to start address
- Does not affect saved games

## Error Handling

### Fatal Errors

The interpreter halts on:
- Invalid story file format
- Unsupported Z-machine version (only v3 supported)
- Stack overflow
- Memory access out of bounds

### Recoverable Errors

The interpreter continues after:
- Invalid user input (prompts again)
- Save/restore failures (reports error, continues game)

## Scripted Input

For automated testing, pipe commands to the interpreter:

```bash
echo -e "look\ninventory\nquit" | ./zvibe_minimal game.z3
```

Or use a command file:

```bash
./zvibe_minimal game.z3 < commands.txt
```

## Python Library

The console target builds a shared library (`libzvibe.so`) for Python integration:

```python
import ctypes

lib = ctypes.CDLL('./build/lib/libzvibe.so')
# See target/console/zvibe_console.py for full example
```
