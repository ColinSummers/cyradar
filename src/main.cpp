#include <Arduino.h>
#include <ArduinoJson.h>
#include <WiFiManager.h>
#include <HTTPClient.h>
#include <HTTPUpdate.h>
#include <WiFiClientSecure.h>

#include "Config.h"
#include "RadarLayout.h"
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

void handleTouch()
{
    lgfx::touch_point_t tp;
    if (!tft.getTouch(&tp)) return;

    unsigned long now = millis();
    if (now - lastTouchTime < 500) return;
    lastTouchTime = now;

    if (tp.x >= RADAR_SIZE) return;

    if (tp.x < RADAR_SIZE / 2 && tp.y < RADAR_SIZE / 2) {
        double d = aircraftManager.GetDiameterNm();
        d *= 2.0 / 3.0;
        if (d < 2) d = 2;
        aircraftManager.SetDiameterNm(d);
        Serial.printf("[TOUCH] Zoom in -> %.1f nm\n", d);
    } else if (tp.x >= RADAR_SIZE / 2 && tp.y < RADAR_SIZE / 2) {
        Serial.println("[TOUCH] METAR detail (not yet implemented)");
    } else if (tp.x < RADAR_SIZE / 2 && tp.y >= RADAR_SIZE / 2) {
        double d = aircraftManager.GetDiameterNm();
        d *= 3.0 / 2.0;
        if (d > 50) d = 50;
        aircraftManager.SetDiameterNm(d);
        Serial.printf("[TOUCH] Zoom out -> %.1f nm\n", d);
    } else {
        Serial.println("[TOUCH] Weather map (not yet implemented)");
    }
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

    String scanlinePref = configServer.GetStoredString("scanline");
    renderScanlines = scanlinePref.isEmpty() || scanlinePref == "true";

    lastOtaCheck = millis();
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

    backbuffer.pushSprite(0, 0);
}
