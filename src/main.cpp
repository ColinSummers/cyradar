#include <Arduino.h>
#include <ArduinoJson.h>
#include <WiFiManager.h>

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

void setup()
{
    Serial.begin(115200);

    tft.init();
    tft.setRotation(1); // landscape, USB port on left

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

    tft.fillScreen(lgfx::color888(0, 0, 0));
    tft.drawCentreString("WiFi connected!", DISPLAY_W / 2, DISPLAY_H / 2 - 10);
    tft.drawCentreString(WiFi.localIP().toString(), DISPLAY_W / 2, DISPLAY_H / 2 + 10);
    delay(1500);

    configServer.Initialise();
    aircraftManager.Initialise();
}

void loop()
{
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
    backbuffer.pushSprite(0, 0);
}
