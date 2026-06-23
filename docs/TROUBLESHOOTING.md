# Troubleshooting

Common issues and solutions.

## Console Target

### Build fails with missing headers

**Symptom:** `fatal error: zvibe.h: No such file or directory`

**Cause:** Building from wrong directory.

**Fix:** Build from `target/console/`:
```bash
cd target/console
make all
```

### Game won't load

**Symptom:** `Unsupported Z-machine version`

**Cause:** ZVibe only supports version 3 games.

**Fix:** Use a `.z3` file. Check version with:
```bash
hexdump -C game.z3 | head -1
# First byte should be 03
```

### No output appears

**Symptom:** Game runs but nothing prints.

**Cause:** Output buffering or terminal issue.

**Fix:** Try `zvibe_minimal` instead of `zvibe_console`. Check terminal settings.

## RISC-V FPGA Target

### Build fails: SERV not found

**Symptom:** Missing SERV RTL files during synthesis.

**Cause:** Submodule not initialized.

**Fix** (from repo root):
```bash
git submodule update --init target/riscv/serv_zvibe/serv
```

### No UART output after programming

**Symptom:** Board programmed but no response on serial.

**Cause:** Wrong serial port, baud rate, or stale firmware in flash.

**Fix (MAX10):**
1. UART module connects to CP2102 — typically `/dev/ttyUSB0`
2. Baud rate must be 115200
3. Power cycle after programming

**Fix (Arty):**
1. UART is the FT2232H second channel — typically `/dev/ttyUSB1` or `/dev/ttyUSB2`
2. Baud rate must be 115200
3. Power cycle after programming

### Garbage characters on UART

**Symptom:** Random characters instead of game output.

**Cause:** Baud rate mismatch.

**Fix:** Verify 115200 baud, 8N1 settings:
```bash
picocom -b 115200 /dev/ttyUSB0    # MAX10
picocom -b 115200 /dev/ttyUSB1    # Arty
```

### CPU stuck, no boot

**Symptom:** Board powered but no activity.

**Cause:** Flash programming incomplete or firmware not built.

**Fix (MAX10):**
1. Reprogram: `cd boards/max10_08_eval/fpga && make program-complete`
2. Power cycle board

**Fix (Arty):**
1. Reprogram: `cd boards/arty_s7_50/fpga && make program-complete`
2. Power cycle board

### Quartus not found (MAX10)

**Symptom:** `quartus_sh: command not found`

**Cause:** Quartus bin directory not in PATH.

**Fix:**
```bash
export PATH=/path/to/quartus/bin:$PATH
which quartus_sh   # verify
```

### USB-Blaster not detected (MAX10)

**Symptom:** `quartus_pgm` reports no JTAG chain.

**Cause:** Missing udev rule.

**Fix:**
```bash
echo 'SUBSYSTEM=="usb", ATTR{idVendor}=="09fb", MODE="0666"' | \
  sudo tee /etc/udev/rules.d/51-usbblaster.rules
sudo udevadm control --reload-rules
lsusb | grep Altera   # verify USB-Blaster appears
```

### Bitstream won't build (Arty)

**Symptom:** Vivado synthesis fails.

**Cause:** Various RTL or constraint issues.

**Fix:**
1. Clean and rebuild: `make clean && make build`
2. Check Vivado version (2024.x or 2025.1 required)
3. Review `build/servant_zvibe_arty_s7_50.runs/synth_1/runme.log`

## SAM E51 Target

### Programming fails

**Symptom:** OpenOCD or programmer errors.

**Cause:** Device not detected or locked.

**Fix:**
1. Check USB connection
2. Verify device is powered
3. Try chip erase if device is locked

### Save fails

**Symptom:** SAVE command reports error.

**Cause:** SmartEEPROM not configured.

**Fix:** SmartEEPROM must be enabled in fuses. See `target/same51/README.md`.

## Game Management Tools

### get_games.py fails with requests error

**Symptom:** `ModuleNotFoundError: No module named 'requests'`

**Cause:** Python package not installed.

**Fix:**
```bash
pip3 install requests
```

### Build produces empty games.bin

**Symptom:** `games.bin` is 0 bytes or very small.

**Cause:** No games in registry or catalog.

**Fix:**
1. Download games: `cd games && ./get_games.py fetch`
2. Check `games/catalog/` has `.z3` files
3. Check `games/registry.json` has entries

## Testing

### Tests fail to find game files

**Symptom:** `Game file not found: games/catalog/czech.z3`

**Cause:** Running tests from wrong directory.

**Fix:**
```bash
cd target/console/tests
make test
```

### Integration tests hang

**Symptom:** Test script doesn't complete.

**Cause:** Missing script file or game file.

**Fix:** Verify files exist:
```bash
ls games/catalog/
ls games/scripts/
```
