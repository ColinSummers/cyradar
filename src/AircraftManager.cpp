#include "AircraftManager.h"

#include <ArduinoJson.h>
#include <WiFi.h>

constexpr int DISPLAY_W = 320;
constexpr int DISPLAY_H = 240;
constexpr int RADAR_SIZE = 240;
constexpr int RADAR_CENTRE = RADAR_SIZE / 2 - 1;
constexpr int RADAR_RADIUS = RADAR_SIZE / 2 - 1;
constexpr int SIDEBAR_X = RADAR_SIZE;
constexpr int SIDEBAR_W = DISPLAY_W - RADAR_SIZE;

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
    rad = configServer.GetStoredString("radius").toDouble();

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

        HttpResult result = http.Get(
            "https://opensky-network.org/api/states/all",
            {
              {"lamin", String(lat - rad)},
              {"lamax", String(lat + rad)},
              {"lomin", String(lon - rad)},
              {"lomax", String(lon + rad)}
            },
            headers
        );

        if (!result.success) {
            Serial.print("[WARN] OpenSky API request failed: ");
            Serial.println(result.errorMessage);
            return;
        }

        JsonDocument doc;
        deserializeJson(doc, result.response);
        auto aircraft = JsonParser::ParseArray<Aircraft>(doc["states"]);
        now = millis();

        for (auto& ac : aircraft) {
            auto it = trackedAircraft.find(ac.icao24);
            if (it == trackedAircraft.end())
                trackedAircraft.emplace(ac.icao24, TrackedAircraft{ ac, now });
            else
                it->second.Update(ac, now);
        }

        for (auto it = trackedAircraft.begin(); it != trackedAircraft.end(); ) {
            bool aircraftPresent = std::any_of(aircraft.begin(), aircraft.end(), [&](const Aircraft& ac) { return ac.icao24 == it->first; });
            if (!aircraftPresent)
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

    backbuffer.setTextSize(1);
    backbuffer.setTextColor(lgfx::color888(0, 200, 0));
    backbuffer.drawString("KFHR", SIDEBAR_X + 4, 4);

    backbuffer.setTextColor(lgfx::color888(0, 100, 0));
    backbuffer.drawString(String(rad, 1) + "d", SIDEBAR_X + 4, 16);

    int airborne = 0;
    for (auto& [icao, tracked] : trackedAircraft) {
        if (!tracked.state.onGround) airborne++;
    }
    backbuffer.drawString(String(airborne) + " ac", SIDEBAR_X + 4, 28);

    backbuffer.setTextColor(GREEN_VDIM);
    backbuffer.drawString(WiFi.localIP().toString(), SIDEBAR_X + 4, DISPLAY_H - 10);

    int yOff = 44;
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
    const float dLon = predLon - lon;
    const float dLat = predLat - lat;

    const float normLon = (dLon + rad) / (2.0f * rad);
    const float normLat = (dLat + rad) / (2.0f * rad);

    const int x = static_cast<int>(normLon * RADAR_SIZE);
    const int y = static_cast<int>(RADAR_SIZE - (normLat * RADAR_SIZE));

    return { x, y };
}

void AircraftManager::DrawAircraftInfo(LGFX_Sprite& backbuffer, int x, int y, const TrackedAircraft& tracked, bool known) const
{
    const int lineHeight = tft.fontHeight() + 1;

    backbuffer.setTextSize(1);
    backbuffer.setTextColor(known ? YELLOW_MID : GREEN_MID);

    String cs = tracked.state.callsign;
    cs.trim();
    if (cs.length() > 0)
        backbuffer.drawString(cs, x + 5, y + 5);

    int altFt = (int)(tracked.state.baroAltitude * 3.28084f);
    backbuffer.drawString(String(altFt) + "'", x + 5, y + 5 + lineHeight);
}

void AircraftManager::DrawAircraftTriangle(LGFX_Sprite& backbuffer, int x, int y, const TrackedAircraft& tracked, bool known) const
{
    const float dx = std::sin(radians(tracked.state.trueTrack));
    const float dy = -std::cos(radians(tracked.state.trueTrack));
    const float px = -dy;
    const float py = dx;

    constexpr float TRIANGLE_LENGTH = 6.0f;
    constexpr float TRIANGLE_WIDTH = 3.0f;

    const float tipX = x + dx * TRIANGLE_LENGTH;
    const float tipY = y + dy * TRIANGLE_LENGTH;
    const float leftX = x - dx * TRIANGLE_LENGTH * 0.5f + px * TRIANGLE_WIDTH * 0.5f;
    const float leftY = y - dy * TRIANGLE_LENGTH * 0.5f + py * TRIANGLE_WIDTH * 0.5f;
    const float rightX = x - dx * TRIANGLE_LENGTH * 0.5f - px * TRIANGLE_WIDTH * 0.5f;
    const float rightY = y - dy * TRIANGLE_LENGTH * 0.5f - py * TRIANGLE_WIDTH * 0.5f;

    backbuffer.fillTriangle(tipX, tipY, leftX, leftY, rightX, rightY, known ? YELLOW_BRIGHT : GREEN_BRIGHT);
}
