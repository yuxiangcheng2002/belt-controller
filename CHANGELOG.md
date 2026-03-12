# Changelog

## v1.0-stable-baseline (2026-03-12)

### Working
- **BLE HID keyboard**: Connects to Mac as "Belt Controller", sends left/right arrow keys via buttons
- **BLE HID gamepad**: Alternate profile with 2 buttons + X/Y axes
- **Profile switching**: Hold both buttons 2s → toggles keyboard/gamepad, white flash confirms
- **Reactive LEDs**: Bright flash on press, exponential fade on release, breathing when idle
- **LED colors**: Blue = keyboard profile, green = gamepad profile
- **Power management**: Active → Idle (30s, LEDs off) → Deep sleep (30min, ~108μA)
- **Deep sleep wake**: Button press reboots, profile persists via RTC memory
- **BLE bonding**: Persists across deep sleep/reboot via NVS
- **BLE encryption recovery**: Stale bond auto-cleared on encryption failure
- **Flash helper**: `./flash.sh` auto-detects port, builds, flashes, monitors
- **Web test interface**: `test.html` for BLE connection testing and input visualization

### Not working
- **Chain joystick**: Probe uses wrong connector (RIGHT) and wrong sequence (no enumeration). See CLAUDE.md "Joystick knowledge" for the fix.
- **3-position switch**: Not implemented. GPIO 7/8 wired but not read.
- **BLE connection stability**: Occasional disconnects reported. No `ble_gap_update_params` (macOS rejects it). May need further investigation.
- **Serial monitor**: Opening USB-Serial/JTAG port resets ESP32-S3 into download mode. Use `idf.py monitor` or physical replug after flash.

### Dead ends
- **Arduino port**: Adafruit NeoPixel and `neopixelWrite` both failed to drive WS2812 LEDs on this board. USB CDC serial never worked. Abandoned.
- **`ble_gap_update_params`**: macOS rejects slave latency changes and drops connection. Removed entirely.
- **LED refresh at 200Hz**: Caused Interrupt WDT timeout. Fixed by rate-limiting to 33Hz (30ms).

## Pre-v1.0 commits

- `9629630` Fix LED count to 2 (per M5Stack DualKey hardware)
- `4d5660d` Gamepad mode: map buttons to L/R axis when no joystick
- `715bdcf` Add composite HID (keyboard + gamepad) with profile switching
- `640e628` Replace ESP-NOW with BLE HID gamepad for macOS
- `680a4d1` Initial import from croc-remote
