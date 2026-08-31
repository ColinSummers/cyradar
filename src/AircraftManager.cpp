#include "AircraftManager.h"
#include "RadarLayout.h"
#include "KFHR.h"
#include "KPAE.h"
#include "KSMO.h"

#include <ArduinoJson.h>
#include <WiFi.h>

static const uint32_t GREEN_BRIGHT = lgfx::color888(0, 255, 0);
static const uint32_t GREEN_MID    = lgfx::color888(0, 128, 0);
static const uint32_t GREEN_DIM    = lgfx::color888(0, 80, 0);
static const uint32_t GREEN_VDIM   = lgfx::color888(0, 60, 0);
static const uint32_t YELLOW_BRIGHT = lgfx::color888(255, 220, 0);
static const uint32_t YELLOW_MID    = lgfx::color888(180, 160, 0);

void AircraftManager::Initialise()
{
    airportId = configServer.GetStoredString("airport");
    if (airportId.isEmpty()) airportId = "KFHR";

    lat = configServer.GetStoredString("latitude").toDouble();
    lon = configServer.GetStoredString("longitude").toDouble();
    double diameterNm = configServer.GetStoredString("diameter").toDouble();
    if (diameterNm <= 0) diameterNm = 8;
    rad = diameterNm / 120.0;
    displayRad = rad;

    float maxAltFt = configServer.GetStoredString("maxalt").toFloat();
    if (maxAltFt <= 0) maxAltFt = 8000;
    maxAltMeters = maxAltFt * 0.3048f;

    const String renderText = configServer.GetStoredString("infotext");
    if (!renderText.isEmpty()) displayInfoText = renderText == "true";

    classDRadiusNm = configServer.GetStoredString("classd").toFloat();

    runways.clear();
    String rwyJson = configServer.GetStoredString("runways");
    if (!rwyJson.isEmpty()) {
        JsonDocument doc;
        if (!deserializeJson(doc, rwyJson) && doc.is<JsonArray>()) {
            for (JsonObject rwy : doc.as<JsonArray>()) {
                RunwayInfo ri = {};
                strlcpy(ri.id, rwy["id"] | "", sizeof(ri.id));
                ri.heading1 = rwy["h1"] | 0.0f;
                ri.heading2 = rwy["h2"] | 0.0f;
                ri.lat1 = rwy["lat1"] | 0.0f;
                ri.lon1 = rwy["lon1"] | 0.0f;
                ri.lat2 = rwy["lat2"] | 0.0f;
                ri.lon2 = rwy["lon2"] | 0.0f;
                runways.push_back(ri);
            }
        }
    }

    knownTails.clear();
    String raw = configServer.GetStoredString("knowntails");
    raw.replace(',', ' ');
    raw.trim();
    int pos = 0;
    while (pos < (int)raw.length()) {
        while (pos < (int)raw.length() && raw[pos] == ' ') pos++;
        if (pos >= (int)raw.length()) break;

        int start = pos;
        while (pos < (int)raw.length() && raw[pos] != ' ' && raw[pos] != '(') pos++;

        String tail = raw.substring(start, pos);
        String displayName;

        while (pos < (int)raw.length() && raw[pos] == ' ') pos++;
        if (pos < (int)raw.length() && raw[pos] == '(') {
            int close = raw.indexOf(')', pos);
            if (close > pos + 1)
                displayName = raw.substring(pos + 1, close);
            pos = (close >= 0) ? close + 1 : (int)raw.length();
        }

        tail.trim();
        tail.toUpperCase();
        if (tail.length() > 0)
            knownTails.push_back({tail, displayName});
    }

    constexpr int MS_PER_DAY = 24 * 60 * 60 * 1000;
    constexpr int ANONYMOUS_TOKENS_PER_DAY = 400;
    constexpr int AUTHED_TOKENS_PER_DAY = 4000;
    constexpr int TOKEN_BUFFER = 3;
    int dailyRequestBudget = ANONYMOUS_TOKENS_PER_DAY - TOKEN_BUFFER;

    const String token = authHandler.GetValidToken(configServer.GetStoredString("opensky-id"), configServer.GetStoredString("opensky-secret"));
    if (!token.isEmpty())
        dailyRequestBudget = AUTHED_TOKENS_PER_DAY - TOKEN_BUFFER;

    fetchInterval = MS_PER_DAY / dailyRequestBudget;
}

