#include <Arduino.h>
#include <ArduinoJson.h>
#include <WiFiManager.h>
#include <HTTPClient.h>
#include <HTTPUpdate.h>
#include <WiFiClientSecure.h>

#include "Config.h"
#include "RadarLayout.h"
#include "Overlays.h"
#include "LGFX.h"
#include "WiFiManagerHelpers.h"
#include "ConfigurationWebServer.h"
#include "HttpRequestManager.h"
#include "OpenSkyAuthTokenHandler.h"
#include "AircraftManager.h"
#include "DrawHelpers.h"
#include "models/Aircraft.h"
#include "models/TrackedAircraft.h"

constexpr unsigned long OTA_CHECK_INTERVAL = 24UL * 60 * 60 * 1000;

LGFX tft;
LGFX_Sprite backbuffer(&tft);

WiFiManager wm;
ConfigurationWebServer configServer;
HttpRequestManager http;
OpenSkyAuthTokenHandler authHandler(http);

AircraftManager aircraftManager(configServer, authHandler, http);

unsigned long lastTouchTime = 0;
unsigned long lastOtaCheck = 0;
bool renderScanlines = true;

// ---- Display modes ----
enum DisplayMode { MODE_RADAR, MODE_METAR_DETAIL, MODE_WEATHER_MAP, MODE_TAF_MAP };
static DisplayMode displayMode = MODE_RADAR;
static unsigned long modeStartTime = 0;

