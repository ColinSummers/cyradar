#include <Arduino.h>
#include <ArduinoJson.h>
#include <WiFiManager.h>
#include <HTTPUpdate.h>
#include <WiFiClientSecure.h>

#include <Preferences.h>

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
#include "WeatherData.h"
#include "WeatherFetch.h"
#include "WeatherScreens.h"
#include "models/Aircraft.h"
#include "models/TrackedAircraft.h"

constexpr unsigned long OTA_CHECK_INTERVAL = 24UL * 60 * 60 * 1000;
constexpr unsigned long WX_FETCH_INTERVAL = 5UL * 60 * 1000;

LGFX tft;
LGFX_Sprite backbuffer(&tft);

WiFiManager wm;
ConfigurationWebServer configServer;
HttpRequestManager http;
OpenSkyAuthTokenHandler authHandler(http);

AircraftManager aircraftManager(configServer, authHandler, http);

unsigned long lastTouchTime = 0;
unsigned long lastOtaCheck = 0;
unsigned long lastWxFetch = 0;
bool renderScanlines = true;

enum DisplayMode { MODE_RADAR, MODE_METAR_DETAIL, MODE_WEATHER_MAP, MODE_TAF_MAP };
static DisplayMode displayMode = MODE_RADAR;
static unsigned long modeStartTime = 0;

static WxData wxData;
static const char* tafTopRow[4];
static const char* tafBotRow[4];
static char metarIds[128];
static char tafIds[128];

static void checkin() {
    String airport = configServer.GetStoredString("airport");
    String user = configServer.GetStoredString("opensky-id");
    if (airport.isEmpty()) airport = "KFHR";
    if (user.isEmpty()) user = WiFi.macAddress();

    char url[256];
    snprintf(url, sizeof(url),
             "https://george.pogsummers.com/cyradar/checkin?v=%s&airport=%s&user=%s",
             FW_VERSION, airport.c_str(), user.c_str());
    Serial.printf("[CHECKIN] %s\n", url);
    httpfetch::get(url);
}

static void setupStationLists() {
    String metarStations = configServer.GetStoredString("metars");
    String tafStations = configServer.GetStoredString("tafs");
    if (metarStations.isEmpty()) metarStations = "KFHR KNUW KPAE KBFI KBVS KBLI KORS";
    if (tafStations.isEmpty()) tafStations = "KFHR KNUW KPAE KBFI KBVS KBLI KORS KCLM";

    strncpy(metarIds, metarStations.c_str(), sizeof(metarIds) - 1);
    strncpy(tafIds, tafStations.c_str(), sizeof(tafIds) - 1);

    static char tafIdsCopy[128];
    strncpy(tafIdsCopy, tafIds, sizeof(tafIdsCopy) - 1);
    static char* tafStationPtrs[8] = {};
    int n = 0;
    char* tok = strtok(tafIdsCopy, " ");
    while (tok && n < 8) { tafStationPtrs[n++] = tok; tok = strtok(nullptr, " "); }
    while (n < 8) tafStationPtrs[n++] = (char*)"";
    for (int i = 0; i < 4; i++) tafTopRow[i] = tafStationPtrs[i];
    for (int i = 0; i < 4; i++) tafBotRow[i] = tafStationPtrs[4 + i];
}

static void wxCacheSave() {
    Preferences prefs;
    prefs.begin("wxcache", false);
    prefs.putInt("mc", wxData.metarCount);
    prefs.putInt("tc", wxData.tafCount);
    prefs.putBytes("metars", wxData.metars, sizeof(WxMetar) * wxData.metarCount);
    prefs.putBytes("tafs", wxData.tafs, sizeof(WxTaf) * wxData.tafCount);
    prefs.end();
    Serial.printf("[WX] Cached %d METARs, %d TAFs\n", wxData.metarCount, wxData.tafCount);
}

static bool wxCacheLoad() {
    Preferences prefs;
    prefs.begin("wxcache", true);
    int mc = prefs.getInt("mc", 0);
    int tc = prefs.getInt("tc", 0);
    if (mc <= 0 && tc <= 0) { prefs.end(); return false; }

    memset(&wxData, 0, sizeof(wxData));
    wxData.metarCount = mc;
    wxData.tafCount = tc;
    prefs.getBytes("metars", wxData.metars, sizeof(WxMetar) * mc);
    prefs.getBytes("tafs", wxData.tafs, sizeof(WxTaf) * tc);
    wxData.fetchTime = millis();
    prefs.end();
    return true;
}

