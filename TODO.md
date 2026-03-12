# TODO

## High priority
- [ ] **Fix chain joystick**: Switch to LEFT UART2 (TX=6, RX=5) and add enumeration sequence (heartbeat → enum → get device type). See CLAUDE.md "Joystick knowledge".
- [ ] **BLE MIDI host testing**: Verify the new MIDI service on macOS Audio MIDI Setup, Ableton, and iPad hosts.
- [ ] **Finalize MIDI mapping**: Tune note layout, CC numbers, and velocity/range based on actual musical workflow.

## Medium priority
- [ ] **3-position switch**: Read GPIO 7/8, leftmost position = deep sleep / off, other positions = on.
- [ ] **Joystick LED feedback**: Set joystick ring LED color to match active profile.

## Low priority / nice-to-have
- [ ] **Battery monitoring**: Read VBAT on GPIO 10 (ADC, 1.51x divider), report via BLE Battery Service.
- [ ] **Charge detection**: Read GPIO 9 (charge status) and GPIO 2 (VBUS detect).
- [ ] **Light sleep**: Re-enable `light_sleep_enable` in PM config once LED/RMT stability is confirmed.
- [ ] **Serial monitor fix**: Investigate why opening USB-Serial/JTAG resets into download mode. May need `--after no_reset` or different esptool config.

## Done
- [x] Composite BLE HID (keyboard + gamepad)
- [x] Profile switching (2s hold)
- [x] Reactive LED system with breathing
- [x] LED rate limiting (33Hz) — WDT fix
- [x] 3-tier power management (active/idle/deep sleep)
- [x] BLE encryption error recovery
- [x] Web test interface
- [x] Flash helper script
