#include <Arduino.h>
#include <ArduinoJson.h>
#include <WiFiManager.h>
#include <ArduinoOTA.h>

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

void beginOTA()
{
    otaMode = true;

    ArduinoOTA.setHostname("kfhr-radar");
    ArduinoOTA.onStart([]() {
        tft.fillScreen(lgfx::color888(0, 0, 0));
        tft.setTextColor(lgfx::color888(0, 255, 0));
        tft.drawCentreString("OTA Updating...", DISPLAY_W / 2, DISPLAY_H / 2 - 10);
    });
    ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
        int pct = progress / (total / 100);
        tft.fillRect(60, DISPLAY_H / 2 + 10, 200, 12, lgfx::color888(0, 0, 0));
        tft.drawRect(60, DISPLAY_H / 2 + 10, 200, 12, lgfx::color888(0, 200, 0));
        tft.fillRect(60, DISPLAY_H / 2 + 10, pct * 2, 12, lgfx::color888(0, 255, 0));
    });
    ArduinoOTA.onEnd([]() {
        tft.fillScreen(lgfx::color888(0, 0, 0));
        tft.drawCentreString("Update complete!", DISPLAY_W / 2, DISPLAY_H / 2);
    });
    ArduinoOTA.onError([](ota_error_t error) {
        otaMode = false;
        Serial.printf("[OTA] Error %u\n", error);
    });
    ArduinoOTA.begin();
    Serial.println("[OTA] Listening for update...");
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
        beginOTA();

        tft.fillScreen(lgfx::color888(0, 0, 0));
        tft.setTextColor(lgfx::color888(0, 255, 0));
        tft.drawCentreString("OTA Ready", DISPLAY_W / 2, DISPLAY_H / 2 - 20);
        tft.drawCentreString("Upload from PlatformIO:", DISPLAY_W / 2, DISPLAY_H / 2);
        tft.setTextColor(lgfx::color888(0, 160, 0));
        String cmd = "pio run -t upload --upload-port " + WiFi.localIP().toString();
        tft.drawCentreString(cmd, DISPLAY_W / 2, DISPLAY_H / 2 + 20);
        tft.drawCentreString("Tap again to cancel", DISPLAY_W / 2, DISPLAY_H / 2 + 40);
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
    tft.setTextColor(lgfx::color888(0, 255, 0));
    tft.drawCentreString("KFHR Radar", DISPLAY_W / 2, DISPLAY_H / 2 - 10);
    tft.drawCentreString("Connecting to WiFi...", DISPLAY_W / 2, DISPLAY_H / 2 + 10);

    WiFiManagerHelpers::ConfigureWiFiManager(wm, tft);

    if (strlen(WIFI_SSID) > 0) {
        WiFi.begin(WIFI_SSID, WIFI_PASS);
        WiFi.waitForConnectResult();
    }

    wm.autoConnect(WiFiManagerHelpers::WiFiManagerName);

    Serial.print("[INFO] WiFi connected, IP: ");
    Serial.println(WiFi.localIP());

    tft.fillScreen(lgfx::color888(0, 0, 0));
    tft.drawCentreString("WiFi connected!", DISPLAY_W / 2, DISPLAY_H / 2 - 10);
    tft.drawCentreString(WiFi.localIP().toString(), DISPLAY_W / 2, DISPLAY_H / 2 + 10);
    delay(2500);

    configServer.Initialise();
    aircraftManager.Initialise();
}

void loop()
{
    handleTouch();

    if (otaMode) {
        ArduinoOTA.handle();
        return;
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
        backbuffer.setTextColor(lgfx::color888(255, 220, 0));
        backbuffer.drawCentreString("Tap again for OTA", DISPLAY_W / 2, DISPLAY_H - 14);
    }

    backbuffer.pushSprite(0, 0);
}
