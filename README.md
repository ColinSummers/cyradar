# KFHR Radar

A desk-sized flight radar for the [Cheap Yellow Display](https://github.com/witnessmenow/ESP32-Cheap-Yellow-Display) (ESP32-2432S028R), centred on Friday Harbor Airport (KFHR) in the San Juan Islands, WA.

Based on [micro-radar](https://github.com/AnthonySturdy/micro-radar) by Anthony Sturdy, adapted for the CYD's ILI9341 320×240 display.

## Hardware

- **ESP32-2432S028R** — the "Cheap Yellow Display" with 2.8" ILI9341 (320×240), XPT2046 touch, ESP32-WROOM
- Powered via USB

## Setup

1. Copy `include/Config.h.example` to `include/Config.h` and fill in your Wi-Fi and [OpenSky Network](https://opensky-network.org/) API credentials.
2. Build and upload with PlatformIO:
   ```
   pio run -t upload
   ```
3. On first boot with no Wi-Fi configured, the device creates a hotspot called **KFHR-Radar-Setup** — connect and enter your Wi-Fi credentials.
4. Once connected, visit `http://kfhr-radar.local` to adjust the radar centre, radius, and display options.

## Display

- **Left 240×240**: radar scope with sweep line, range rings, crosshairs, and aircraft blips
- **Right 80px sidebar**: station ID (KFHR), scan radius, aircraft count, callsign list

Aircraft show as directional triangles with callsign and altitude labels. Data refreshes from the OpenSky Network API at the maximum rate your credentials allow (~22s with authentication).

## Credits

- [micro-radar](https://github.com/AnthonySturdy/micro-radar) — Anthony Sturdy
- [Blog walkthrough](https://abidcg.blogspot.com/2026/07/micro-radar-cyd.html) — AbidCG / Lazy Day
- [OpenSky Network](https://opensky-network.org/) — live ADS-B data