// ---- Mock weather data (until real API fetch is implemented) ----
struct MockMetar {
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

static MockMetar mockMetars[] = {
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
static unsigned long mockMetarFetchTime = 0;

struct MockTafPeriod {
    const char* flightCat;
    const char* label;
};

struct MockTaf {
    const char* icao;
    MockTafPeriod periods[8];
    int periodCount;
};

static MockTaf mockTafs[] = {
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

static const char* findMetarCat(const char* icao) {
    for (int j = 0; j < MOCK_METAR_COUNT; j++)
        if (strcmp(mockMetars[j].icao, icao) == 0) return mockMetars[j].flightCat;
    return "???";
}

// ---- OTA ----
void showOtaStatus(const char* msg)
{
    tft.fillScreen(lgfx::color888(0, 0, 0));
    tft.setTextSize(1.5);
    tft.setTextColor(lgfx::color888(0, 255, 0));
    tft.drawCentreString(msg, DISPLAY_W / 2, DISPLAY_H / 2);
}

bool checkHttpOta()
{
    showOtaStatus("Checking for updates...");
    backbuffer.deleteSprite();
    Serial.printf("[OTA] Checking for update... (heap: %u)\n", ESP.getFreeHeap());

    bool updated = false;

    WiFiClientSecure secureClient;
    secureClient.setInsecure();
    secureClient.setTimeout(10);

    HTTPClient client;
    client.setConnectTimeout(10000);
    client.setTimeout(10000);
    client.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
    client.begin(secureClient, FW_VERSION_URL);
    int code = client.GET();

    if (code == 200) {
        String remoteVersion = client.getString();
        remoteVersion.trim();
        client.end();

        Serial.printf("[OTA] Local: %s  Remote: %s\n", FW_VERSION, remoteVersion.c_str());

        if (remoteVersion != FW_VERSION) {
            showOtaStatus("Updating firmware...");

            WiFiClientSecure transport;
            transport.setInsecure();
            httpUpdate.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
            t_httpUpdate_return ret = httpUpdate.update(transport, FW_BINARY_URL);

            if (ret == HTTP_UPDATE_OK) {
                Serial.println("[OTA] Update success, rebooting");
                ESP.restart();
            } else if (ret == HTTP_UPDATE_FAILED) {
                Serial.printf("[OTA] Update failed: %s\n", httpUpdate.getLastErrorString().c_str());
            }
        }
    } else {
        Serial.printf("[OTA] Version check failed: %d\n", code);
        client.end();
    }

    backbuffer.setColorDepth(8);
    backbuffer.createSprite(DISPLAY_W, DISPLAY_H);
    return updated;
}

// ---- WX drawing functions ----
void drawMetarDetail()
{
    backbuffer.fillScreen(0);

    const String& apt = aircraftManager.GetAirportId();
    MockMetar* m = nullptr;
    for (int i = 0; i < MOCK_METAR_COUNT; i++) {
        if (apt.equalsIgnoreCase(mockMetars[i].icao)) {
            m = &mockMetars[i];
            break;
        }
    }
    if (!m) m = &mockMetars[0];

    uint32_t green = lgfx::color888(0, 200, 0);
    uint32_t dim   = lgfx::color888(0, 100, 0);

    backbuffer.setTextSize(2);
    backbuffer.setTextColor(green);
    backbuffer.drawString(apt.c_str(), 10, 8);
    backbuffer.setTextColor(flightCatColor(m->flightCat));
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

    if (m->windSpd > 0) {
        const auto& rwys = aircraftManager.GetRunways();
        for (const auto& rwy : rwys) {
            float a1 = fmodf(m->windDir - rwy.heading1 + 360.0f, 360.0f);
            if (a1 > 180.0f) a1 -= 360.0f;
            float a2 = fmodf(m->windDir - rwy.heading2 + 360.0f, 360.0f);
            if (a2 > 180.0f) a2 -= 360.0f;

            const char* end;
            float angle;
            char endBuf[4];
            if (fabsf(a1) <= 90.0f) {
                int num = (int)(rwy.heading1 / 10.0f + 0.5f);
                snprintf(endBuf, sizeof(endBuf), "%d", num);
                end = endBuf;
                angle = a1;
            } else {
                int num = (int)(rwy.heading2 / 10.0f + 0.5f);
                snprintf(endBuf, sizeof(endBuf), "%d", num);
                end = endBuf;
                angle = a2;
            }

            float rad = angle * M_PI / 180.0f;
            int hw = (int)(m->windSpd * cosf(rad) + 0.5f);
            int xw = (int)(fabsf(m->windSpd * sinf(rad)) + 0.5f);
            snprintf(buf, sizeof(buf), "Rwy %-3s %d hw  %d xw", end, hw, xw);
            backbuffer.drawString(buf, 10, y);
            y += lh;
        }
    }
    y += 6;

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

    backbuffer.drawRect(218, 206, 96, 16, lgfx::color888(0, 100, 0));
    backbuffer.setTextColor(lgfx::color888(0, 160, 0));
    backbuffer.drawString("FW Update", 224, 210);

    int ageMin = (int)((millis() - mockMetarFetchTime) / 60000);
    snprintf(buf, sizeof(buf), "WX: %d min ago", ageMin);
    backbuffer.setTextColor(lgfx::color888(0, 60, 0));
    backbuffer.drawString(buf, 4, DISPLAY_H - 22);

    snprintf(buf, sizeof(buf), "v%s  %s", FW_VERSION, __DATE__);
    backbuffer.drawString(buf, 4, DISPLAY_H - 10);
}

void drawWeatherMap()
{
    backbuffer.fillScreen(0);

    const float latMin = 47.40f, latMax = 48.90f;
    const float lonMin = -123.60f, lonMax = -122.10f;
    const int mapTop = 14, mapBot = DISPLAY_H - 14;
    const int mapLeft = 10, mapRight = DISPLAY_W - 10;
    const int mapW = mapRight - mapLeft;
    const int mapH = mapBot - mapTop;

    for (int i = 0; i < ROUTE_STATION_COUNT; i++) {
        int sx = mapLeft + (int)((ROUTE_STATIONS[i].lon - lonMin) / (lonMax - lonMin) * mapW);
        int sy = mapTop + (int)((latMax - ROUTE_STATIONS[i].lat) / (latMax - latMin) * mapH);

        uint32_t color = flightCatColor(findMetarCat(ROUTE_STATIONS[i].icao));
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

    int ageMin = (int)((millis() - mockMetarFetchTime) / 60000);
    char buf[32];
    snprintf(buf, sizeof(buf), "%d min ago", ageMin);
    backbuffer.setTextColor(lgfx::color888(0, 60, 0));
    backbuffer.drawString(buf, 4, DISPLAY_H - 10);
}

static const char* TAF_TOP_ROW[] = {"KFHR", "KNUW", "KPAE", "KBFI"};
static const char* TAF_BOT_ROW[] = {"KBVS", "KBLI", "KORS", "KCLM"};

void drawTafMap()
{
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

    auto findTaf = [](const char* icao) -> const MockTaf* {
        for (int j = 0; j < MOCK_TAF_COUNT; j++)
            if (strcmp(mockTafs[j].icao, icao) == 0) return &mockTafs[j];
        return nullptr;
    };

    auto drawColumn = [&](int cx, const char* icao, int anchorY, int dir, bool showLabels) {
        const MockTaf* taf = findTaf(icao);
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
}

// ---- Touch ----
void handleTouch()
{
    lgfx::touch_point_t tp;
    if (!tft.getTouch(&tp)) return;

    unsigned long now = millis();
    if (now - lastTouchTime < 500) return;
    lastTouchTime = now;

    tp.x = DISPLAY_W - 1 - tp.x;

    if (displayMode == MODE_METAR_DETAIL) {
        if (tp.x >= 218 && tp.y >= 206) {
            displayMode = MODE_RADAR;
            checkHttpOta();
            return;
        }
        displayMode = MODE_RADAR;
        return;
    }

    if (displayMode == MODE_WEATHER_MAP) {
        if (tp.x >= DISPLAY_W / 2 && tp.y >= DISPLAY_H / 2) {
            displayMode = MODE_TAF_MAP;
            modeStartTime = millis();
            return;
        }
        displayMode = MODE_RADAR;
        return;
    }

    if (displayMode == MODE_TAF_MAP) {
        displayMode = MODE_RADAR;
        return;
    }

    if (tp.x >= RADAR_SIZE) return;

    if (tp.x < RADAR_SIZE / 2 && tp.y < RADAR_SIZE / 2) {
        double d = aircraftManager.GetDiameterNm();
        d *= 2.0 / 3.0;
        if (d < 2) d = 2;
        aircraftManager.SetDiameterNm(d);
    } else if (tp.x >= RADAR_SIZE / 2 && tp.y < RADAR_SIZE / 2) {
        displayMode = MODE_METAR_DETAIL;
        modeStartTime = millis();
    } else if (tp.x < RADAR_SIZE / 2 && tp.y >= RADAR_SIZE / 2) {
        double d = aircraftManager.GetDiameterNm();
        d *= 3.0 / 2.0;
        if (d > 50) d = 50;
        aircraftManager.SetDiameterNm(d);
    } else {
        displayMode = MODE_WEATHER_MAP;
        modeStartTime = millis();
    }
}

// ---- Main ----
void setup()
{
    Serial.begin(115200);

    tft.init();
    tft.setRotation(1);

    backbuffer.setColorDepth(8);
    backbuffer.createSprite(DISPLAY_W, DISPLAY_H);

    tft.fillScreen(lgfx::color888(0, 0, 0));
    tft.setTextSize(1.5);
    tft.setTextColor(lgfx::color888(0, 255, 0));
    tft.drawCentreString("KFHR Radar", DISPLAY_W / 2, DISPLAY_H / 2 - 16);
    tft.setTextSize(1);
    tft.drawCentreString("Connecting to WiFi...", DISPLAY_W / 2, DISPLAY_H / 2 + 8);

    WiFiManagerHelpers::ConfigureWiFiManager(wm, tft);

    if (strlen(WIFI_SSID) > 0) {
        WiFi.begin(WIFI_SSID, WIFI_PASS);
        WiFi.waitForConnectResult();
    }

    wm.autoConnect(WiFiManagerHelpers::WiFiManagerName);

    Serial.print("[INFO] WiFi connected, IP: ");
    Serial.println(WiFi.localIP());
    Serial.printf("[INFO] Firmware version: %s\n", FW_VERSION);

    tft.fillScreen(lgfx::color888(0, 0, 0));
    tft.setTextSize(1.5);
    tft.drawCentreString("WiFi connected!", DISPLAY_W / 2, DISPLAY_H / 2 - 16);
    tft.setTextSize(1);
    tft.drawCentreString(WiFi.localIP().toString(), DISPLAY_W / 2, DISPLAY_H / 2 + 8);
    delay(2500);

    configServer.Initialise();
    aircraftManager.Initialise();

    String scanlinePref = configServer.GetStoredString("scanline");
    renderScanlines = scanlinePref.isEmpty() || scanlinePref == "true";

    mockMetarFetchTime = millis();
    lastOtaCheck = millis();
}

void drawRadarFrame()
{
    aircraftManager.Update();

    backbuffer.fillScreen(lgfx::color888(0, 0, 0));

    if (renderScanlines) {
        float sweepAngle = aircraftManager.GetSweepAngle();
        DrawScanLines(backbuffer,
            RADAR_CENTRE,
            RADAR_CENTRE,
            RADAR_CENTRE + (std::cos(sweepAngle) * (RADAR_CENTRE + 4)),
            RADAR_CENTRE + (std::sin(sweepAngle) * (RADAR_CENTRE + 4)),
            20, 128, 5
        );
    }

    aircraftManager.Draw(backbuffer);
}

void loop()
{
    handleTouch();

    if (configServer.shouldRestart && millis() >= configServer.restartAt)
        ESP.restart();

    if (millis() - lastOtaCheck >= OTA_CHECK_INTERVAL) {
        lastOtaCheck = millis();
        checkHttpOta();
    }

    unsigned long timeout = (displayMode == MODE_TAF_MAP) ? 20000 : 15000;
    if (displayMode != MODE_RADAR && millis() - modeStartTime > timeout)
        displayMode = MODE_RADAR;

    switch (displayMode) {
        case MODE_METAR_DETAIL: drawMetarDetail(); break;
        case MODE_WEATHER_MAP:  drawWeatherMap();  break;
        case MODE_TAF_MAP:      drawTafMap();      break;
        default:                drawRadarFrame();  break;
    }

    backbuffer.pushSprite(0, 0);
}
