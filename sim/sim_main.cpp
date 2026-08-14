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
static constexpr GeoPoint SIM_RWY_N = {48.5266f, -123.0250f};
static constexpr GeoPoint SIM_RWY_S = {48.5173f, -123.0240f};

static RunwayInfo simRunways[] = {
    {"16/34", 177.0f, 357.0f, 48.5266f, -123.0250f, 48.5173f, -123.0240f}
};

static double sim_lat = 48.5220;
static double sim_lon = -123.0244;
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
static const char* tafTopRow[4] = {"KFHR", "KNUW", "KPAE", "KBFI"};
static const char* tafBotRow[4] = {"KBVS", "KBLI", "KORS", "KCLM"};

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

static const float sim_cosLat = cosf(sim_lat * M_PI / 180.0f);

std::pair<int, int> projectToScreen(float predLat, float predLon) {
    float dLon = (predLon - sim_lon) * sim_cosLat;
    float normLon = (dLon + sim_rad) / (2.0f * sim_rad);
    float normLat = (predLat - sim_lat + sim_rad) / (2.0f * sim_rad);
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

    const uint32_t coastColor = lgfx::color888(40, 80, 120);
    for (int i = 0; i < SAN_JUAN_COASTLINE_COUNT - 1; i++) {
        auto [x1, y1] = projectToScreen(SAN_JUAN_COASTLINE[i].lat, SAN_JUAN_COASTLINE[i].lon);
        auto [x2, y2] = projectToScreen(SAN_JUAN_COASTLINE[i + 1].lat, SAN_JUAN_COASTLINE[i + 1].lon);
        backbuffer.drawLine(x1, y1, x2, y2, coastColor);
    }

    const uint32_t rwyColor = lgfx::color888(50, 100, 200);
    {
        auto [x1, y1] = projectToScreen(SIM_RWY_N.lat, SIM_RWY_N.lon);
        auto [x2, y2] = projectToScreen(SIM_RWY_S.lat, SIM_RWY_S.lon);
        backbuffer.drawLine(x1, y1, x2, y2, rwyColor);
        backbuffer.drawLine(x1 - 1, y1, x2 - 1, y2, rwyColor);
        backbuffer.drawLine(x1 + 1, y1, x2 + 1, y2, rwyColor);
    }

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
        case MODE_METAR_DETAIL:
            wxDrawMetarDetail(backbuffer, wxData, "KFHR",
                              simRunways, 1, "v1.1.0  sim", millis());
            backbuffer.pushSprite(0, 0);
            break;
        case MODE_WEATHER_MAP:
            wxDrawWeatherMap(backbuffer, wxData, millis());
            backbuffer.pushSprite(0, 0);
            break;
        case MODE_TAF_MAP:
            wxDrawTafMap(backbuffer, wxData, tafTopRow, tafBotRow, millis());
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
    if (argc > 1) dataPath = argv[1];

    tft.init();
    backbuffer.setColorDepth(8);
    backbuffer.createSprite(DISPLAY_W, DISPLAY_H);

    loadMockData(dataPath);

    memset(&wxData, 0, sizeof(wxData));
    httpfetch::globalInit();
    wxfetch::fetchAll(wxData, "KFHR KNUW KPAE KBFI KBVS KBLI KORS KCLM",
                              "KFHR KNUW KPAE KBFI KBVS KBLI KORS KCLM", millis());
    httpfetch::globalCleanup();

    lgfx::Panel_sdl::main(simLoop, 16);
    return 0;
}
