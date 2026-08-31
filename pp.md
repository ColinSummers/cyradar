# Practice of Programming Review — CYRadar

Fable review, 2026-08-30. Ranked by severity.

## Status

- [x] H1 — Cache bounds clamping + schema versioning
- [ ] H2 — Secrets in binary (release-process change, not code)
- [ ] H3 — OTA TLS pinning (needs george's CA cert)
- [x] H4 — JSON parser bounded to object end
- [x] M1 — Preferences race fixed (function-local)
- [x] M2 — Failed fetch preserves cached data
- [x] M3 — Button rects extracted to RadarLayout.h
- [x] M4 — OpenSky bounding box precision (4 decimals)
- [x] M5 — Flash wear reduced (skip unchanged saves, 5-min checkin)
- [x] M6 — TAF label safe for empty/non-K stations
- [x] M7 — METAR detail shows actual station on fallback
- [x] M8 — Boot sprite uses recreateSprite()
- [ ] M9 — Per-frame String churn (larger refactor)
- [x] M10 — Millis wraparound fixed
- [x] S4 — WiFi autoConnect return checked
- [x] S9 — Raw METAR line split char-drop fixed
- [x] .gitignore — opensky-credentials.json added
- [x] S3 — Dead `updated` return removed from checkHttpOta

## HIGH

### H1. Weather cache load trusts NVS counts — potential buffer overflow
`src/main.cpp:209-224` — `wxCacheLoad()` reads `mc`/`tc` from NVS unclamped. If NVS
holds a count > `WX_MAX_STATIONS` (corruption, or cache written by older firmware with
different struct layout), writes past fixed arrays. No schema versioning either — an OTA
that changes WxMetar/WxTaf layout silently reinterprets old bytes.

**Fix:** Clamp counts to `[0, WX_MAX_STATIONS]`, verify `getBytes` returned expected
length, store a schema tag (e.g. `sizeof(WxMetar)`) in wxcache namespace and discard on
mismatch.

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

### H4. Hand-rolled JSON scanner reads across object boundaries
`include/WeatherParse.h:11-19` — `findKey()` does `strstr` from object start to end of
entire response, not to the object's closing brace. If one METAR lacks a key, value is
silently taken from the next station's object.

**Fix:** Pass object end pointer into `findKey` and bound the search.

## MEDIUM

### M1. Shared Preferences instance races between async web and main loop
`src/ConfigurationWebServer.h:10` + `.cpp:207,265,312` — Web handlers run on AsyncTCP
task, main loop calls `GetStoredString`. Interleaved `begin()/end()` on one handle.

**Fix:** Use function-local `Preferences` in each method.

### M2. Failed TAF fetch erases cached data and persists the loss
`include/WeatherFetch.h:36-38` — On empty TAF response, `parseTAFs("[]", ...)` zeros
tafCount, then `wxCacheSave()` writes the empty state. Single network glitch wipes TAFs.

**Fix:** On empty tafJson, keep existing wx.tafs. Consider skipping wxCacheSave when
both fetches failed.

### M3. Touch hit-boxes and drawn buttons are duplicated magic numbers
`src/main.cpp:316,322` vs `include/WeatherScreens.h:116,120` — Button rects defined
separately in draw code and touch handling. Move a button, touch desyncs.

**Fix:** Named button rects in RadarLayout.h used by both.

### M4. OpenSky bounding box loses precision
`src/AircraftManager.cpp:134-137` — `String(lat - rad)` defaults to 2 decimal places
(~0.6nm error). Aircraft near display edge can be excluded.

**Fix:** `String(lat - rad, 4)`.

### M5. Flash wear from frequent NVS writes
`src/main.cpp:198-207,232,501-507` — Weather cache written every 5 min, uptime every
60s. Devices meant to run for years.

**Fix:** Save wx cache only when contents changed (memcmp). Widen uptime save interval.

### M6. TAF map dereferences past empty station strings
`include/WeatherScreens.h:256,260` — `topRow[c] + 1` strips leading 'K', but empty
string padding means `"" + 1` reads past terminator.

**Fix:** `const char* p = topRow[c]; if (*p == 'K') p++;`

### M7. METAR detail shows wrong station on fallback
`include/WeatherScreens.h:25-36` — If airportId has no METAR, falls back to metars[0]
but still draws airportId as header. User reads wrong station's weather.

**Fix:** Draw "No METAR data" or show the actual station ICAO.

### M8. Boot-time sprite creation ignores failure
`src/main.cpp:381-382` — `setup()` calls `backbuffer.createSprite()` raw, unlike every
other site which uses `recreateSprite()` with logging/reboot policy.

**Fix:** Use `recreateSprite("boot")`.

### M9. Per-frame String churn fragments heap
`src/AircraftManager.cpp:99-113,186,236-243` — `IsKnownTail` allocates 2+ temporary
Strings per aircraft per frame. Plus `String(diamNm) + "nm"` etc every loop.

**Fix:** Normalize callsign once in Update(), use stack char buffers with snprintf.

### M10. Millis wraparound at 49.7 days
`src/main.cpp:474,477` — `millis() >= configServer.restartAt` and `configActiveUntil`
use absolute comparisons instead of subtraction-based interval checks.

**Fix:** Store start time, compare `millis() - start < duration`.

## LOW

### S1. Defaults defined in three places
ApplyDefaults, main.cpp fallbacks, AircraftManager defaults. ApplyDefaults guarantees
NVS is populated, so the others are dead code that will drift.

### S2. Dead fields on Aircraft struct
originCountry, squawk, spi, positionSource, category, verticalRate, geoAltitude —
parsed and heap-allocated but never displayed. Trim to save per-aircraft heap.

### S3. Two HTTP client wrappers
HttpFetch.h vs HttpRequestManager — two idioms for the same thing.

### S4. Boot "WiFi connected!" without checking return value
main.cpp:398-408 — `wm.autoConnect()` return value ignored.

### S5. checkin URL not escaped
main.cpp:167-173 — airport/user from NVS interpolated raw into query string.

### S6. Raw-METAR line split drops a character
WeatherScreens.h:110 — when no space found, second line starts at `splitAt + 1`.
