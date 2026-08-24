#include <SDL.h>

#include "Arduino.h"

#include <LovyanGFX.hpp>
#include <lgfx/v1/platforms/sdl/Panel_sdl.hpp>
#include <lgfx/v1/platforms/sdl/common.hpp>

#include <cstring>
#include <ctime>
#include <string>
#include <vector>
#include <map>
#include <fstream>
#include <sstream>

class LGFX : public lgfx::LGFX_Device {
    lgfx::Panel_sdl _panel;
public:
    LGFX() {
        auto cfg = _panel.config();
        cfg.memory_width = 320;
        cfg.panel_width = 320;
        cfg.memory_height = 240;
        cfg.panel_height = 240;
        _panel.config(cfg);
        _panel.setScaling(1, 1);
        _panel.setWindowTitle("CYRadar Simulator");
        setPanel(&_panel);
    }
};

#include "DrawHelpers.h"
#include "models/Aircraft.h"
#include "models/TrackedAircraft.h"
#include "Overlays.h"
#include "RadarLayout.h"
#include "WeatherData.h"
#include "WeatherFetch.h"
#include "WeatherScreens.h"

static constexpr float MAX_ALT_METERS = 2438.4f;

// ---- Airport configs ----
struct SimAirportConfig {
    const char* id;
    double lat;
    double lon;
    RunwayInfo runways[4];
    int numRunways;
    float classDNm;
    const char* metarStations;
    const char* tafStations;
    const char* tafTop[4];
    const char* tafBot[4];
};

#include "KFHR.h"
#include "KPAE.h"

static const SimAirportConfig AIRPORT_KFHR = {
    "KFHR", 48.5220, -123.0244,
    {{"16/34", 177.0f, 357.0f, 48.5266f, -123.0250f, 48.5173f, -123.0240f}},
    1, 0,
    "KFHR KNUW KPAE KBFI KBVS KBLI KORS KCLM",
    "KFHR KNUW KPAE KBFI KBVS KBLI KORS KCLM",
    {"KFHR", "KNUW", "KPAE", "KBFI"},
    {"KBVS", "KBLI", "KORS", "KCLM"},
};

static const SimAirportConfig AIRPORT_KPAE = {
    "KPAE", 47.9063, -122.2816,
    {
        {"16R/34L", 175.0f, 355.0f, 47.9118f, -122.2826f, 47.8998f, -122.2810f},
        {"16L/34R", 175.0f, 355.0f, 47.9108f, -122.2870f, 47.9000f, -122.2854f},
    },
    2, 4.0f,
    "KPAE KBFI KSEA KRNT KNUW KBVS KORS",
    "KPAE KBFI KSEA KRNT KNUW KBVS KORS KCLM",
    {"KPAE", "KBFI", "KSEA", "KRNT"},
    {"KNUW", "KBVS", "KORS", "KCLM"},
};

static const SimAirportConfig* simAirport = &AIRPORT_KFHR;
static double sim_diameter_nm = 8;
static double sim_rad = sim_diameter_nm / 120.0;
static std::vector<String> knownTails = { "N80117", "N2939J", "N9766Z", "N87KA" };
static std::map<String, TrackedAircraft> trackedAircraft;

static const uint32_t GREEN_BRIGHT  = lgfx::color888(0, 255, 0);
static const uint32_t GREEN_MID     = lgfx::color888(0, 128, 0);
static const uint32_t GREEN_DIM     = lgfx::color888(0, 80, 0);
static const uint32_t GREEN_VDIM    = lgfx::color888(0, 60, 0);
static const uint32_t YELLOW_BRIGHT = lgfx::color888(255, 220, 0);
static const uint32_t YELLOW_MID    = lgfx::color888(180, 160, 0);

enum DisplayMode { MODE_RADAR, MODE_METAR_DETAIL, MODE_WEATHER_MAP, MODE_TAF_MAP };
static DisplayMode displayMode = MODE_RADAR;
static unsigned long modeStartTime = 0;

static WxData wxData;

