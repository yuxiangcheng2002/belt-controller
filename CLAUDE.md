# Belt Controller

ESP-IDF 5.x firmware for ESP32-S3 (M5Stack DualKey + Chain Joystick).
Connects to Mac/iPad/DAW hosts as a BLE MIDI controller via NimBLE.

## Architecture
- `ble_midi.c/h` — BLE MIDI service + characteristic (official MIDI UUIDs). Handles advertising, GATT services, and packetized MIDI notifications.
- `dualkey_hw.c/h` — Button reading (GPIO0, GPIO17) and WS2812 LED on DualKey board.
- `chain_joystick.c/h` — UART chain protocol for M5Stack Chain Joystick. Modular: deinits UART if not detected (power saving).
- `app_main.c` — Profile switching, MIDI mapping, LED feedback, power management.

## Profiles
- **Notes** (blue LED): Button A/B and joystick directions emit MIDI notes.
- **CC** (green LED): Buttons emit on/off CC values and joystick X/Y emit continuous CCs.
- **Toggle**: Hold both buttons for 2 seconds. White flash confirms switch.

## Build
```
idf.py set-target esp32s3
idf.py build
idf.py -p /dev/tty.usbmodemXXXX flash monitor
```
Requires Python 3.11: `export PATH="/Library/Frameworks/Python.framework/Versions/3.11/bin:$PATH"`

## Power management
Three-tier model: **ACTIVE** → **IDLE** → **DEEP SLEEP**.

- **ACTIVE** (~55mA): BLE 15ms interval, latency 0, 20ms poll loop, LEDs on.
- **IDLE** (after 30s, ~1–3mA): LEDs + power rail off, light sleep via tickless idle. Button press wakes instantly.
- **DEEP SLEEP** (after 30min idle, ~108μA): Full deep sleep. BLE disconnects. Button press reboots chip → BLE reconnects (~1–2s). Profile survives via `RTC_DATA_ATTR`.

Key config:
- `CONFIG_PM_ENABLE=y` + `CONFIG_FREERTOS_USE_TICKLESS_IDLE=y`
- `esp_sleep_enable_ext1_wakeup_io()` on GPIO0 + GPIO17 for deep sleep wake
- `gpio_wakeup_enable()` on both buttons for light sleep wake

## Key decisions
- BLE MIDI transport instead of HID — better fit for synths, DAWs, and Audio MIDI Setup on Apple hosts.
- Two controller profiles share the same BLE MIDI endpoint, so profile changes do not require reconnecting.
- Joystick is modular: if probe fails, UART is deinitialized to save power. Runs buttons-only.
- Both-button simultaneous press is reserved for profile toggle, never sent as input.
- LED refresh rate-limited to ~33Hz (30ms) — 200Hz (every 5ms poll) caused Interrupt WDT timeout because RMT refresh is slow.
- Joystick UART reads rate-limited to ~50Hz (20ms) to avoid blocking.
- Joystick LED commands (set_brightness, set_rgb) only at startup/profile-change — each does full UART round-trip with 50ms timeout, can't be in hot loop.
- The BLE MIDI characteristic is notify/write capable so hosts can subscribe normally.
- Security is left open by default; unlike HID, BLE MIDI does not need enforced pairing to work with macOS/iPadOS.

## Dead ends / constraints
- HID-specific decisions from the old presentation/gamepad firmware no longer apply in the MIDI fork.
- `CONFIG_BT_LE_SLEEP_ENABLE` does not exist for ESP32-S3 — only for C2/C5/C6/H2. S3 BT controller sleeps automatically via PM framework.
- `ble_store_config_init()` removed in ESP-IDF 5.5 NimBLE — store auto-inits when `CONFIG_BT_NIMBLE_NVS_PERSIST=y`.
- Arduino ESP32 2.0.x port failed: Adafruit NeoPixel and `neopixelWrite` both fail to drive WS2812 LEDs on this board. USB CDC serial output never worked either. Multiple platform incompatibilities with 2MB flash ESP32-S3.

## Joystick knowledge (not yet in code)
These findings are from debugging but not currently implemented — the code uses the simple probe (GET_DEVICE_TYPE only) on the RIGHT connector:

- **Working connector**: LEFT UART2, TX=GPIO6, RX=GPIO5 (normal pin orientation)
- **Joystick requires enumeration** before responding to device-specific commands:
  1. Heartbeat (0xFD) broadcast to ID 0xFF — wakes the bus
  2. Enumerate (0xFE) broadcast with payload 0x00 — assigns IDs, returns device count
  3. Then GET_DEVICE_TYPE (0xFB) to ID 1 works
- Without enumeration, joystick never responds to GET_DEVICE_TYPE
- Chain bus packet format: `AA 55 [len_lo len_hi] [id] [cmd] [payload...] [crc] 55 AA`

## 3-position switch (not yet implemented)
- GPIO 7 (SW_RAIN): high when switch in leftmost position
- GPIO 8 (SW_BLE): high when switch in rightmost position
- Plan: leftmost = deep sleep / off, other positions = on