static void fetchWeather() {
    setupStationLists();
    wxfetch::fetchAll(wxData, metarIds, tafIds, millis());
    Serial.printf("[WX] Parsed %d METARs, %d TAFs\n", wxData.metarCount, wxData.tafCount);
    for (int i = 0; i < wxData.metarCount; i++)
        Serial.printf("[WX]   %s: %s\n", wxData.metars[i].icao, wxData.metars[i].flightCat);
    wxCacheSave();
    lastWxFetch = millis();
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

    bool left = tp.x < RADAR_SIZE / 2;
    bool top  = tp.y < DISPLAY_H / 2;

    if (left && top) {
        double d = aircraftManager.GetDiameterNm();
        d *= 2.0 / 3.0;
        if (d < 2) d = 2;
        aircraftManager.SetDiameterNm(d);
    } else if (!left && top) {
        displayMode = MODE_METAR_DETAIL;
        modeStartTime = millis();
    } else if (left && !top) {
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
    tft.drawCentreString("CYRadar", DISPLAY_W / 2, DISPLAY_H / 2 - 16);
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
    delay(1500);

    configServer.Initialise();
    aircraftManager.Initialise();

    String scanlinePref = configServer.GetStoredString("scanline");
    renderScanlines = scanlinePref.isEmpty() || scanlinePref == "true";

    memset(&wxData, 0, sizeof(wxData));
    setupStationLists();

    if (wxCacheLoad()) {
        Serial.printf("[WX] Loaded cache: %d METARs, %d TAFs\n",
                      wxData.metarCount, wxData.tafCount);
        lastWxFetch = millis();
        backbuffer.deleteSprite();
        checkin();
        backbuffer.setColorDepth(8);
        backbuffer.createSprite(DISPLAY_W, DISPLAY_H);
    } else {
        tft.fillScreen(lgfx::color888(0, 0, 0));
        tft.setTextSize(1);
        tft.setTextColor(lgfx::color888(0, 160, 0));
        tft.drawCentreString("Fetching weather...", DISPLAY_W / 2, DISPLAY_H / 2);
        backbuffer.deleteSprite();
        checkin();
        fetchWeather();
        backbuffer.setColorDepth(8);
        backbuffer.createSprite(DISPLAY_W, DISPLAY_H);
    }

    lastOtaCheck = millis();
}

void drawRadarFrame()
{
    if (aircraftManager.NeedsFetch()) {
        backbuffer.deleteSprite();
        aircraftManager.Update();
        backbuffer.setColorDepth(8);
        if (!backbuffer.createSprite(DISPLAY_W, DISPLAY_H))
            Serial.printf("[WARN] createSprite failed, heap: %u\n", ESP.getFreeHeap());
    }

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

    if (millis() < configServer.configActiveUntil) {
        backbuffer.fillScreen(0);
        backbuffer.setTextSize(1.5);
        backbuffer.setTextColor(lgfx::color888(0, 160, 0));
        backbuffer.drawCentreString("Configuration", DISPLAY_W / 2, DISPLAY_H / 2 - 16);
        backbuffer.drawCentreString("in flux...", DISPLAY_W / 2, DISPLAY_H / 2 + 4);
        backbuffer.setTextSize(1);
        backbuffer.setTextColor(lgfx::color888(0, 80, 0));
        backbuffer.drawCentreString(WiFi.localIP().toString(), DISPLAY_W / 2, DISPLAY_H / 2 + 28);
        backbuffer.pushSprite(0, 0);
        return;
    }

    if (millis() - lastOtaCheck >= OTA_CHECK_INTERVAL) {
        lastOtaCheck = millis();
        checkHttpOta();
    }

    if (millis() - lastWxFetch >= WX_FETCH_INTERVAL) {
        backbuffer.deleteSprite();
        fetchWeather();
        backbuffer.setColorDepth(8);
        backbuffer.createSprite(DISPLAY_W, DISPLAY_H);
    }

    unsigned long timeout = (displayMode == MODE_TAF_MAP) ? 20000 : 15000;
    if (displayMode != MODE_RADAR && millis() - modeStartTime > timeout)
        displayMode = MODE_RADAR;

    switch (displayMode) {
        case MODE_METAR_DETAIL: {
            const auto& rwys = aircraftManager.GetRunways();
            char verBuf[40];
            snprintf(verBuf, sizeof(verBuf), "v%s  %s", FW_VERSION, __DATE__);
            wxDrawMetarDetail(backbuffer, wxData,
                aircraftManager.GetAirportId().c_str(),
                rwys.data(), (int)rwys.size(),
                verBuf, millis());
            break;
        }
        case MODE_WEATHER_MAP:
            wxDrawWeatherMap(backbuffer, wxData, millis());
            break;
        case MODE_TAF_MAP:
            wxDrawTafMap(backbuffer, wxData, tafTopRow, tafBotRow, millis());
            break;
        default:
            drawRadarFrame();
            break;
    }

    backbuffer.pushSprite(0, 0);
}