// ---- Aircraft helpers ----
bool isKnownTail(const String& callsign, const String& icao) {
    String cs = callsign; cs.trim(); cs.toUpperCase();
    String ic = icao; ic.toUpperCase();
    for (const auto& tail : knownTails) {
        if (tail.length() == 0) continue;
        if (cs.indexOf(tail) >= 0 || ic.indexOf(tail) >= 0) return true;
    }
    return false;
}

static float sim_cosLat;

std::pair<int, int> projectToScreen(float predLat, float predLon) {
    float dLon = (predLon - simAirport->lon) * sim_cosLat;
    float normLon = (dLon + sim_rad) / (2.0f * sim_rad);
    float normLat = (predLat - simAirport->lat + sim_rad) / (2.0f * sim_rad);
    int x = (int)(normLon * RADAR_SIZE);
    int y = (int)(RADAR_SIZE - (normLat * RADAR_SIZE));
    return { x, y };
}

void loadMockData(const char* path) {
    std::ifstream f(path);
    if (!f.is_open()) { printf("Could not open %s\n", path); return; }
    std::stringstream buf;
    buf << f.rdbuf();
    std::string json = buf.str();

    auto statesPos = json.find("\"states\"");
    if (statesPos == std::string::npos) { printf("No states in JSON\n"); return; }
    auto arrStart = json.find('[', statesPos);
    if (arrStart == std::string::npos) return;

    size_t pos = arrStart + 1;
    unsigned long now = millis();

    while (pos < json.size()) {
        auto subStart = json.find('[', pos);
        if (subStart == std::string::npos) break;
        auto subEnd = json.find(']', subStart);
        if (subEnd == std::string::npos) break;

        std::string sub = json.substr(subStart + 1, subEnd - subStart - 1);
        pos = subEnd + 1;

        std::vector<std::string> fields;
        std::string field;
        bool inQuote = false;
        for (char c : sub) {
            if (c == '"') { inQuote = !inQuote; continue; }
            if (c == ',' && !inQuote) { fields.push_back(field); field.clear(); continue; }
            field += c;
        }
        fields.push_back(field);
        if (fields.size() < 17) continue;

        auto trim = [](std::string& s) {
            while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.erase(s.begin());
            while (!s.empty() && (s.back() == ' ' || s.back() == '\t')) s.pop_back();
        };
        auto toFloat = [&](const std::string& s) -> float {
            std::string t = s; trim(t);
            if (t == "null" || t.empty()) return 0.0f;
            return std::stof(t);
        };
        auto toLong = [&](const std::string& s) -> long {
            std::string t = s; trim(t);
            if (t == "null" || t.empty()) return 0;
            return std::stol(t);
        };
        auto toBool = [&](const std::string& s) -> bool {
            std::string t = s; trim(t);
            return t == "true";
        };

        Aircraft a;
        a.icao24        = String(fields[0].c_str()); a.icao24.trim();
        a.callsign      = String(fields[1].c_str());
        a.originCountry = String(fields[2].c_str());
        a.timePosition  = toLong(fields[3]);
        a.lastContact   = toLong(fields[4]);
        a.longitude     = toFloat(fields[5]);
        a.latitude      = toFloat(fields[6]);
        a.baroAltitude  = toFloat(fields[7]);
        a.onGround      = toBool(fields[8]);
        a.velocity       = toFloat(fields[9]);
        a.trueTrack     = toFloat(fields[10]);
        a.verticalRate  = toFloat(fields[11]);
        a.geoAltitude   = toFloat(fields[13]);
        a.squawk        = String(fields[14].c_str());
        a.spi           = toBool(fields[15]);
        a.positionSource = (int)toFloat(fields[16]);

        if (a.baroAltitude > MAX_ALT_METERS) continue;
        trackedAircraft.emplace(a.icao24, TrackedAircraft{ a, now });
    }
    printf("Loaded %zu aircraft from %s\n", trackedAircraft.size(), path);
}

LGFX tft;
LGFX_Sprite backbuffer(&tft);

