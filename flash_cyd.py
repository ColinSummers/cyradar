#!/usr/bin/env python3
"""Flash CYRadar firmware to a CYD board via USB.

Usage:
    python3 flash_cyd.py cyradar-cyd-1.3.7.bin

Writes the firmware to both OTA slots so the board boots the new version
regardless of which slot was previously active. Does not erase anything
outside the app partitions, so WiFi credentials are preserved.
"""

import subprocess
import sys
import glob

APP0_ADDR = "0x10000"
APP1_ADDR = "0x1F0000"

def find_serial_port():
    candidates = glob.glob("/dev/cu.usbserial-*")
    if len(candidates) == 1:
        return candidates[0]
    if len(candidates) > 1:
        print(f"Multiple serial ports found: {', '.join(candidates)}")
        print("Unplug other devices or pass the port manually.")
        sys.exit(1)
    print("No /dev/cu.usbserial-* port found. Is the board plugged in?")
    sys.exit(1)

def main():
    if len(sys.argv) != 2:
        print(f"Usage: python3 {sys.argv[0]} <firmware.bin>")
        sys.exit(1)

    firmware = sys.argv[1]
    port = find_serial_port()

    print(f"Port:     {port}")
    print(f"Firmware: {firmware}")
    print()

    print("Flashing firmware to both OTA slots...")
    args = [
        "esptool", "--chip", "esp32", "--baud", "460800", "--port", port,
        "write-flash", APP0_ADDR, firmware, APP1_ADDR, firmware,
    ]
    print(f"  → {' '.join(args)}")
    result = subprocess.run(args)
    if result.returncode != 0:
        print(f"\nFailed (exit {result.returncode}). Check the output above.")
        sys.exit(1)

    print()
    print("Done! Unplug and replug the board -- it should boot the new version.")

if __name__ == "__main__":
    main()
