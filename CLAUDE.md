# Belt Controller

ESP-IDF 5.x firmware for ESP32-S3 (M5Stack DualKey + Chain Joystick).
Connects to Mac as a composite BLE HID device (keyboard + gamepad) via NimBLE.

## Architecture
- `ble_hid.c/h` — Composite NimBLE HID: Report ID 1 (keyboard) + Report ID 2 (gamepad). Handles advertising, pairing, GATT services.
- `dualkey_hw.c/h` — Button reading (GPIO0, GPIO17) and WS2812 LED on DualKey board.
- `chain_joystick.c/h` — UART chain protocol for M5Stack Chain Joystick. Modular: deinits UART if not detected (power saving).
- `app_main.c` — Profile switching, input mapping, LED feedback.

## Profiles
- **Keyboard** (blue LED): Joystick left/right → arrow keys, Button A → Enter, Button B → Escape. Great for presentations.
- **Gamepad** (green LED): Raw joystick X/Y + 2 buttons as HID gamepad.
- **Toggle**: Hold both buttons for 2 seconds. White flash confirms switch.

## Build
```
idf.py set-target esp32s3
idf.py build
idf.py -p /dev/tty.usbmodemXXXX flash monitor
```

## Key decisions
- Composite HID (keyboard + gamepad in one descriptor) — no re-pairing needed when switching profiles.
- "Just Works" pairing — macOS requires encryption for HID.
- Joystick is modular: if probe fails, UART is deinitialized to save power. Runs buttons-only.
- Both-button simultaneous press is reserved for profile toggle, never sent as input.

## Dead ends / constraints
- Single HID descriptor approach (gamepad only) doesn't work for presentation arrow keys — macOS needs keyboard HID for that.
