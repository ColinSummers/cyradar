#include <SDL.h>

#include "Arduino.h"

#include <LovyanGFX.hpp>
#include <lgfx/v1/platforms/sdl/Panel_sdl.hpp>
#include <lgfx/v1/platforms/sdl/common.hpp>

#include <cstring>

// SDL-backed display matching the CYD: 320×240 at 1× scaling
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
        _panel.setWindowTitle("KFHR Radar Simulator");
        setPanel(&_panel);
    }
};

#include "DrawHelpers.h"

// shared with firmware
#include "models/Aircraft.h"
#include "models/TrackedAircraft.h"
#include "Overlays.h"

// ---- mock aircraft data loaded from JSON ----
#include <fstream>
#include <sstream>
#include <vector>
#include <map>

static double sim_lat = 48.5220;
static double sim_lon = -123.0244;
static double sim_diameter_nm = 8;
static double sim_rad = sim_diameter_nm / 120.0;
static std::vector<String> knownTails = { "N80117", "N2939J", "N9766Z", "N87KA" };
static std::map<String, TrackedAircraft> trackedAircraft;

// Shared with firmware
#include "RadarLayout.h"

static const uint32_t GREEN_BRIGHT  = lgfx::color888(0, 255, 0);
static const uint32_t GREEN_MID     = lgfx::color888(0, 128, 0);
static const uint32_t GREEN_DIM     = lgfx::color888(0, 80, 0);
static const uint32_t GREEN_VDIM    = lgfx::color888(0, 60, 0);
static const uint32_t YELLOW_BRIGHT = lgfx::color888(255, 220, 0);
static const uint32_t YELLOW_MID    = lgfx::color888(180, 160, 0);

// ---- Display modes ----
enum DisplayMode { MODE_RADAR, MODE_METAR_DETAIL, MODE_WEATHER_MAP, MODE_TAF_MAP };
static DisplayMode displayMode = MODE_RADAR;
static unsigned long modeStartTime = 0;
static unsigned long mockMetarFetchTime = 0;

// ---- Mock METAR data ----
struct SimMetar {
    const char* icao;
    int windDir;
    int windSpd;
    int windGust;
    float visibility;
    const char* sky;
    float tempC;
    float dewpC;
    float altimeter;
    const char* flightCat;
    const char* rawOb;
};

static SimMetar mockMetars[] = {
    {"KFHR", 210, 8, 0, 10.0f, "Few at 3500'", 18.0f, 12.0f, 30.12f, "VFR",
     "KFHR 140056Z AUTO 21008KT 10SM FEW035 18/12 A3012 RMK AO2"},
    {"KNUW", 200, 10, 0, 5.0f, "Broken at 2500'", 17.0f, 14.0f, 30.09f, "MVFR",
     "KNUW 140056Z AUTO 20010KT 5SM BKN025 17/14 A3009 RMK AO2"},
    {"KPAE", 170, 8, 0, 10.0f, "Few at 5000'", 21.0f, 13.0f, 30.10f, "VFR",
     "KPAE 140056Z 17008KT 10SM FEW050 21/13 A3010 RMK AO2"},
    {"KBFI", 190, 10, 0, 10.0f, "Scattered at 4500'", 22.0f, 14.0f, 30.08f, "VFR",
     "KBFI 140056Z 19010KT 10SM SCT045 22/14 A3008 RMK AO2"},
    {"KBVS", 180, 6, 0, 10.0f, "Clear", 22.0f, 11.0f, 30.11f, "VFR",
     "KBVS 140056Z AUTO 18006KT 10SM CLR 22/11 A3011 RMK AO2"},
    {"KBLI", 190, 12, 18, 10.0f, "Scattered at 4000'", 20.0f, 13.0f, 30.10f, "VFR",
     "KBLI 140056Z 19012G18KT 10SM SCT040 20/13 A3010 RMK AO2"},
    {"KORS", 220, 5, 0, 10.0f, "Few at 4000'", 19.0f, 12.0f, 30.11f, "VFR",
     "KORS 140056Z AUTO 22005KT 10SM FEW040 19/12 A3011 RMK AO2"},
    {"KCLM", 250, 7, 0, 10.0f, "Clear", 20.0f, 11.0f, 30.10f, "VFR",
     "KCLM 140056Z AUTO 25007KT 10SM CLR 20/11 A3010 RMK AO2"},
};
static const int MOCK_METAR_COUNT = sizeof(mockMetars) / sizeof(mockMetars[0]);

