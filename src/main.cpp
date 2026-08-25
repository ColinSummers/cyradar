#include <Arduino.h>
#include <ArduinoJson.h>
#include <WiFiManager.h>
#include <HTTPUpdate.h>
#include <WiFiClientSecure.h>
#include <esp_system.h>
#include <esp_heap_caps.h>

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
#include "SonarPing.h"

constexpr unsigned long OTA_CHECK_INTERVAL = 24UL * 60 * 60 * 1000;
constexpr unsigned long WX_FETCH_INTERVAL = 5UL * 60 * 1000;
constexpr unsigned long CHECKIN_INTERVAL = 60UL * 1000;

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
unsigned long lastCheckin = 0;
bool renderScanlines = true;

enum DisplayMode { MODE_RADAR, MODE_METAR_DETAIL, MODE_WEATHER_MAP, MODE_TAF_MAP };
static DisplayMode displayMode = MODE_RADAR;
static unsigned long modeStartTime = 0;

static void saveUptime();
static int spriteFailCount = 0;
static unsigned long lastSpriteFailMs = 0;

static bool recreateSprite(const char* caller) {
    backbuffer.setColorDepth(8);
    bool ok = backbuffer.createSprite(DISPLAY_W, DISPLAY_H);
    size_t freeHeap = ESP.getFreeHeap();
    size_t largestBlock = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);

    if (!ok) {
        spriteFailCount++;
        lastSpriteFailMs = millis();
        Serial.printf("[SPRITE FAIL] %s: createSprite failed (#%d) heap=%u largest=%u\n",
                      caller, spriteFailCount, freeHeap, largestBlock);

        if (spriteFailCount >= 5) {
            Serial.printf("[SPRITE FAIL] %d consecutive failures, rebooting\n", spriteFailCount);
            saveUptime();
            ESP.restart();
        }
    } else {
        if (spriteFailCount > 0)
            Serial.printf("[SPRITE OK] %s: recovered after %d failures, heap=%u largest=%u\n",
                          caller, spriteFailCount, freeHeap, largestBlock);
        spriteFailCount = 0;
    }
    return ok;
}

