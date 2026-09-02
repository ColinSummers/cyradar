# CYRadar Flight Tracker — Implementation Plan

Track a specific tail number: get notified when it departs, then watch its live
track scroll across the CYD with a HUD of altitude, speed, heading, time
airborne, and ETA.

**Reality check on the stack:** george is the existing **Express (Node.js)
backend on Fly.io** with SQLite (`george.pogsummers.com`), already serving
`/cyradar/checkin` and OTA firmware. The tracker server work goes into george
as new Express routes, not a separate Flask service. FlightAware's official
Flask alerts sample lives at
`reference/flightaware/alerts_backend/app.py` and is the reference for the
alert-config and webhook table shapes — port its schema, don't run it.

---

## 1. Architecture Overview

```
                         (a) POST /alerts, PUT /alerts/endpoint   (setup, rare)
                         (b) GET /flights/{fa_flight_id}/position (60s while active)
                         (c) GET /flights/{fa_flight_id}          (10 min, for ETA)
   ┌──────────────┐ ◄─────────────────────────────────────────────┐
   │  FlightAware │                                               │
   │   AeroAPI    │      webhook POST on departure/arrival        │
   │              │ ─────────────────────────────────────────────►│
   └──────────────┘                                        ┌──────┴──────┐
                                                           │   george    │
                                                           │  (Express,  │
                                                           │   Fly.io,   │
                                                           │   SQLite)   │
                                                           └──────┬──────┘
                                          CYD polls (HTTPS GET):  │
                          /cyradar/tracker/status  (piggybacked)  │
                          /cyradar/tracker/track?since=N  (30s)   │
                                                           ┌──────┴──────┐
                                                           │  CYD / CCYD │
                                                           │ Freenove S3 │
                                                           └─────────────┘
```

Three principles:

1. **george is the only thing that talks to AeroAPI.** The API key never
   ships in firmware; costs are centralized; multiple boards can watch the
   same flight for the price of one poller.
2. **Push for "is it flying," poll for "where is it."** The AeroAPI alerts
   webhook eliminates all speculative polling. george does zero AeroAPI
   traffic until FlightAware POSTs a departure event.
3. **Nobody watching → nobody paying.** george only polls
   `/flights/{id}/position` while at least one CYD has requested track data
   in the last 5 minutes. If the board is off, george lets the flight go
   untracked (it can backfill the whole trail later with one
   `/flights/{id}/track` call).

The track itself is *accumulated on george* from the periodic `/position`
responses — one point per poll — rather than re-fetching the full `/track`
repeatedly. `/track` is called at most twice per flight: once when tracking
starts mid-flight (webhook raced the poller, or the CYD woke up late) and
optionally once at arrival to store a clean final trail.

---

## 2. API Call Sequence

### One-time setup (manual, or a george admin action)
1. `PUT /alerts/endpoint` with
   `{"url": "https://george.pogsummers.com/cyradar/aeroapi-webhook"}`.
   This is account-wide — one endpoint for everything.
2. Verify with `GET /alerts/endpoint`.

### Arming a tail number (user enters N12345)
1. User enters the tail number on the CYD's existing AsyncWebServer config
   page (new field, stored in NVS alongside `known tails`), or on george's
   admin CYRadar tab.
2. CYD reports it on next check-in (`&track=N12345` param) or immediately via
   `GET /cyradar/tracker/arm?tail=N12345&user=<device>`.
