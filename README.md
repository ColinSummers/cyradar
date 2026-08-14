# CYRadar

Cheap Yellow Radar — a desk-sized flight radar for the [Cheap Yellow Display](https://github.com/witnessmenow/ESP32-Cheap-Yellow-Display) (ESP32-2432S028R).

Based on [micro-radar](https://github.com/AnthonySturdy/micro-radar) by Anthony Sturdy, adapted for the CYD's ILI9341 320×240 display with weather overlays and configurable airport support.

## Hardware

- **ESP32-2432S028R** — the "Cheap Yellow Display" with 2.8" ILI9341 (320×240), XPT2046 touch, ESP32-WROOM
- Powered via USB

## Setup

1. Copy `include/Config.h.example` to `include/Config.h` and fill in your Wi-Fi and [OpenSky Network](https://opensky-network.org/) API credentials.
2. Build and upload with PlatformIO:
   ```
   pio run -t upload
   ```
3. On first boot with no Wi-Fi configured, the device creates a hotspot — connect and enter your Wi-Fi credentials.
4. Once connected, visit the device's IP to configure airport, runways, weather stations, and display options.

## Display

- **Left 240×240**: radar scope with sweep line, range rings, coastline overlay, runway, and aircraft blips
- **Right 80px sidebar**: station ID, scan diameter, aircraft count, callsign list
- **METAR detail** (tap top-right): weather conditions, crosswind for all runways, raw observation
- **Weather map** (tap bottom-right): METAR stations with flight category dots
- **TAF grid** (tap bottom-right on weather map): forecast flight categories

Aircraft show as directional triangles with callsign and altitude labels. Data refreshes from the OpenSky Network API at the maximum rate your credentials allow (~22s with authentication).

## Credits

- [The Real Hacksaw's Tiny Desk Radar](https://www.instagram.com/therealhacksaw/) — inspiration
- [micro-radar](https://github.com/AnthonySturdy/micro-radar) — Anthony Sturdy
- [Blog walkthrough](https://abidcg.blogspot.com/2026/07/micro-radar-cyd.html) — AbidCG / Lazy Day
- [OpenSky Network](https://opensky-network.org/) — live ADS-B data
- [OurAirports](https://ourairports.com/) — runway data
- [aviationweather.gov](https://aviationweather.gov/) — METAR and TAF data