String AircraftManager::IsKnownTail(const String& callsign, const String& icao) const
{
    String cs = callsign;
    cs.trim();
    cs.toUpperCase();
    String ic = icao;
    ic.toUpperCase();

    for (const auto& [tail, displayName] : knownTails) {
        if (tail.length() == 0) continue;
        if (cs.indexOf(tail) >= 0 || ic.indexOf(tail) >= 0)
            return displayName.length() > 0 ? displayName : tail;
    }
    return "";
}

void AircraftManager::Update()
{
    lastFetch = millis();

    const String token = authHandler.GetValidToken(
        configServer.GetStoredString("opensky-id"),
        configServer.GetStoredString("opensky-secret")
    );

    std::vector<std::pair<String, String>> headers = {};
    if (!token.isEmpty()) headers.push_back({ "Authorization", "Bearer " + token });

    float lonRad = rad / cos(lat * M_PI / 180.0);

    JsonDocument doc;
    if (!http.GetJson(
        "https://opensky-network.org/api/states/all",
        doc,
        {
          {"lamin", String(lat - rad, 4)},
          {"lamax", String(lat + rad, 4)},
          {"lomin", String(lon - lonRad, 4)},
          {"lomax", String(lon + lonRad, 4)}
        },
        headers
    )) return;

    unsigned long now = millis();
    auto aircraft = JsonParser::ParseArray<Aircraft>(doc["states"]);
    Serial.printf("[OPENSKY] Parsed %d aircraft (interval=%lums)\n",
                  (int)aircraft.size(), fetchInterval);

    for (auto& ac : aircraft) {
        if (ac.baroAltitude > maxAltMeters) continue;
        auto it = trackedAircraft.find(ac.icao24);
        if (it == trackedAircraft.end())
            trackedAircraft.emplace(ac.icao24, TrackedAircraft{ ac, now });
        else
            it->second.Update(ac, now);
    }

    for (auto it = trackedAircraft.begin(); it != trackedAircraft.end(); ) {
        bool keep = std::any_of(aircraft.begin(), aircraft.end(),
            [&](const Aircraft& ac) { return ac.icao24 == it->first && ac.baroAltitude <= maxAltMeters; });
        if (!keep)
            it = trackedAircraft.erase(it);
        else
            ++it;
    }
}

void AircraftManager::Draw(LGFX_Sprite& backbuffer)
{
    DrawRadarCircles(backbuffer);
    DrawCrosshairs(backbuffer);
    DrawCoastline(backbuffer);
    DrawRunway(backbuffer);
    DrawClassD(backbuffer);
    DrawNearbyRunways(backbuffer);
    DrawSidebar(backbuffer);

    for (auto& [icao, tracked] : trackedAircraft) {
        if (tracked.state.onGround) continue;

        tracked.Tick();
        auto [predLat, predLon] = tracked.GetDisplayPosition();
        auto [x, y] = ProjectCoordinateToScreen(predLat, predLon);

        if (x < 0 || x >= RADAR_SIZE || y < 0 || y >= RADAR_SIZE)
            continue;

        String knownName = IsKnownTail(tracked.state.callsign, icao);
        bool known = knownName.length() > 0;

        if (displayInfoText)
            DrawAircraftInfo(backbuffer, x, y, tracked, knownName);

        DrawAircraftTriangle(backbuffer, x, y, tracked, known);
    }
}

void AircraftManager::DrawRadarCircles(LGFX_Sprite& backbuffer) const
{
    backbuffer.drawCircle(RADAR_CENTRE, RADAR_CENTRE, RADAR_RADIUS, lgfx::color888(0, 200, 0));
    backbuffer.drawCircle(RADAR_CENTRE, RADAR_CENTRE, (RADAR_RADIUS * 2) / 3, lgfx::color888(0, 64, 0));
    backbuffer.drawCircle(RADAR_CENTRE, RADAR_CENTRE, RADAR_RADIUS / 3, lgfx::color888(0, 32, 0));
}

void AircraftManager::DrawCrosshairs(LGFX_Sprite& backbuffer) const
{
    backbuffer.drawFastHLine(0, RADAR_CENTRE, RADAR_SIZE, lgfx::color888(0, 32, 0));
    backbuffer.drawFastVLine(RADAR_CENTRE, 0, RADAR_SIZE, lgfx::color888(0, 32, 0));
}

