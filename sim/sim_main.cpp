#include <SDL.h>

#include "Arduino.h"

#include <LovyanGFX.hpp>
#include <lgfx/v1/platforms/sdl/Panel_sdl.hpp>
#include <lgfx/v1/platforms/sdl/common.hpp>

// SDL-backed display matching the CYD: 320×240 at 2× scaling
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
        _panel.setScaling(2, 2);
        _panel.setWindowTitle("KFHR Radar Simulator");
        setPanel(&_panel);
    }
};

#include "DrawHelpers.h"

// shared with firmware
#include "models/Aircraft.h"
#include "models/TrackedAircraft.h"

// ---- mock aircraft data loaded from JSON ----
#include <fstream>
#include <sstream>
#include <vector>
#include <map>

static double sim_lat = 48.5220;
static double sim_lon = -123.0244;
static double sim_rad = 0.3;
static std::vector<String> knownTails = { "N80117", "N2939J", "N9766Z", "N87KA" };
static std::map<String, TrackedAircraft> trackedAircraft;

constexpr int DISPLAY_W = 320;
constexpr int DISPLAY_H = 240;
constexpr int RADAR_SIZE = 240;
constexpr int RADAR_CENTRE = RADAR_SIZE / 2 - 1;
constexpr int RADAR_RADIUS = RADAR_SIZE / 2 - 1;
constexpr int SIDEBAR_X = RADAR_SIZE;
constexpr float MAX_ALT_METERS = 2438.4f; // 8000 ft

static const uint32_t GREEN_BRIGHT  = lgfx::color888(0, 255, 0);
static const uint32_t GREEN_MID     = lgfx::color888(0, 128, 0);
static const uint32_t GREEN_DIM     = lgfx::color888(0, 80, 0);
static const uint32_t GREEN_VDIM    = lgfx::color888(0, 60, 0);
static const uint32_t YELLOW_BRIGHT = lgfx::color888(255, 220, 0);
static const uint32_t YELLOW_MID    = lgfx::color888(180, 160, 0);

bool isKnownTail(const String& callsign, const String& icao) {
    String cs = callsign; cs.trim(); cs.toUpperCase();
    String ic = icao; ic.toUpperCase();
    for (const auto& tail : knownTails) {
        if (tail.length() == 0) continue;
        if (cs.indexOf(tail) >= 0 || ic.indexOf(tail) >= 0) return true;
    }
    return false;
}

std::pair<int, int> projectToScreen(float predLat, float predLon) {
    float normLon = (predLon - sim_lon + sim_rad) / (2.0f * sim_rad);
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

void drawFrame() {
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

    // sidebar
    backbuffer.drawFastVLine(SIDEBAR_X, 0, DISPLAY_H, lgfx::color888(0, 64, 0));
    backbuffer.setTextSize(1.5);
    backbuffer.setTextColor(lgfx::color888(0, 200, 0));
    backbuffer.drawString("KFHR", SIDEBAR_X + 4, 3);
    backbuffer.setTextSize(1);
    backbuffer.setTextColor(lgfx::color888(0, 100, 0));
    backbuffer.drawString(String(sim_rad, 1) + "d", SIDEBAR_X + 4, 18);

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

int simLoop(bool* running) {
    while (*running) {
        drawFrame();
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

    lgfx::Panel_sdl::main(simLoop, 16);

    return 0;
}
