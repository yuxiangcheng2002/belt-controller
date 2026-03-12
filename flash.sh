#!/bin/bash
# Flash and monitor Belt Controller firmware.
# Usage: ./flash.sh [PORT]
#   PORT defaults to auto-detect (/dev/tty.usbmodem*)

set -e

# Source ESP-IDF (needs Python 3.11 first in PATH)
export PATH="/Library/Frameworks/Python.framework/Versions/3.11/bin:$PATH"
. "$HOME/esp/esp-idf/export.sh" >/dev/null 2>&1

cd "$(dirname "$0")"

PORT="${1:-}"

if [ -z "$PORT" ]; then
    # Auto-detect USB serial port
    PORT=$(ls /dev/tty.usbmodem* 2>/dev/null | head -1)
    if [ -z "$PORT" ]; then
        echo "No USB device found. Plug in DualKey and try again."
        echo "Usage: $0 [/dev/tty.usbmodemXXXX]"
        exit 1
    fi
    echo "Auto-detected: $PORT"
fi

echo "Building..."
idf.py build

echo ""
echo "Flashing to $PORT..."
idf.py -p "$PORT" flash

echo ""
echo "Starting monitor (Ctrl+] to exit)..."
idf.py -p "$PORT" monitor