void AircraftManager::DrawSidebar(LGFX_Sprite& backbuffer) const
{
    backbuffer.drawFastVLine(SIDEBAR_X, 0, DISPLAY_H, lgfx::color888(0, 64, 0));

    backbuffer.setTextSize(1.5);
    backbuffer.setTextColor(lgfx::color888(0, 200, 0));
    backbuffer.drawString(airportId.c_str(), SIDEBAR_X + 4, 3);

    backbuffer.setTextSize(1);
    backbuffer.setTextColor(lgfx::color888(0, 100, 0));
    int diamNm = (int)(displayRad * 120.0 + 0.5);
    backbuffer.drawString(String(diamNm) + "nm", SIDEBAR_X + 4, 18);

    int airborne = 0;
    for (auto& [icao, tracked] : trackedAircraft) {
        if (!tracked.state.onGround) airborne++;
    }
    backbuffer.drawString(String(airborne) + " ac", SIDEBAR_X + 4, 30);

    backbuffer.setTextColor(GREEN_VDIM);
    backbuffer.drawString(WiFi.localIP().toString(), SIDEBAR_X + 4, DISPLAY_H - 10);

    int yOff = 46;
    for (auto& [icao, tracked] : trackedAircraft) {
        if (tracked.state.onGround) continue;
        if (yOff > DISPLAY_H - 20) break;

        String cs = tracked.state.callsign;
        cs.trim();
        if (cs.length() == 0) cs = icao;

        String knownName = IsKnownTail(tracked.state.callsign, icao);
        bool known = knownName.length() > 0;
        backbuffer.setTextColor(known ? YELLOW_MID : GREEN_DIM);
        backbuffer.drawString(known ? knownName : cs, SIDEBAR_X + 4, yOff);
        yOff += 10;
    }
}

std::pair<int, int> AircraftManager::ProjectCoordinateToScreen(float predLat, float predLon) const
{
    const float cosLat = cosf(lat * M_PI / 180.0f);
    const float dLon = (predLon - lon) * cosLat;
    const float dLat = predLat - lat;

    const float normLon = (dLon + displayRad) / (2.0f * displayRad);
    const float normLat = (dLat + displayRad) / (2.0f * displayRad);

    const int x = static_cast<int>(normLon * RADAR_SIZE);
    const int y = static_cast<int>(RADAR_SIZE - (normLat * RADAR_SIZE));

    return { x, y };
}

void AircraftManager::DrawCoastline(LGFX_Sprite& backbuffer) const
{
    const CoastSegment* segments = nullptr;
    int segmentCount = 0;

    if (airportId == "KPAE") {
        segments = KPAE_COASTLINE;
        segmentCount = KPAE_COASTLINE_SEGMENTS;
    } else if (airportId == "KFHR") {
        segments = KFHR_COASTLINE;
        segmentCount = KFHR_COASTLINE_SEGMENTS;
    } else if (airportId == "KSMO" || airportId == "KTOA") {
        segments = KSMO_COASTLINE;
        segmentCount = KSMO_COASTLINE_SEGMENTS;
    }

    if (!segments) return;

    const uint32_t coastColor = lgfx::color888(40, 80, 120);
    for (int s = 0; s < segmentCount; s++) {
        for (int i = 0; i < segments[s].count - 1; i++) {
            auto [x1, y1] = ProjectCoordinateToScreen(segments[s].points[i].lat, segments[s].points[i].lon);
            auto [x2, y2] = ProjectCoordinateToScreen(segments[s].points[i + 1].lat, segments[s].points[i + 1].lon);
            backbuffer.drawLine(x1, y1, x2, y2, coastColor);
        }
    }
}

void AircraftManager::DrawRunway(LGFX_Sprite& backbuffer) const
{
    const uint32_t rwyColor = lgfx::color888(50, 100, 200);
    for (const auto& rwy : runways) {
        auto [x1, y1] = ProjectCoordinateToScreen(rwy.lat1, rwy.lon1);
        auto [x2, y2] = ProjectCoordinateToScreen(rwy.lat2, rwy.lon2);
        backbuffer.drawLine(x1, y1, x2, y2, rwyColor);
        backbuffer.drawLine(x1 - 1, y1, x2 - 1, y2, rwyColor);
        backbuffer.drawLine(x1 + 1, y1, x2 + 1, y2, rwyColor);
    }
}

void AircraftManager::DrawClassD(LGFX_Sprite& backbuffer) const
{
    if (classDRadiusNm <= 0) return;

    const uint32_t color = lgfx::color888(50, 100, 200);
    float pixelR = (classDRadiusNm / (displayRad * 120.0f)) * RADAR_SIZE * 0.5f;
    constexpr int DASHES = 36;
    for (int i = 0; i < DASHES; i++) {
        float a1 = i * 2.0f * M_PI / DASHES;
        float a2 = (i + 0.5f) * 2.0f * M_PI / DASHES;
        constexpr int STEPS = 4;
        for (int j = 0; j < STEPS; j++) {
            float ta = a1 + (a2 - a1) * j / STEPS;
            float tb = a1 + (a2 - a1) * (j + 1) / STEPS;
            backbuffer.drawLine(
                RADAR_CENTRE + cosf(ta) * pixelR, RADAR_CENTRE + sinf(ta) * pixelR,
                RADAR_CENTRE + cosf(tb) * pixelR, RADAR_CENTRE + sinf(tb) * pixelR,
                color);
        }
    }
}