// ---- Mock TAF data (based on real aviationweather.gov TAFs) ----
struct SimTafPeriod {
    const char* flightCat;
    const char* label;
};

struct SimTaf {
    const char* icao;
    SimTafPeriod periods[8];
    int periodCount;
};

static SimTaf mockTafs[] = {
    {"KFHR", {}, 0},
    {"KNUW", {{"VFR","12a"},{"IFR","1a"},{"LIFR","T"},{"VFR","11a"}}, 4},
    {"KPAE", {{"VFR","11p"},{"LIFR","5a"},{"VFR","9a"},{"VFR","4p"},{"VFR","8p"}}, 5},
    {"KBFI", {{"VFR","11p"},{"MVFR","6a"},{"IFR","7a"},{"VFR","10a"},{"VFR","8p"}}, 5},
    {"KBVS", {}, 0},
    {"KBLI", {{"VFR","11p"},{"VFR","6a"},{"VFR","10a"}}, 3},
    {"KORS", {}, 0},
    {"KCLM", {{"LIFR","11p"},{"MVFR","9a"},{"VFR","1p"},{"VFR","5p"}}, 4},
};
static const int MOCK_TAF_COUNT = sizeof(mockTafs) / sizeof(mockTafs[0]);

static uint32_t flightCatColor(const char* cat) {
    if (strcmp(cat, "VFR") == 0)  return lgfx::color888(0, 220, 0);
    if (strcmp(cat, "MVFR") == 0) return lgfx::color888(0, 140, 255);
    if (strcmp(cat, "IFR") == 0)  return lgfx::color888(255, 100, 180);
    if (strcmp(cat, "LIFR") == 0) return lgfx::color888(255, 0, 0);
    return lgfx::color888(128, 128, 128);
}

bool isKnownTail(const String& callsign, const String& icao) {
    String cs = callsign; cs.trim(); cs.toUpperCase();
    String ic = icao; ic.toUpperCase();
    for (const auto& tail : knownTails) {
        if (tail.length() == 0) continue;
        if (cs.indexOf(tail) >= 0 || ic.indexOf(tail) >= 0) return true;
    }
    return false;
}

static const float sim_cosLat = cosf(sim_lat * M_PI / 180.0f);

std::pair<int, int> projectToScreen(float predLat, float predLon) {
    float dLon = (predLon - sim_lon) * sim_cosLat;
    float normLon = (dLon + sim_rad) / (2.0f * sim_rad);
    float normLat = (predLat - sim_lat + sim_rad) / (2.0f * sim_rad);
    int x = (int)(normLon * RADAR_SIZE);
    int y = (int)(RADAR_SIZE - (normLat * RADAR_SIZE));
    return { x, y };
}