3. george → `POST /alerts` with:
   ```json
   {
     "ident": "N12345",
     "events": {"departure": true, "arrival": true,
                "cancelled": true, "diverted": true, "filed": true},
     "max_weekly": 1000
   }
   ```
   Response body is empty; the alert id comes back in the `Location` header
   (per the reference app's `create_alert`). Store `fa_alert_id` ↔ tail ↔
   device in SQLite.
4. Disarming (user clears the field) → `DELETE /alerts/{fa_alert_id}`.

### Tracking lifecycle
1. **`filed` webhook** (optional pre-departure): george stores origin,
   destination, `scheduled_off`. CYD can show "N12345 filed KSMO→KTOA, off
   at 14:30" as a pending banner.
2. **`departure` webhook**: payload carries `event_code`,
   `flight.fa_flight_id`, `ident`, `registration`, `origin`, `destination`.
   george creates a `tracked_flights` row, state=`active`.
3. **CYD notices** on its next poll (see §4 — the check-in response gains a
   `tracker` field). CYD switches to `MODE_TRACKER` and starts polling
   `/cyradar/tracker/track` every 30 s.
4. **george poll loop** (only while a CYD is watching):
   - `GET /flights/{fa_flight_id}/position` every 60 s → append one point.
   - `GET /flights/{fa_flight_id}` every 10 min → refresh
     `estimated_on` / `estimated_in` for ETA/ETE, and destination if it
     appears late (VFR flights often have none).
   - If the first position poll happens >3 min after departure:
     `GET /flights/{fa_flight_id}/track` once to backfill the trail.
   - Optionally `GET /flights/{fa_flight_id}/route` once if a filed route
     exists (draw as dim dashed line — nice-to-have, phase 2).
5. **`arrival` webhook**: george marks the flight `landed`, stops polling,
   records `actual_on`. CYD shows a landed summary (total time, final
   track) for 10 minutes, then returns to `MODE_RADAR`. The alert stays
   armed for the next flight.
6. **`cancelled` / `diverted` webhooks**: update state; diverted updates the
   destination and keeps tracking.

Failure handling: if george's poll gets a 404/timeout, back off (60→120→300 s)
but keep the flight active until the arrival webhook or 30 min of no
position updates (then mark `stale`, stop polling, one final `/track` to
close it out).

---

## 3. Data Flow

```
FlightAware                    george                          CYD
    │                            │                              │
    │  departure webhook POST    │                              │
    ├───────────────────────────►│  tracked_flights: active     │
    │                            │                              │
    │                            │◄── GET /cyradar/checkin ─────┤ (existing 5-min cadence,
    │                            │    resp: {"tracker":"active"}│  or faster idle poll)
    │                            │                              │
    │                            │◄── GET /tracker/track?since=0┤ MODE_TRACKER entered
    │◄── GET /position (60s) ────┤                              │
    │    {lat,lon,alt,gs,hdg} ──►│  append to track_points      │
    │                            │── binary: hdr + pts 0..N ───►│ draws trail
    │                            │◄── GET /tracker/track?since=N┤ (every 30s)
    │                            │── hdr + pts N+1..M (delta) ─►│
    │  arrival webhook POST      │                              │
    ├───────────────────────────►│  state: landed               │
    │                            │── {"tracker":"landed",...} ─►│ summary screen
```

The `since` parameter is a monotonically increasing point sequence number.
CYD keeps the highest it has seen; george returns only newer points. A
reboot (CYD reports `since=0`) gets the full trail back — george is the
source of truth, the CYD is a dumb renderer.

---

## 4. George Server Additions (Express)

New SQLite tables (mirror the reference Flask app's schema where sensible):

```sql
-- armed alerts
CREATE TABLE tracker_alerts (
  fa_alert_id INTEGER PRIMARY KEY,
  ident TEXT, device TEXT, created_at TEXT
);
-- one row per flight instance
CREATE TABLE tracked_flights (
  fa_flight_id TEXT PRIMARY KEY,
  ident TEXT, registration TEXT,
  origin TEXT, destination TEXT,        -- ICAO, may be NULL for VFR
  state TEXT,                           -- filed|active|landed|cancelled|stale
  actual_off INTEGER, estimated_on INTEGER, actual_on INTEGER,
  last_watched INTEGER,                 -- last CYD poll, gates AeroAPI polling
  last_polled INTEGER
);
CREATE TABLE track_points (
  fa_flight_id TEXT, seq INTEGER,
  ts INTEGER, lat REAL, lon REAL,
  alt_ft INTEGER, gs_kt INTEGER, hdg INTEGER,
  PRIMARY KEY (fa_flight_id, seq)
);
CREATE TABLE tracker_events (           -- raw webhook log, like aeroapi_alerts
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  received_at TEXT, event_code TEXT, fa_flight_id TEXT, payload TEXT
);
```

New routes:

| Route | Who | Purpose |
|---|---|---|
| `POST /cyradar/aeroapi-webhook` | FlightAware | Receive alert events. Log raw payload, dispatch on `event_code`. Return 200 fast (FlightAware retries on non-2xx). |
| `GET /cyradar/tracker/arm?tail=&user=` | CYD/admin | Create AeroAPI alert, store mapping. Idempotent per tail. |
| `GET /cyradar/tracker/disarm?tail=` | CYD/admin | `DELETE /alerts/{id}`, remove row. |
| `GET /cyradar/tracker/status?user=` | CYD | Tiny JSON: `{state, ident, origin, dest, off, eta, seq}` — also embedded in the existing check-in response so idle boards learn about departures without a new request. |
| `GET /cyradar/tracker/track?user=&since=N` | CYD | Binary payload (below). Updates `last_watched`. |
| `GET /cyradar/tracker/geo?lat0=&lon0=&lat1=&lon1=` | CYD | Viewport-clipped coastline/boundary polylines (see §7). |

Poller: a `setInterval` loop (george already runs long-lived on Fly.io).
Every tick, for each `active` flight where `now - last_watched < 300s`,
fire the AeroAPI position fetch. Skip and note `paused` otherwise.

Webhook security: FlightAware doesn't sign webhooks; put a secret path
segment or query token in the endpoint URL registered via
`PUT /alerts/endpoint` (e.g. `/cyradar/aeroapi-webhook/<random128bit>`),
and validate the payload has a plausible `fa_flight_id` for an armed ident
before acting on it.

Admin: extend the existing colin-only CYRadar tab — armed tails, current
flight state, point count, AeroAPI calls this month.

### Binary track payload

CYD heap is precious and ArduinoJson parsing of hundreds of points is
wasteful. Serve `application/octet-stream`, little-endian:

```
Header (40 bytes):
  magic     u32   'TRK1'
  state     u8    0=none 1=filed 2=active 3=landed 4=cancelled 5=stale
  flags     u8    bit0: dest known, bit1: this is a delta (since>0 honored)
  ident     char[8]  padded NUL
  origin    char[4]  ICAO
  dest      char[4]  ICAO or zeros
  off_ts    u32   actual_off (unix)
  eta_ts    u32   estimated_on (0 if unknown)
  first_seq u32   seq of first point in this payload
  count     u16   number of points following
  reserved  u16

Point (16 bytes each):
  lat   i32  degrees * 1e5
  lon   i32  degrees * 1e5
  ts    u32  unix
  alt   u16  feet / 10
  gs    u8   knots / 2       (0..510 kt)
  hdg   u8   degrees / 2     (0..358)
```

A 4-hour flight at one point/min = 240 points = 3.8 KB + header. george
additionally decimates server-side: never return more than 300 points —
if the trail is longer, thin the older two-thirds (keep every 2nd/4th
point). The CYD never needs more resolution than ~1.5 px between points.

---

## 5. CYD Firmware Additions

New files, following existing conventions:

- `include/TrackerData.h` — structs + fixed buffers
- `include/TrackerFetch.h` — poll george, parse binary payload
- `include/TrackerScreen.h` — rendering (pattern of `WeatherScreens.h`)

### Data structures (static allocation, no heap churn)

```cpp
struct TrackPoint {          // 12 bytes in RAM
    int32_t lat, lon;        // deg * 1e5
    uint16_t altDaft;        // feet/10
    uint8_t  gsHalf;         // kt/2
    uint8_t  hdgHalf;        // deg/2
};

struct TrackerState {
    char     ident[9], origin[5], dest[5];
    uint8_t  state;              // mirrors payload
    uint32_t offTs, etaTs;
    uint32_t lastSeq;            // for ?since=
    uint16_t count;
    TrackPoint points[300];      // 3.6 KB static — fine at 200 KB heap
};
```

One global `TrackerState` (3.7 KB) lives in static/BSS, costing nothing at
runtime beyond its footprint. No dynamic allocation during a flight.

### Mode integration

- Extend `DisplayMode` in `src/main.cpp`
  (currently `MODE_RADAR, MODE_METAR_DETAIL, MODE_WEATHER_MAP, MODE_TAF_MAP`)
  with `MODE_TRACKER`.
- Entry: when the check-in/status response reports `tracker: active`, flash a
  banner on the radar screen ("N12345 DEPARTED KSMO"); auto-switch to
  `MODE_TRACKER` after 5 s (touch cancels). Also reachable by tap if a
  flight is filed/active.
- Exit: tap returns to `MODE_RADAR` (tracker keeps polling in background at a
  slower 60 s so re-entry is instant); on `landed`, show summary 10 min then
  auto-return.

### Fetch loop

Follow the existing TLS discipline exactly: `deleteSprite("tracker")` →
HTTPS GET → parse into `TrackerState` → `recreateSprite("tracker")` (the
~77 KB LGFX_Sprite must be gone during TLS, per the weather/check-in/OTA
pattern in `main.cpp`). Poll every 30 s in `MODE_TRACKER`, 60 s otherwise
while a flight is active. Binary parse is a `memcpy`-and-validate: check
magic, bounds-check `count`, append or replace points by `first_seq`.

While no flight is active, the tracker adds **zero** network traffic — the
status rides along in the existing check-in response.

### Rendering

Reuse the projection approach from
`AircraftManager::ProjectCoordinateToScreen` (equirectangular with
`cos(lat)` longitude scaling) but parameterized by a viewport struct
instead of the fixed radar center/zoom:

```cpp
struct Viewport { float ctrLat, ctrLon, spanNm; };  // spanNm = width of screen
```

Per frame (drawn into the existing full-screen sprite):
1. Fill dark background (match radar's palette).
2. Geo lines (dim gray, §7).
3. Origin airport: small circle + ICAO label.
4. Track polyline: bright green/amber, drawn oldest→newest; last 10 points
   slightly brighter to imply motion.
5. Aircraft: the existing directional triangle, rotated by `hdg`.
6. HUD (§6).

All three boards (CYD/ILI9341, CCYD/ST7789, Freenove S3) share this code —
it's sprite-space rendering, board differences stay confined to `LGFX_*.h`.
Build and deploy all three, per the standing rule.

### Sim first

Build `MODE_TRACKER` in the desktop sim (`sim/`, LGFX_SDL + libcurl in
`HttpFetch.h`) against canned binary payloads before touching hardware —
the zoom tuning in §7 needs fast iteration.

---

## 6. Display Mockup (320×240)

Full-screen map (unlike radar's 240+80 split — the track needs width),
with HUD strips top and bottom:

```
┌────────────────────────────────────────────┐
│ N12345  KSMO→KTOA        ETA 15:42  ETE 0:18│  top bar, 14 px, inverse
├────────────────────────────────────────────┤
│                                            │
│      ~~~~ coastline (dim gray) ~~~~        │
│   KSMO ○                                   │
│         `·..                               │
│             `·..___                        │
│                    `··─➤  (bright track,   │  map area, 212 px tall
│                            plane triangle) │
│                                            │
│                          + KTOA (if known) │
│                                            │
├────────────────────────────────────────────┤
│ 4,500ft  118kt  H214°   UP 0:23    [20nm]  │  bottom bar, 14 px
└────────────────────────────────────────────┘
```

- **Top bar:** ident, origin→dest (or "KSMO→ ---" VFR), ETA local time and
  ETE if `eta_ts` known, else time-airborne moves up here.
- **Bottom bar:** altitude (from `altDaft*10`, comma-formatted), groundspeed,
  heading, time airborne (mm:ss under 1 h, h:mm after), and current viewport
  span in the corner so the zoom level is legible.
- **Landed summary:** same screen, track frozen, top bar becomes
  "LANDED KTOA 15:44 — 1:21 airborne".
- Colors: background black; track bright amber (`0xFD20`-ish) or the radar
  green; plane white triangle; geo features dark gray (~`0x39E7`); HUD text
  white on dark bars. Matches the existing dark radar aesthetic.

---

## 7. Geographic Features, Viewport, Zoom

### The zoom problem (the part that makes or breaks the feature)

Fixed constraints: 320 px wide map. At a viewport span of S nm, one pixel =
S/320 nm. For motion to be *visible*, the plane should move ≥ ~2 px between
30 s display updates. At 120 kt, the plane covers 1 nm per 30 s → need
S ≤ 160 nm for 2 px/update. At S = 40 nm it moves 8 px/update — clearly
alive.

**Two-phase auto-zoom:**

1. **Context phase (early flight):** viewport fits origin + current position
   with 20% margin, minimum span 10 nm. As the plane flies away, this
   naturally zooms *out*.
2. **Chase phase:** once the fitted span would exceed **60 nm**, switch to a
   fixed-span chase camera: span clamped to 60 nm, centered ahead of the
   aircraft (center = plane position advanced 30% of span along heading), so
   ~2/3 of the screen shows where it's going. Origin scrolls off; the trail
   tail is truncated at the viewport edge. At 60 nm span even a 90 kt
   Cessna moves ~4 px per 30 s update — visible motion, which is the point.
3. **Arrival phase:** when distance-to-destination < 25 nm (dest known),
   re-fit to plane + destination, tightening as it closes, min span 10 nm.

Camera moves are eased (lerp viewport center/span 25% per frame toward
target) so zoom changes glide instead of snapping. A tap on the map can
cycle span overrides: AUTO → 30 → 60 → 120 → fit-all → AUTO.

### Geo line data

The baked-in per-airport headers (`KFHR.h`, ~80-point constexpr arrays)
don't generalize — a tracked flight can go anywhere. Two-tier approach:

1. **Flash fallback (always available):** one new generated header,
   `include/GeoUS.h`, holding a coarse continental outline — Natural Earth
   1:10m coastline plus state boundaries, clipped to a generous region
   (western US initially — boards live at KFHR/KPAE/KSMO), simplified with
   the same RDP pipeline used for `KFHR.h` to ~2–4 k points. At 4 bytes/pt
   quantized (`int16` lat/lon in 0.01° units) that's 8–16 KB of flash —
   negligible next to the firmware. Good enough for the 60 nm chase view to
   show a coastline sliding by.
2. **Server-clipped detail (phase 2):** `GET /cyradar/tracker/geo?bbox=…`
   returns polylines from full-resolution Natural Earth / OSM data, clipped
   and decimated to ≤ 500 points for the current viewport. CYD refreshes it
   only when the viewport center moves > 25% of span (a few KB every couple
   of minutes; cached in a static 8 KB buffer). If the fetch fails, tier 1
   still renders.

Ship tier 1 first; it may honestly be enough at these spans.

Rendering: iterate segments, project endpoints via the viewport transform,
skip segments fully outside screen bounds (cheap bbox reject), draw 1 px
dim lines. Identical pattern to `DrawCoastline()` in `AircraftManager.cpp`.

---

## 8. Cost Estimation

AeroAPI bills per query with per-endpoint pricing tiers; alert deliveries
are also billed. **Exact per-call prices below are placeholders from the
published tier structure — verify against the current AeroAPI pricing page
before setting the polling cadence** (personal plans include a monthly
usage credit, historically $5).

Per tracked flight, with the §2 design (60 s position polls, 10 min info
refresh, george pauses when no CYD is watching):

| Item | 1 h flight | 2 h | 4 h |
|---|---|---|---|
| Alert deliveries (filed/departure/arrival) | 3 | 3 | 3 |
| `/flights/{id}/position` @60 s | 60 | 120 | 240 |
| `/flights/{id}` (ETA refresh @10 min) | 6 | 12 | 24 |
| `/flights/{id}/track` (backfill) | ≤1 | ≤1 | ≤1 |
| **Total queries** | **~70** | **~136** | **~268** |

At an illustrative $0.01/query that's roughly **$0.70 / $1.35 / $2.70 per
flight**. Arming an alert (`POST /alerts`) is a one-time single call per
tail. Idle cost is zero — no polling between flights.

Levers if that's too rich:
- Position poll at 90–120 s (still 2–5 px motion per update at chase zoom;
  the CYD can dead-reckon between server points using gs/hdg, exactly like
  the radar's existing prediction interpolation).
- **OpenSky blending (big one):** the project already has authenticated
  OpenSky access, which is free. george could poll OpenSky's
  `/states/all?icao24=` for positions and use AeroAPI only for
  alerts + flight info + ETA. Cost per flight drops to ~$0.10 (alerts +
  a handful of info calls). Tradeoff: OpenSky coverage gaps and no
  server-side track backfill. Recommended as phase 2 once the AeroAPI
  path works.
- Skip the 10-min info refresh when destination is unknown (VFR) — ETE
  can't be computed anyway.

---

## 9. Open Questions and Decisions Needed

1. **AeroAPI plan tier and real prices.** Which plan is the account on, what
   do `/position`, `/track`, `/flights/{ident}` and alert deliveries
   actually cost, and what's the alert-count cap? This sets the default
   polling cadence. Verify before implementation.
2. **OpenSky as the position source** (§8): ADS-B coverage for the specific
   aircraft/routes matters — a NorCal mountain flight may drop out. Decide
   whether AeroAPI-only v1 → OpenSky-blend v2, or blend from the start.
3. **Multi-tail / multi-device.** One tail per device? The check-in already
   carries `user=`; the schema above supports N tails, but the CYD UX
   (which flight shows when two are airborne?) needs a decision. Proposal:
   v1 = one armed tail per device, most-recent-departure wins.
4. **Tail entry UX.** Config web page field (easy, already exists) vs.
   on-screen touch keyboard (painful on XPT2046). Proposal: web page only
   for v1; george admin tab as the second path.
5. **Missed webhooks.** If Fly.io restarts george during the POST,
   FlightAware retries, but how many times and for how long? If a departure
   is missed entirely, is a manual "check now" button on the admin tab
   (one `GET /flights/{ident}` call) sufficient recovery? Proposal: yes.
6. **Webhook auth.** Confirm AeroAPI has no payload signing (v4.30 docs);
   if so, the secret-URL approach in §4 is the mitigation. Check whether
   FlightAware publishes source IP ranges worth allowlisting.
7. **Geo coverage region.** Western-US-only `GeoUS.h` keeps flash small but
   breaks if a tracked plane flies to Texas. Full CONUS at 1:10m coarse
   simplification is maybe 40–60 KB flash — probably still fine. Decide
   after measuring current firmware headroom against the 4 MB partition.
8. **Track persistence on george.** Keep `track_points` rows forever
   (flight-log feature later: "show me last Tuesday's flight") or prune
   after 30 days? SQLite on Fly.io volume is cheap; proposal: keep, prune
   at 10 k flights.
9. **Filed-but-not-departed UX.** Show a pending banner on the radar screen,
   or stay silent until departure? Banner adds anticipation; proposal: yes,
   small one-line banner.
10. **ESE/ETA source of truth.** `estimated_on` from `/flights/{id}` vs.
    computing ETE locally from distance-to-dest / groundspeed. Proposal:
    show FlightAware's when present, else compute great-circle ETE locally
    when dest is known — it's two lines of math the firmware already has
    the pieces for.

---

## Build order

1. george: webhook route + tables + arm/disarm + status in check-in
   response (deployable and testable with FlightAware's alert test POST
   before any firmware work).
2. george: poller + binary track endpoint; test with `curl | xxd` and a
   real flight on a cheap tail.
3. Sim: `MODE_TRACKER` rendering + zoom tuning against canned payloads.
4. Firmware: fetch loop + mode wiring; `GeoUS.h` generation script in
   `geo/` next to the existing geojson tooling.
5. All three boards built + deployed to `kfhr-radar/{cyd,ccyd,s3}/`, per
   standing deploy rule.
6. Phase 2: server-clipped geo detail, OpenSky blending, filed-route
   overlay from `/flights/{id}/route`.
