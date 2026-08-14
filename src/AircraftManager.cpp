#include "AircraftManager.h"
#include "RadarLayout.h"
#include "Overlays.h"

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
    lat = configServer.GetStoredString("latitude").toDouble();
    lon = configServer.GetStoredString("longitude").toDouble();
    double diameterNm = configServer.GetStoredString("diameter").toDouble();
    if (diameterNm <= 0) diameterNm = 8;
    rad = diameterNm / 120.0;
    displayRad = rad;

    const String renderText = configServer.GetStoredString("infotext");
    const String renderTris = configServer.GetStoredString("triangle");
    if (!renderText.isEmpty()) displayInfoText = renderText == "true";
    if (!renderTris.isEmpty()) displayTriangles = renderTris == "true";

    knownTails.clear();
    String tails = configServer.GetStoredString("knowntails");
    tails.trim();
    while (tails.length() > 0) {
        int sep = tails.indexOf(',');
        String entry;
        if (sep < 0) {
            entry = tails;
            tails = "";
        } else {
            entry = tails.substring(0, sep);
            tails = tails.substring(sep + 1);
        }
        entry.trim();
        entry.toUpperCase();
        if (entry.length() > 0)
            knownTails.push_back(entry);
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

bool AircraftManager::IsKnownTail(const String& callsign, const String& icao) const
{
    String cs = callsign;
    cs.trim();
    cs.toUpperCase();
    String ic = icao;
    ic.toUpperCase();

    for (const auto& tail : knownTails) {
        if (tail.length() == 0) continue;
        if (cs.indexOf(tail) >= 0 || ic.indexOf(tail) >= 0)
            return true;
    }
    return false;
}

void AircraftManager::Update()
{
    unsigned long now = millis();

    if (now - lastFetch >= fetchInterval) {
        lastFetch = now;

        const String token = authHandler.GetValidToken(
            configServer.GetStoredString("opensky-id"),
            configServer.GetStoredString("opensky-secret")
        );

        std::vector<std::pair<String, String>> headers = {};
        if (!token.isEmpty()) headers.push_back({ "Authorization", "Bearer " + token });

        float lonRad = rad / cos(lat * M_PI / 180.0);

        HttpResult result = http.Get(
            "https://opensky-network.org/api/states/all",
            {
              {"lamin", String(lat - rad)},
              {"lamax", String(lat + rad)},
              {"lomin", String(lon - lonRad)},
              {"lomax", String(lon + lonRad)}
            },
            headers
        );

        if (!result.success) {
            Serial.print("[WARN] OpenSky API request failed: ");
            Serial.println(result.errorMessage);
            return;
        }

        JsonDocument doc;
        if (deserializeJson(doc, result.response)) {
            Serial.println("[WARN] Failed to parse OpenSky response");
            return;
        }
        auto aircraft = JsonParser::ParseArray<Aircraft>(doc["states"]);
        now = millis();

        for (auto& ac : aircraft) {
            if (ac.baroAltitude > MAX_ALT_METERS) continue;
            auto it = trackedAircraft.find(ac.icao24);
            if (it == trackedAircraft.end())
                trackedAircraft.emplace(ac.icao24, TrackedAircraft{ ac, now });
            else
                it->second.Update(ac, now);
        }

        for (auto it = trackedAircraft.begin(); it != trackedAircraft.end(); ) {
            bool keep = std::any_of(aircraft.begin(), aircraft.end(),
                [&](const Aircraft& ac) { return ac.icao24 == it->first && ac.baroAltitude <= MAX_ALT_METERS; });
            if (!keep)
                it = trackedAircraft.erase(it);
            else
                ++it;
        }
    }
}

void AircraftManager::Draw(LGFX_Sprite& backbuffer)
{
    DrawRadarCircles(backbuffer);
    DrawCrosshairs(backbuffer);
    DrawCoastline(backbuffer);
    DrawRunway(backbuffer);
    DrawSidebar(backbuffer);

    for (auto& [icao, tracked] : trackedAircraft) {
        if (tracked.state.onGround) continue;

        tracked.Tick();
        auto [predLat, predLon] = tracked.GetDisplayPosition();
        auto [x, y] = ProjectCoordinateToScreen(predLat, predLon);

        if (x < 0 || x >= RADAR_SIZE || y < 0 || y >= RADAR_SIZE)
            continue;

        bool known = IsKnownTail(tracked.state.callsign, icao);

        if (displayInfoText)
            DrawAircraftInfo(backbuffer, x, y, tracked, known);

        if (displayTriangles)
            DrawAircraftTriangle(backbuffer, x, y, tracked, known);
        else
            backbuffer.fillCircle(x, y, 3, known ? YELLOW_BRIGHT : GREEN_BRIGHT);
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
    backbuffer.drawString("KFHR", SIDEBAR_X + 4, 3);

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

        bool known = IsKnownTail(tracked.state.callsign, icao);
        backbuffer.setTextColor(known ? YELLOW_MID : GREEN_DIM);
        backbuffer.drawString(cs, SIDEBAR_X + 4, yOff);
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
    const uint32_t coastColor = lgfx::color888(40, 80, 120);
    for (int i = 0; i < SAN_JUAN_COASTLINE_COUNT - 1; i++) {
        auto [x1, y1] = ProjectCoordinateToScreen(SAN_JUAN_COASTLINE[i].lat, SAN_JUAN_COASTLINE[i].lon);
        auto [x2, y2] = ProjectCoordinateToScreen(SAN_JUAN_COASTLINE[i + 1].lat, SAN_JUAN_COASTLINE[i + 1].lon);
        backbuffer.drawLine(x1, y1, x2, y2, coastColor);
    }
}

void AircraftManager::DrawRunway(LGFX_Sprite& backbuffer) const
{
    const uint32_t rwyColor = lgfx::color888(50, 100, 200);
    auto [x1, y1] = ProjectCoordinateToScreen(RUNWAY_NORTH.lat, RUNWAY_NORTH.lon);
    auto [x2, y2] = ProjectCoordinateToScreen(RUNWAY_SOUTH.lat, RUNWAY_SOUTH.lon);
    backbuffer.drawLine(x1, y1, x2, y2, rwyColor);
    backbuffer.drawLine(x1 - 1, y1, x2 - 1, y2, rwyColor);
    backbuffer.drawLine(x1 + 1, y1, x2 + 1, y2, rwyColor);
}

void AircraftManager::DrawAircraftInfo(LGFX_Sprite& backbuffer, int x, int y, const TrackedAircraft& tracked, bool known) const
{
    backbuffer.setTextSize(1.5);
    int lineHeight = (int)(backbuffer.fontHeight()) + 2;

    backbuffer.setTextColor(known ? YELLOW_MID : GREEN_MID);

    String cs = tracked.state.callsign;
    cs.trim();
    if (cs.length() > 0)
        backbuffer.drawString(cs, x + 7, y + 7);

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
