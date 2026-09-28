# CYRadar — Fix These

Remaining items from Fable review (2026-08-30), ranked by severity.

## HIGH

### H2. Secrets baked into publicly downloadable binary
`include/Config.h:4-9` — WiFi SSID/password and OpenSky client secret compiled into
firmware published unauthenticated at `FW_BINARY_URL`. `strings firmware.bin` exposes them.
`opensky-credentials.json` also sitting untracked in repo root.

**Fix:** Ship release builds with `WIFI_SSID ""` / `WIFI_PASS ""` (WiFiManager portal
covers provisioning). Leave OpenSky creds to per-device NVS config via web page. Add
`opensky-credentials.json` to `.gitignore`.

### H3. OTA updates are unauthenticated
`src/main.cpp:253-277` — Both version check and `httpUpdate.update()` use
`WiFiClientSecure::setInsecure()`. MITM can serve arbitrary firmware = code execution.

**Fix:** Pin george's CA cert (`setCACert`) for the OTA path only. Heap cost paid just
during daily check when sprite is already freed.

## MEDIUM

### M9. Per-frame String churn fragments heap
`src/AircraftManager.cpp:99-113,186,236-243` — `IsKnownTail` allocates 2+ temporary
Strings per aircraft per frame. Plus `String(diamNm) + "nm"` etc every loop.

**Fix:** Normalize callsign once in Update(), use stack char buffers with snprintf.

## LOW

### S1. Defaults defined in three places
ApplyDefaults, main.cpp fallbacks, AircraftManager defaults. ApplyDefaults guarantees
NVS is populated, so the others are dead code that will drift.

### S2. Dead fields on Aircraft struct
originCountry, squawk, spi, positionSource, category, verticalRate, geoAltitude —
parsed and heap-allocated but never displayed. Trim to save per-aircraft heap.

### S3. Two HTTP client wrappers
HttpFetch.h vs HttpRequestManager — two idioms for the same thing.

### S5. checkin URL not escaped
main.cpp:167-173 — airport/user from NVS interpolated raw into query string.
