#include <Arduino.h>
#include <ArduinoJson.h>
#include <WiFiManager.h>
#include <ArduinoOTA.h>
#include <HTTPClient.h>
#include <HTTPUpdate.h>

#include "Config.h"
#include "LGFX.h"
#include "WiFiManagerHelpers.h"
#include "ConfigurationWebServer.h"
#include "HttpRequestManager.h"
#include "OpenSkyAuthTokenHandler.h"
#include "AircraftManager.h"
#include "DrawHelpers.h"
#include "models/Aircraft.h"
#include "models/TrackedAircraft.h"

constexpr int DISPLAY_W = 320;
constexpr int DISPLAY_H = 240;
constexpr int RADAR_SIZE = 240;
constexpr int RADAR_CENTRE = RADAR_SIZE / 2 - 1;

constexpr unsigned long OTA_CHECK_INTERVAL = 24UL * 60 * 60 * 1000;

LGFX tft;
LGFX_Sprite backbuffer(&tft);

WiFiManager wm;
ConfigurationWebServer configServer;
HttpRequestManager http;
OpenSkyAuthTokenHandler authHandler(http);

AircraftManager aircraftManager(configServer, authHandler, http, tft);

bool otaMode = false;
bool otaConfirmShown = false;
unsigned long otaConfirmTime = 0;
unsigned long lastTouchTime = 0;
unsigned long lastOtaCheck = 0;

void showOtaStatus(const char* msg)
{
    tft.fillScreen(lgfx::color888(0, 0, 0));
    tft.setTextSize(1.5);
    tft.setTextColor(lgfx::color888(0, 255, 0));
    tft.drawCentreString(msg, DISPLAY_W / 2, DISPLAY_H / 2);
}

bool checkHttpOta()
{
    Serial.println("[OTA] Checking for update...");

    HTTPClient client;
    client.begin(FW_VERSION_URL);
    int code = client.GET();
    if (code != 200) {
        Serial.printf("[OTA] Version check failed: %d\n", code);
        client.end();
        return false;
    }

    String remoteVersion = client.getString();
    remoteVersion.trim();
    client.end();

    Serial.printf("[OTA] Local: %s  Remote: %s\n", FW_VERSION, remoteVersion.c_str());

    if (remoteVersion == FW_VERSION)
        return false;

    showOtaStatus("Updating firmware...");

    WiFiClient transport;
    httpUpdate.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
    t_httpUpdate_return ret = httpUpdate.update(transport, FW_BINARY_URL);

    switch (ret) {
        case HTTP_UPDATE_OK:
            Serial.println("[OTA] Update success, rebooting");
            ESP.restart();
            break;
        case HTTP_UPDATE_FAILED:
            Serial.printf("[OTA] Update failed: %s\n", httpUpdate.getLastErrorString().c_str());
            break;
        case HTTP_UPDATE_NO_UPDATES:
            Serial.println("[OTA] No update needed");
            break;
    }
    return ret == HTTP_UPDATE_OK;
}

void beginArduinoOta()
{
    otaMode = true;

    ArduinoOTA.setHostname("kfhr-radar");
    ArduinoOTA.onStart([]() {
        showOtaStatus("OTA Updating...");
    });
    ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
        int pct = progress / (total / 100);
        tft.fillRect(60, DISPLAY_H / 2 + 20, 200, 12, lgfx::color888(0, 0, 0));
        tft.drawRect(60, DISPLAY_H / 2 + 20, 200, 12, lgfx::color888(0, 200, 0));
        tft.fillRect(60, DISPLAY_H / 2 + 20, pct * 2, 12, lgfx::color888(0, 255, 0));
    });
    ArduinoOTA.onEnd([]() {
        showOtaStatus("Update complete!");
    });
    ArduinoOTA.onError([](ota_error_t error) {
        otaMode = false;
        Serial.printf("[OTA] Error %u\n", error);
    });
    ArduinoOTA.begin();
    Serial.println("[OTA] ArduinoOTA listening...");
}

void handleTouch()
{
    lgfx::touch_point_t tp;
    if (!tft.getTouch(&tp)) return;

    unsigned long now = millis();
    if (now - lastTouchTime < 500) return;
    lastTouchTime = now;

    if (otaConfirmShown && (now - otaConfirmTime < 5000)) {
        otaConfirmShown = false;
        beginArduinoOta();

        tft.fillScreen(lgfx::color888(0, 0, 0));
        tft.setTextSize(1.5);
        tft.setTextColor(lgfx::color888(0, 255, 0));
        tft.drawCentreString("OTA Ready", DISPLAY_W / 2, DISPLAY_H / 2 - 30);
        tft.setTextSize(1);
        tft.setTextColor(lgfx::color888(0, 160, 0));
        tft.drawCentreString("Upload from PlatformIO:", DISPLAY_W / 2, DISPLAY_H / 2);
        String cmd = "pio run -t upload --upload-port " + WiFi.localIP().toString();
        tft.drawCentreString(cmd, DISPLAY_W / 2, DISPLAY_H / 2 + 14);
        tft.drawCentreString("Tap to cancel", DISPLAY_W / 2, DISPLAY_H / 2 + 36);
        return;
    }

    if (otaMode) {
        otaMode = false;
        otaConfirmShown = false;
        Serial.println("[OTA] Cancelled by touch");
        return;
    }

    otaConfirmShown = true;
    otaConfirmTime = now;
}

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

    checkHttpOta();
    lastOtaCheck = millis();
}

void loop()
{
    handleTouch();

    if (otaMode) {
        ArduinoOTA.handle();
        return;
    }

    if (millis() - lastOtaCheck >= OTA_CHECK_INTERVAL) {
        lastOtaCheck = millis();
        checkHttpOta();
    }

    aircraftManager.Update();

    backbuffer.fillScreen(lgfx::color888(0, 0, 0));

    String renderScanlines = configServer.GetStoredString("scanline");
    if (renderScanlines.isEmpty() || renderScanlines == "true") {
        DrawScanLines(backbuffer,
            RADAR_CENTRE,
            RADAR_CENTRE,
            RADAR_CENTRE + (std::cos(millis() / 3000.0f) * RADAR_CENTRE),
            RADAR_CENTRE + (std::sin(millis() / 3000.0f) * RADAR_CENTRE),
            20, 128, 5
        );
    }

    aircraftManager.Draw(backbuffer);

    if (otaConfirmShown && (millis() - otaConfirmTime < 5000)) {
        backbuffer.setTextSize(1.5);
        backbuffer.setTextColor(lgfx::color888(255, 220, 0));
        backbuffer.drawCentreString("Tap again for OTA", DISPLAY_W / 2, DISPLAY_H - 18);
        backbuffer.setTextSize(1);
    }

    backbuffer.pushSprite(0, 0);
}
