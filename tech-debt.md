# Tech Debt

## H2. Secrets baked into publicly downloadable binary

`include/Config.h` — WiFi SSID/password and OpenSky client secret are compiled into
firmware published unauthenticated at george. `strings firmware.bin` exposes them.

**Fix:** Ship release builds with empty `WIFI_SSID`/`WIFI_PASS` (WiFiManager portal
covers provisioning). Leave OpenSky creds to per-device NVS config via web page. This
is a release-process change — need a `Config.h.release` template or build flag.

## H3. OTA updates are unauthenticated (MITM = code execution)

`src/main.cpp` — Both the version check and `httpUpdate.update()` use
`WiFiClientSecure::setInsecure()`. A MITM on the network can serve arbitrary firmware.

**Fix:** Pin george.pogsummers.com's CA cert (`setCACert`) for the OTA path only. The
sprite is already freed during OTA checks, so heap is available for the TLS handshake
with full cert validation. Need to extract the CA cert from Fly.io's certificate chain.

## M9. Per-frame String churn fragments heap

`src/AircraftManager.cpp` — `IsKnownTail()` allocates 2+ temporary `String`s per
aircraft per frame (called from both `Draw` and `DrawSidebar`). Plus
`String(diamNm) + "nm"`, `WiFi.localIP().toString()` etc every loop iteration.

**Fix:** Compute known-tail name once per aircraft in `Update()` and store it on
`TrackedAircraft`. Replace sidebar String concatenations with stack `char` buffers
and `snprintf`. This is a moderate refactor touching AircraftManager's data model.