void AircraftManager::DrawNearbyRunways(LGFX_Sprite& backbuffer) const
{
    const NearbyRunway* rwys = PNW_RUNWAYS;
    int count = PNW_RUNWAY_COUNT;
    if (airportId == "KSMO" || airportId == "KTOA") {
        rwys = LA_RUNWAYS;
        count = LA_RUNWAY_COUNT;
    }

    const uint32_t rwyColor = lgfx::color888(50, 100, 200);
    for (int i = 0; i < count; i++) {
        float midLat = (rwys[i].lat1 + rwys[i].lat2) * 0.5f;
        float midLon = (rwys[i].lon1 + rwys[i].lon2) * 0.5f;
        float dLat = midLat - (float)lat;
        float dLon = midLon - (float)lon;
        if (dLat * dLat + dLon * dLon < 0.0004f) continue;
        auto [x1, y1] = ProjectCoordinateToScreen(rwys[i].lat1, rwys[i].lon1);
        auto [x2, y2] = ProjectCoordinateToScreen(rwys[i].lat2, rwys[i].lon2);
        if ((x1 < 0 || x1 >= RADAR_SIZE) && (x2 < 0 || x2 >= RADAR_SIZE)) continue;
        if ((y1 < 0 || y1 >= RADAR_SIZE) && (y2 < 0 || y2 >= RADAR_SIZE)) continue;
        backbuffer.drawLine(x1, y1, x2, y2, rwyColor);
    }
}

void AircraftManager::DrawAircraftInfo(LGFX_Sprite& backbuffer, int x, int y, const TrackedAircraft& tracked, const String& knownName) const
{
    bool known = knownName.length() > 0;
    backbuffer.setTextSize(1.5);
    int lineHeight = (int)(backbuffer.fontHeight()) + 2;

    backbuffer.setTextColor(known ? YELLOW_MID : GREEN_MID);

    String label = knownName;
    if (label.length() == 0) {
        label = tracked.state.callsign;
        label.trim();
    }
    if (label.length() > 0)
        backbuffer.drawString(label, x + 7, y + 7);

    int altFt = (int)(tracked.state.baroAltitude * 3.28084f);
    backbuffer.drawString(String(altFt) + "'", x + 7, y + 7 + lineHeight);
    backbuffer.setTextSize(1);
}

void AircraftManager::DrawAircraftTriangle(LGFX_Sprite& backbuffer, int x, int y, const TrackedAircraft& tracked, bool known) const
{
    const float dx = std::sin(radians(tracked.state.trueTrack));
    const float dy = -std::cos(radians(tracked.state.trueTrack));
    const float px = -dy;
    const float py = dx;

    constexpr float TRIANGLE_LENGTH = 8.0f;
    constexpr float TRIANGLE_WIDTH = 4.0f;

    const float tipX = x + dx * TRIANGLE_LENGTH;
    const float tipY = y + dy * TRIANGLE_LENGTH;
    const float leftX = x - dx * TRIANGLE_LENGTH * 0.5f + px * TRIANGLE_WIDTH * 0.5f;
    const float leftY = y - dy * TRIANGLE_LENGTH * 0.5f + py * TRIANGLE_WIDTH * 0.5f;
    const float rightX = x - dx * TRIANGLE_LENGTH * 0.5f - px * TRIANGLE_WIDTH * 0.5f;
    const float rightY = y - dy * TRIANGLE_LENGTH * 0.5f - py * TRIANGLE_WIDTH * 0.5f;

    backbuffer.fillTriangle(tipX, tipY, leftX, leftY, rightX, rightY, known ? YELLOW_BRIGHT : GREEN_BRIGHT);
}

float AircraftManager::GetSweepAngle() const
{
    float t = (float)(millis() - lastFetch) / (float)fetchInterval;
    if (t > 1.0f) t = fmodf(t, 1.0f);
    return -M_PI_2 + t * 2.0f * M_PI;
}

void AircraftManager::SetDiameterNm(double nm) {
    displayRad = nm / 120.0;
}

double AircraftManager::GetDiameterNm() const {
    return displayRad * 120.0;
}