// ---- Coastline drawing ----
void drawCoastline() {
    const CoastSegment* segments = nullptr;
    int segmentCount = 0;

    if (strcmp(simAirport->id, "KPAE") == 0) {
        segments = KPAE_COASTLINE;
        segmentCount = KPAE_COASTLINE_SEGMENTS;
    } else if (strcmp(simAirport->id, "KFHR") == 0) {
        segments = KFHR_COASTLINE;
        segmentCount = KFHR_COASTLINE_SEGMENTS;
    }

    if (!segments) return;

    const uint32_t coastColor = lgfx::color888(40, 80, 120);
    for (int s = 0; s < segmentCount; s++) {
        for (int i = 0; i < segments[s].count - 1; i++) {
            auto [x1, y1] = projectToScreen(segments[s].points[i].lat, segments[s].points[i].lon);
            auto [x2, y2] = projectToScreen(segments[s].points[i + 1].lat, segments[s].points[i + 1].lon);
            backbuffer.drawLine(x1, y1, x2, y2, coastColor);
        }
    }
}

// ---- Class D drawing ----
void drawClassD() {
    if (simAirport->classDNm <= 0) return;

    const uint32_t color = lgfx::color888(50, 100, 200);
    float pixelR = (simAirport->classDNm / (sim_rad * 120.0f)) * RADAR_SIZE * 0.5f;
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

// ---- Radar frame ----
void drawRadarFrame() {
    backbuffer.fillScreen(lgfx::color888(0, 0, 0));

    DrawScanLines(backbuffer,
        RADAR_CENTRE, RADAR_CENTRE,
        RADAR_CENTRE + (std::cos(millis() / 3000.0f) * (RADAR_CENTRE + 4)),
        RADAR_CENTRE + (std::sin(millis() / 3000.0f) * (RADAR_CENTRE + 4)),
        20, 128, 5);

    backbuffer.drawCircle(RADAR_CENTRE, RADAR_CENTRE, RADAR_RADIUS, lgfx::color888(0, 200, 0));
    backbuffer.drawCircle(RADAR_CENTRE, RADAR_CENTRE, (RADAR_RADIUS * 2) / 3, lgfx::color888(0, 64, 0));
    backbuffer.drawCircle(RADAR_CENTRE, RADAR_CENTRE, RADAR_RADIUS / 3, lgfx::color888(0, 32, 0));
    backbuffer.drawFastHLine(0, RADAR_CENTRE, RADAR_SIZE, lgfx::color888(0, 32, 0));
    backbuffer.drawFastVLine(RADAR_CENTRE, 0, RADAR_SIZE, lgfx::color888(0, 32, 0));

    drawCoastline();

    const uint32_t rwyColor = lgfx::color888(50, 100, 200);
    for (int r = 0; r < simAirport->numRunways; r++) {
        auto [x1, y1] = projectToScreen(simAirport->runways[r].lat1, simAirport->runways[r].lon1);
        auto [x2, y2] = projectToScreen(simAirport->runways[r].lat2, simAirport->runways[r].lon2);
        backbuffer.drawLine(x1, y1, x2, y2, rwyColor);
        backbuffer.drawLine(x1 - 1, y1, x2 - 1, y2, rwyColor);
        backbuffer.drawLine(x1 + 1, y1, x2 + 1, y2, rwyColor);
    }

    drawClassD();

    // Nearby airport runways (skip home airport's own)
    for (int i = 0; i < PNW_RUNWAY_COUNT; i++) {
        float midLat = (PNW_RUNWAYS[i].lat1 + PNW_RUNWAYS[i].lat2) * 0.5f;
        float midLon = (PNW_RUNWAYS[i].lon1 + PNW_RUNWAYS[i].lon2) * 0.5f;
        float dLat = midLat - simAirport->lat;
        float dLon = midLon - simAirport->lon;
        if (dLat * dLat + dLon * dLon < 0.0004f) continue;
        auto [x1, y1] = projectToScreen(PNW_RUNWAYS[i].lat1, PNW_RUNWAYS[i].lon1);
        auto [x2, y2] = projectToScreen(PNW_RUNWAYS[i].lat2, PNW_RUNWAYS[i].lon2);
        if ((x1 < 0 || x1 >= RADAR_SIZE) && (x2 < 0 || x2 >= RADAR_SIZE)) continue;
        if ((y1 < 0 || y1 >= RADAR_SIZE) && (y2 < 0 || y2 >= RADAR_SIZE)) continue;
        backbuffer.drawLine(x1, y1, x2, y2, rwyColor);
    }

    backbuffer.drawFastVLine(SIDEBAR_X, 0, DISPLAY_H, lgfx::color888(0, 64, 0));
    backbuffer.setTextSize(1.5);
    backbuffer.setTextColor(lgfx::color888(0, 200, 0));
    backbuffer.drawString(simAirport->id, SIDEBAR_X + 4, 3);
    backbuffer.setTextSize(1);
    backbuffer.setTextColor(lgfx::color888(0, 100, 0));
    backbuffer.drawString(String((int)sim_diameter_nm) + "nm", SIDEBAR_X + 4, 18);

    int airborne = 0;
    for (auto& [icao, tracked] : trackedAircraft)
        if (!tracked.state.onGround) airborne++;
    backbuffer.drawString(String(airborne) + " ac", SIDEBAR_X + 4, 30);

    backbuffer.setTextColor(GREEN_VDIM);
    backbuffer.drawString("192.168.1.42", SIDEBAR_X + 4, DISPLAY_H - 10);

    int yOff = 46;
    for (auto& [icao, tracked] : trackedAircraft) {
        if (tracked.state.onGround) continue;
        if (yOff > DISPLAY_H - 20) break;
        String cs = tracked.state.callsign; cs.trim();
        if (cs.length() == 0) cs = icao;
        bool known = isKnownTail(tracked.state.callsign, icao);
        backbuffer.setTextColor(known ? YELLOW_MID : GREEN_DIM);
        backbuffer.drawString(cs, SIDEBAR_X + 4, yOff);
        yOff += 10;
    }

    for (auto& [icao, tracked] : trackedAircraft) {
        if (tracked.state.onGround) continue;
        tracked.Tick();
        auto [predLat, predLon] = tracked.GetDisplayPosition();
        auto [x, y] = projectToScreen(predLat, predLon);
        if (x < 0 || x >= RADAR_SIZE || y < 0 || y >= RADAR_SIZE) continue;

        bool known = isKnownTail(tracked.state.callsign, icao);
        float dx = std::sin(radians(tracked.state.trueTrack));
        float dy = -std::cos(radians(tracked.state.trueTrack));
        float px = -dy, py = dx;
        float TL = 8.0f, TW = 4.0f;
        backbuffer.fillTriangle(
            x + dx*TL, y + dy*TL,
            x - dx*TL*0.5f + px*TW*0.5f, y - dy*TL*0.5f + py*TW*0.5f,
            x - dx*TL*0.5f - px*TW*0.5f, y - dy*TL*0.5f - py*TW*0.5f,
            known ? YELLOW_BRIGHT : GREEN_BRIGHT);

        backbuffer.setTextSize(1.5);
        backbuffer.setTextColor(known ? YELLOW_MID : GREEN_MID);
        String cs = tracked.state.callsign; cs.trim();
        if (cs.length() > 0) backbuffer.drawString(cs, x + 7, y + 7);
        int altFt = (int)(tracked.state.baroAltitude * 3.28084f);
        backbuffer.drawString(String(altFt) + "'", x + 7, y + 7 + 14);
        backbuffer.setTextSize(1);
    }

    backbuffer.pushSprite(0, 0);
}

// ---- Frame dispatcher ----
void drawFrame() {
    unsigned long timeout = (displayMode == MODE_TAF_MAP) ? 20000 : 15000;
    if (displayMode != MODE_RADAR && millis() - modeStartTime > timeout)
        displayMode = MODE_RADAR;

    switch (displayMode) {
        case MODE_METAR_DETAIL: {
            char verBuf[40];
            snprintf(verBuf, sizeof(verBuf), "v1.3.3  sim");
            wxDrawMetarDetail(backbuffer, wxData, simAirport->id,
                              simAirport->runways, simAirport->numRunways,
                              verBuf, millis());
            backbuffer.pushSprite(0, 0);
            break;
        }
        case MODE_WEATHER_MAP:
            wxDrawWeatherMap(backbuffer, wxData, millis());
            backbuffer.pushSprite(0, 0);
            break;
        case MODE_TAF_MAP:
        {
            const char* top[] = {simAirport->tafTop[0], simAirport->tafTop[1], simAirport->tafTop[2], simAirport->tafTop[3]};
            const char* bot[] = {simAirport->tafBot[0], simAirport->tafBot[1], simAirport->tafBot[2], simAirport->tafBot[3]};
            wxDrawTafMap(backbuffer, wxData, top, bot, millis());
        }
            backbuffer.pushSprite(0, 0);
            break;
        default:
            drawRadarFrame();
            break;
    }
}

void handleSimTouch() {
    lgfx::touch_point_t tp;
    if (!tft.getTouch(&tp)) return;

    static unsigned long lastTouch = 0;
    if (millis() - lastTouch < 400) return;
    lastTouch = millis();

    if (displayMode == MODE_METAR_DETAIL) {
        displayMode = MODE_RADAR;
        return;
    }
    if (displayMode == MODE_WEATHER_MAP) {
        if (tp.x >= DISPLAY_W / 2 && tp.y >= DISPLAY_H / 2) {
            displayMode = MODE_TAF_MAP;
            modeStartTime = millis();
        } else {
            displayMode = MODE_RADAR;
        }
        return;
    }
    if (displayMode != MODE_RADAR) {
        displayMode = MODE_RADAR;
        return;
    }

    if (tp.x >= RADAR_SIZE) return;

    bool left = tp.x < RADAR_SIZE / 2;
    bool top  = tp.y < RADAR_SIZE / 2;

    if (left && top) {
        sim_diameter_nm = sim_diameter_nm * 2.0 / 3.0;
        if (sim_diameter_nm < 2) sim_diameter_nm = 2;
        sim_rad = sim_diameter_nm / 120.0;
    } else if (!left && top) {
        displayMode = MODE_METAR_DETAIL;
        modeStartTime = millis();
    } else if (left && !top) {
        sim_diameter_nm = sim_diameter_nm * 3.0 / 2.0;
        if (sim_diameter_nm > 50) sim_diameter_nm = 50;
        sim_rad = sim_diameter_nm / 120.0;
    } else {
        displayMode = MODE_WEATHER_MAP;
        modeStartTime = millis();
    }
}

int simLoop(bool* running) {
    while (*running) {
        handleSimTouch();
        drawFrame();
        delay(16);
    }
    return 0;
}

int main(int argc, char** argv) {
    const char* dataPath = "test/mock_states.json";
    const char* airportArg = "KFHR";

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--airport") == 0 && i + 1 < argc) {
            airportArg = argv[++i];
        } else {
            dataPath = argv[i];
        }
    }

    if (strcasecmp(airportArg, "KPAE") == 0)
        simAirport = &AIRPORT_KPAE;
    else
        simAirport = &AIRPORT_KFHR;

    sim_cosLat = cosf(simAirport->lat * M_PI / 180.0f);
    printf("Airport: %s (%.4f, %.4f)\n", simAirport->id, simAirport->lat, simAirport->lon);

    tft.init();
    backbuffer.setColorDepth(8);
    backbuffer.createSprite(DISPLAY_W, DISPLAY_H);

    loadMockData(dataPath);

    memset(&wxData, 0, sizeof(wxData));
    httpfetch::globalInit();
    wxfetch::fetchAll(wxData, simAirport->metarStations,
                              simAirport->tafStations, millis());
    httpfetch::globalCleanup();

    lgfx::Panel_sdl::main(simLoop, 16);
    return 0;
}