// Minimal JSON parser for the mock file (just enough for OpenSky states array)
void loadMockData(const char* path) {
    std::ifstream f(path);
    if (!f.is_open()) {
        printf("Could not open %s\n", path);
        return;
    }
    std::stringstream buf;
    buf << f.rdbuf();
    std::string json = buf.str();

    // find "states" array
    auto statesPos = json.find("\"states\"");
    if (statesPos == std::string::npos) { printf("No states in JSON\n"); return; }

    auto arrStart = json.find('[', statesPos);
    if (arrStart == std::string::npos) return;

    // parse each sub-array: ["icao", "callsign", ...]
    size_t pos = arrStart + 1;
    unsigned long now = millis();

    while (pos < json.size()) {
        auto subStart = json.find('[', pos);
        if (subStart == std::string::npos) break;
        auto subEnd = json.find(']', subStart);
        if (subEnd == std::string::npos) break;

        std::string sub = json.substr(subStart + 1, subEnd - subStart - 1);
        pos = subEnd + 1;

        // tokenize by comma, respecting quotes
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
        a.icao24        = String(fields[0].c_str());    a.icao24.trim();
        a.callsign      = String(fields[1].c_str());
        a.originCountry = String(fields[2].c_str());
        a.timePosition  = toLong(fields[3]);
        a.lastContact   = toLong(fields[4]);
        a.longitude     = toFloat(fields[5]);
        a.latitude      = toFloat(fields[6]);
        a.baroAltitude  = toFloat(fields[7]);
        a.onGround      = toBool(fields[8]);
        a.velocity      = toFloat(fields[9]);
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

// ---- Radar frame (normal view) ----
void drawRadarFrame() {
    backbuffer.fillScreen(lgfx::color888(0, 0, 0));

    // sweep arm
    DrawScanLines(backbuffer,
        RADAR_CENTRE, RADAR_CENTRE,
        RADAR_CENTRE + (std::cos(millis() / 3000.0f) * (RADAR_CENTRE + 4)),
        RADAR_CENTRE + (std::sin(millis() / 3000.0f) * (RADAR_CENTRE + 4)),
        20, 128, 5);

    // range rings
    backbuffer.drawCircle(RADAR_CENTRE, RADAR_CENTRE, RADAR_RADIUS, lgfx::color888(0, 200, 0));
    backbuffer.drawCircle(RADAR_CENTRE, RADAR_CENTRE, (RADAR_RADIUS * 2) / 3, lgfx::color888(0, 64, 0));
    backbuffer.drawCircle(RADAR_CENTRE, RADAR_CENTRE, RADAR_RADIUS / 3, lgfx::color888(0, 32, 0));

    // crosshairs
    backbuffer.drawFastHLine(0, RADAR_CENTRE, RADAR_SIZE, lgfx::color888(0, 32, 0));
    backbuffer.drawFastVLine(RADAR_CENTRE, 0, RADAR_SIZE, lgfx::color888(0, 32, 0));

    // coastline
    const uint32_t coastColor = lgfx::color888(40, 80, 120);
    for (int i = 0; i < SAN_JUAN_COASTLINE_COUNT - 1; i++) {
        auto [x1, y1] = projectToScreen(SAN_JUAN_COASTLINE[i].lat, SAN_JUAN_COASTLINE[i].lon);
        auto [x2, y2] = projectToScreen(SAN_JUAN_COASTLINE[i + 1].lat, SAN_JUAN_COASTLINE[i + 1].lon);
        backbuffer.drawLine(x1, y1, x2, y2, coastColor);
    }

    // runway
    const uint32_t rwyColor = lgfx::color888(50, 100, 200);
    {
        auto [x1, y1] = projectToScreen(RUNWAY_NORTH.lat, RUNWAY_NORTH.lon);
        auto [x2, y2] = projectToScreen(RUNWAY_SOUTH.lat, RUNWAY_SOUTH.lon);
        backbuffer.drawLine(x1, y1, x2, y2, rwyColor);
        backbuffer.drawLine(x1 - 1, y1, x2 - 1, y2, rwyColor);
        backbuffer.drawLine(x1 + 1, y1, x2 + 1, y2, rwyColor);
    }

    // sidebar
    backbuffer.drawFastVLine(SIDEBAR_X, 0, DISPLAY_H, lgfx::color888(0, 64, 0));
    backbuffer.setTextSize(1.5);
    backbuffer.setTextColor(lgfx::color888(0, 200, 0));
    backbuffer.drawString("KFHR", SIDEBAR_X + 4, 3);
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

    // aircraft blips
    for (auto& [icao, tracked] : trackedAircraft) {
        if (tracked.state.onGround) continue;
        tracked.Tick();
        auto [predLat, predLon] = tracked.GetDisplayPosition();
        auto [x, y] = projectToScreen(predLat, predLon);
        if (x < 0 || x >= RADAR_SIZE || y < 0 || y >= RADAR_SIZE) continue;

        bool known = isKnownTail(tracked.state.callsign, icao);

        // triangle
        float dx = std::sin(radians(tracked.state.trueTrack));
        float dy = -std::cos(radians(tracked.state.trueTrack));
        float px = -dy, py = dx;
        float TL = 8.0f, TW = 4.0f;
        backbuffer.fillTriangle(
            x + dx*TL, y + dy*TL,
            x - dx*TL*0.5f + px*TW*0.5f, y - dy*TL*0.5f + py*TW*0.5f,
            x - dx*TL*0.5f - px*TW*0.5f, y - dy*TL*0.5f - py*TW*0.5f,
            known ? YELLOW_BRIGHT : GREEN_BRIGHT);

        // labels
        backbuffer.setTextSize(1.5);
        backbuffer.setTextColor(known ? YELLOW_MID : GREEN_MID);
        String cs = tracked.state.callsign; cs.trim();
        int lineHeight = 14;
        if (cs.length() > 0)
            backbuffer.drawString(cs, x + 7, y + 7);
        int altFt = (int)(tracked.state.baroAltitude * 3.28084f);
        backbuffer.drawString(String(altFt) + "'", x + 7, y + 7 + lineHeight);
        backbuffer.setTextSize(1);
    }

    backbuffer.pushSprite(0, 0);
}

// ---- METAR detail screen (KFHR) ----
void drawMetarDetail() {
    backbuffer.fillScreen(0);

    SimMetar* m = nullptr;
    for (int i = 0; i < MOCK_METAR_COUNT; i++) {
        if (strcmp(mockMetars[i].icao, "KFHR") == 0) {
            m = &mockMetars[i];
            break;
        }
    }
    if (!m) { backbuffer.pushSprite(0, 0); return; }

    uint32_t green = lgfx::color888(0, 200, 0);
    uint32_t dim   = lgfx::color888(0, 100, 0);
    uint32_t catColor = flightCatColor(m->flightCat);

    backbuffer.setTextSize(2);
    backbuffer.setTextColor(green);
    backbuffer.drawString("KFHR", 10, 8);
    backbuffer.setTextColor(catColor);
    backbuffer.drawString(m->flightCat, 110, 8);

    backbuffer.setTextSize(1);
    backbuffer.setTextColor(green);

    char buf[80];
    int y = 38;
    const int lh = 14;

    if (m->windSpd == 0)
        snprintf(buf, sizeof(buf), "Wind    Calm");
    else if (m->windGust > 0)
        snprintf(buf, sizeof(buf), "Wind    %03d at %d G%d kt", m->windDir, m->windSpd, m->windGust);
    else
        snprintf(buf, sizeof(buf), "Wind    %03d at %d kt", m->windDir, m->windSpd);
    backbuffer.drawString(buf, 10, y); y += lh;

    if (m->visibility >= 10.0f)
        snprintf(buf, sizeof(buf), "Vis     10+ SM");
    else
        snprintf(buf, sizeof(buf), "Vis     %.0f SM", m->visibility);
    backbuffer.drawString(buf, 10, y); y += lh;

    snprintf(buf, sizeof(buf), "Sky     %s", m->sky);
    backbuffer.drawString(buf, 10, y); y += lh;

    float tempF = m->tempC * 9.0f / 5.0f + 32.0f;
    float dewF  = m->dewpC * 9.0f / 5.0f + 32.0f;
    snprintf(buf, sizeof(buf), "Temp    %.0fF  Dew %.0fF", tempF, dewF);
    backbuffer.drawString(buf, 10, y); y += lh;

    snprintf(buf, sizeof(buf), "Altim   %.2f\"", m->altimeter);
    backbuffer.drawString(buf, 10, y); y += lh;

    // crosswind for the runway with a headwind
    if (m->windSpd > 0) {
        float a16 = fmodf(m->windDir - RWY16_TRUE_HDG + 360.0f, 360.0f);
        if (a16 > 180.0f) a16 -= 360.0f;
        float a34 = fmodf(m->windDir - RWY34_TRUE_HDG + 360.0f, 360.0f);
        if (a34 > 180.0f) a34 -= 360.0f;

        int rwy;
        float angle;
        if (fabsf(a16) <= 90.0f) { rwy = 16; angle = a16; }
        else                      { rwy = 34; angle = a34; }

        float rad = angle * M_PI / 180.0f;
        int hw = (int)(m->windSpd * cosf(rad) + 0.5f);
        int xw = (int)(fabsf(m->windSpd * sinf(rad)) + 0.5f);
        snprintf(buf, sizeof(buf), "Rwy %d   %d hw  %d xw", rwy, hw, xw);
        backbuffer.drawString(buf, 10, y);
    }
    y += lh + 6;

    // raw METAR in dim text, word-wrapped
    backbuffer.setTextColor(dim);
    String raw = m->rawOb;
    const int maxChars = (DISPLAY_W - 20) / 6;
    if ((int)raw.length() > maxChars) {
        int splitAt = -1;
        for (int i = maxChars; i >= maxChars / 2; i--) {
            if (raw[i] == ' ') { splitAt = i; break; }
        }
        if (splitAt < 0) splitAt = maxChars;
        backbuffer.drawString(raw.substring(0, splitAt), 10, y); y += 10;
        backbuffer.drawString(raw.substring(splitAt + 1), 10, y);
    } else {
        backbuffer.drawString(raw, 10, y);
    }

    // Firmware update button — bottom right
    backbuffer.drawRect(218, 206, 96, 16, lgfx::color888(0, 100, 0));
    backbuffer.setTextColor(lgfx::color888(0, 160, 0));
    backbuffer.drawString("FW Update", 224, 210);

    // WX age — second-to-last line
    int ageMin = (int)((millis() - mockMetarFetchTime) / 60000);
    snprintf(buf, sizeof(buf), "WX: %d min ago", ageMin);
    backbuffer.setTextColor(lgfx::color888(0, 60, 0));
    backbuffer.drawString(buf, 4, DISPLAY_H - 22);

    // version + build date — last line
    backbuffer.drawString("v1.1.0  Aug 13 2026", 4, DISPLAY_H - 10);

    backbuffer.pushSprite(0, 0);
}

// ---- Weather station map ----
void drawWeatherMap() {
    backbuffer.fillScreen(0);

    const float latMin = 47.40f, latMax = 48.90f;
    const float lonMin = -123.60f, lonMax = -122.10f;
    const int mapTop = 14, mapBot = DISPLAY_H - 14;
    const int mapLeft = 10, mapRight = DISPLAY_W - 10;
    const int mapW = mapRight - mapLeft;
    const int mapH = mapBot - mapTop;

    // stations
    for (int i = 0; i < ROUTE_STATION_COUNT; i++) {
        int sx = mapLeft + (int)((ROUTE_STATIONS[i].lon - lonMin) / (lonMax - lonMin) * mapW);
        int sy = mapTop + (int)((latMax - ROUTE_STATIONS[i].lat) / (latMax - latMin) * mapH);

        const char* cat = "???";
        for (int j = 0; j < MOCK_METAR_COUNT; j++) {
            if (strcmp(mockMetars[j].icao, ROUTE_STATIONS[i].icao) == 0) {
                cat = mockMetars[j].flightCat;
                break;
            }
        }

        uint32_t color = flightCatColor(cat);
        backbuffer.fillCircle(sx, sy, 6, color);
        backbuffer.drawCircle(sx, sy, 7, lgfx::color888(60, 60, 60));

        backbuffer.setTextSize(1);
        backbuffer.setTextColor(lgfx::color888(180, 180, 180));

        int labelX = sx + 10;
        int labelY = sy - 4;
        if (labelX + 28 > DISPLAY_W - 4)
            labelX = sx - 32;

        backbuffer.drawString(ROUTE_STATIONS[i].icao, labelX, labelY);
    }

    // timestamp
    int ageMin = (int)((millis() - mockMetarFetchTime) / 60000);
    char buf[32];
    snprintf(buf, sizeof(buf), "%d min ago", ageMin);
    backbuffer.setTextColor(lgfx::color888(0, 60, 0));
    backbuffer.drawString(buf, 4, DISPLAY_H - 10);

    backbuffer.pushSprite(0, 0);
}

// ---- TAF grid ----
// Top row: FHR NUW PAE BFI (dots above)
// Bot row: BVS BLI ORS CLM (dots below)
static const char* TAF_TOP_ROW[] = {"KFHR", "KNUW", "KPAE", "KBFI"};
static const char* TAF_BOT_ROW[] = {"KBVS", "KBLI", "KORS", "KCLM"};

void drawTafMap() {
    backbuffer.fillScreen(0);

    const int cols = 4;
    const int colW = 72;
    const int gridW = cols * colW;
    const int xOff = (DISPLAY_W - gridW) / 2;

    const int topRowY = 110;
    const int botRowY = 126;
    const int dotR = 4;
    const int dotSpacing = 12;

    backbuffer.setTextSize(1);

    auto findTaf = [](const char* icao) -> const SimTaf* {
        for (int j = 0; j < MOCK_TAF_COUNT; j++)
            if (strcmp(mockTafs[j].icao, icao) == 0) return &mockTafs[j];
        return nullptr;
    };

    auto findMetarCat = [](const char* icao) -> const char* {
        for (int j = 0; j < MOCK_METAR_COUNT; j++)
            if (strcmp(mockMetars[j].icao, icao) == 0) return mockMetars[j].flightCat;
        return "???";
    };

    // Find the longest TAF among the rightmost column (BFI top, CLM bottom)
    // to draw time labels after dots
    auto drawColumn = [&](int cx, const char* icao, int anchorY, int dir, bool showLabels) {
        const SimTaf* taf = findTaf(icao);
        if (taf && taf->periodCount > 0) {
            for (int p = 0; p < taf->periodCount; p++) {
                int dy = anchorY + dir * (p * dotSpacing);
                backbuffer.fillCircle(cx, dy, dotR, flightCatColor(taf->periods[p].flightCat));
                if (strcmp(taf->periods[p].label, "T") == 0)
                    backbuffer.drawCircle(cx, dy, dotR + 1, lgfx::color888(120, 120, 120));
                if (showLabels) {
                    backbuffer.setTextColor(lgfx::color888(0, 80, 0));
                    backbuffer.drawString(taf->periods[p].label, cx + dotR + 4, dy - 4);
                }
            }
        } else {
            backbuffer.fillCircle(cx, anchorY, dotR, flightCatColor(findMetarCat(icao)));
        }
    };

    for (int c = 0; c < cols; c++) {
        int cx = xOff + c * colW + colW / 2;
        bool isLast = (c == cols - 1);

        backbuffer.setTextColor(lgfx::color888(0, 160, 0));
        backbuffer.drawCentreString(TAF_TOP_ROW[c] + 1, cx, topRowY);
        drawColumn(cx, TAF_TOP_ROW[c], topRowY - 10, -1, isLast);

        backbuffer.setTextColor(lgfx::color888(0, 160, 0));
        backbuffer.drawCentreString(TAF_BOT_ROW[c] + 1, cx, botRowY);
        drawColumn(cx, TAF_BOT_ROW[c], botRowY + 14, 1, isLast);
    }

    int ageMin = (int)((millis() - mockMetarFetchTime) / 60000);
    char buf[32];
    snprintf(buf, sizeof(buf), "TAF  %d min ago", ageMin);
    backbuffer.setTextColor(lgfx::color888(0, 60, 0));
    backbuffer.drawString(buf, 4, DISPLAY_H - 10);

    backbuffer.pushSprite(0, 0);
}

// ---- Frame dispatcher ----
void drawFrame() {
    unsigned long timeout = (displayMode == MODE_TAF_MAP) ? 20000 : 15000;
    if (displayMode != MODE_RADAR && millis() - modeStartTime > timeout)
        displayMode = MODE_RADAR;

    switch (displayMode) {
        case MODE_METAR_DETAIL: drawMetarDetail(); break;
        case MODE_WEATHER_MAP:  drawWeatherMap();  break;
        case MODE_TAF_MAP:      drawTafMap();      break;
        default:                drawRadarFrame();  break;
    }
}

void handleSimTouch() {
    lgfx::touch_point_t tp;
    if (!tft.getTouch(&tp)) return;

    static unsigned long lastTouch = 0;
    if (millis() - lastTouch < 400) return;
    lastTouch = millis();

    if (displayMode == MODE_METAR_DETAIL) {
        if (tp.x >= 218 && tp.y >= 206) {
            printf("Firmware update check requested\n");
        }
        displayMode = MODE_RADAR;
        return;
    }
    if (displayMode == MODE_WEATHER_MAP) {
        if (tp.x >= DISPLAY_W / 2 && tp.y >= DISPLAY_H / 2) {
            displayMode = MODE_TAF_MAP;
            modeStartTime = millis();
            printf("TAF map\n");
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
        printf("Zoom in: %.1f nm\n", sim_diameter_nm);
    } else if (!left && top) {
        displayMode = MODE_METAR_DETAIL;
        modeStartTime = millis();
        printf("METAR detail\n");
    } else if (left && !top) {
        sim_diameter_nm = sim_diameter_nm * 3.0 / 2.0;
        if (sim_diameter_nm > 50) sim_diameter_nm = 50;
        sim_rad = sim_diameter_nm / 120.0;
        printf("Zoom out: %.1f nm\n", sim_diameter_nm);
    } else {
        displayMode = MODE_WEATHER_MAP;
        modeStartTime = millis();
        printf("Weather map\n");
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
    if (argc > 1) dataPath = argv[1];

    tft.init();

    backbuffer.setColorDepth(8);
    backbuffer.createSprite(DISPLAY_W, DISPLAY_H);

    loadMockData(dataPath);
    mockMetarFetchTime = millis();

    lgfx::Panel_sdl::main(simLoop, 16);

    return 0;
}