static void deleteSprite(const char* caller) {
    backbuffer.deleteSprite();
    Serial.printf("[SPRITE] %s: deleted, heap=%u largest=%u\n",
                  caller, ESP.getFreeHeap(),
                  heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
}

static WxData wxData;
static const char* tafTopRow[4];
static const char* tafBotRow[4];
static char metarIds[128];
static char tafIds[128];

static const char* resetReasonStr() {
    switch (esp_reset_reason()) {
        case ESP_RST_POWERON:   return "poweron";
        case ESP_RST_SW:        return "sw";
        case ESP_RST_PANIC:     return "panic";
        case ESP_RST_INT_WDT:   return "int_wdt";
        case ESP_RST_TASK_WDT:  return "task_wdt";
        case ESP_RST_WDT:       return "wdt";
        case ESP_RST_DEEPSLEEP: return "deepsleep";
        case ESP_RST_BROWNOUT:  return "brownout";
        default:                return "unknown";
    }
}

static bool isCrashReason(esp_reset_reason_t r) {
    return r == ESP_RST_PANIC || r == ESP_RST_INT_WDT
        || r == ESP_RST_TASK_WDT || r == ESP_RST_WDT
        || r == ESP_RST_BROWNOUT;
}

static int crashCount = 0;

static void saveUptime() {
    Preferences prefs;
    prefs.begin("crashlog", false);
    prefs.putULong("uptime", millis() / 1000);
    prefs.end();
}

static void recordCrashIfAny() {
    Preferences prefs;
    prefs.begin("crashlog", false);

    esp_reset_reason_t reason = esp_reset_reason();
    String lastVersion = prefs.getString("fwver", "");
    if (lastVersion != FW_VERSION) {
        Serial.printf("[INFO] Firmware changed %s -> %s, resetting crash count\n",
                      lastVersion.c_str(), FW_VERSION);
        prefs.putInt("count", 0);
        prefs.putString("log", "");
        prefs.putString("fwver", FW_VERSION);
    }
    crashCount = prefs.getInt("count", 0);

    if (isCrashReason(reason)) {
        unsigned long lastUptime = prefs.getULong("uptime", 0);
        crashCount++;
        prefs.putInt("count", crashCount);

        String log = prefs.getString("log", "");
        char entry[64];
        snprintf(entry, sizeof(entry), "%s@%lus;", resetReasonStr(), lastUptime);
        log += entry;
        // keep last ~500 chars
        if (log.length() > 500)
            log = log.substring(log.length() - 500);
        prefs.putString("log", log);

        Serial.printf("[CRASH] Recorded: %s after %lu seconds uptime (total crashes: %d)\n",
                      resetReasonStr(), lastUptime, crashCount);
    }

    prefs.putULong("uptime", 0);
    prefs.end();
}

static void checkin(const char* event = "heartbeat") {
    String airport = configServer.GetStoredString("airport");
    String user = configServer.GetStoredString("opensky-id");
    if (airport.isEmpty()) airport = "KFHR";
    if (user.isEmpty()) user = WiFi.macAddress();

    char url[512];
    snprintf(url, sizeof(url),
             "https://george.pogsummers.com/cyradar/checkin?v=%s&airport=%s&user=%s&event=%s&reason=%s&heap=%u&largest=%u&crashes=%d&up=%lu&spritefails=%d",
             FW_VERSION, airport.c_str(), user.c_str(),
             event, resetReasonStr(), ESP.getFreeHeap(),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT),
             crashCount, millis() / 1000, spriteFailCount);
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
    deleteSprite("OTA");
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

    recreateSprite("OTA");
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

#ifdef BOARD_CYD
    tp.x = DISPLAY_W - 1 - tp.x;
#elif defined(BOARD_FREENOVE_S3)
    tp.x = DISPLAY_W - 1 - tp.x;
    tp.y = DISPLAY_H - 1 - tp.y;
#endif

    if (displayMode == MODE_METAR_DETAIL) {
        Serial.printf("[TOUCH] METAR screen x=%d y=%d\n", tp.x, tp.y);
        if (tp.x >= 218 && tp.y >= 198) {
            displayMode = MODE_RADAR;
            checkHttpOta();
            return;
        }
#ifdef BOARD_FREENOVE_S3
        if (tp.x >= 156 && tp.x < 212 && tp.y >= 198) {
            sonar::ping();
            return;
        }
#endif
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
    recordCrashIfAny();

    tft.init();
    tft.setRotation(1);

#ifdef BOARD_FREENOVE_S3
    sonar::init();
#endif

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

    configTzTime("PST8PDT,M3.2.0,M11.1.0", "pool.ntp.org");

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

    checkHttpOta();

    memset(&wxData, 0, sizeof(wxData));
    setupStationLists();

    if (wxCacheLoad()) {
        Serial.printf("[WX] Loaded cache: %d METARs, %d TAFs\n",
                      wxData.metarCount, wxData.tafCount);
        lastWxFetch = millis();
        deleteSprite("boot-cached");
        checkin("boot");
        recreateSprite("boot-cached");
    } else {
        tft.fillScreen(lgfx::color888(0, 0, 0));
        tft.setTextSize(1);
        tft.setTextColor(lgfx::color888(0, 160, 0));
        tft.drawCentreString("Fetching weather...", DISPLAY_W / 2, DISPLAY_H / 2);
        deleteSprite("boot-fresh");
        checkin("boot");
        fetchWeather();
        recreateSprite("boot-fresh");
    }

    lastOtaCheck = millis();
    lastCheckin = millis();
}

void drawRadarFrame()
{
    if (aircraftManager.NeedsFetch()) {
        deleteSprite("opensky");
        aircraftManager.Update();
        recreateSprite("opensky");
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
        deleteSprite("wx-fetch");
        fetchWeather();
        recreateSprite("wx-fetch");
    }

    if (millis() - lastCheckin >= CHECKIN_INTERVAL) {
        saveUptime();
        deleteSprite("checkin");
        checkin();
        recreateSprite("checkin");
        lastCheckin = millis();
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
