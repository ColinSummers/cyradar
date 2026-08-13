#pragma once

#include <WiFiManager.h>

namespace WiFiManagerHelpers
{
    constexpr const char* WiFiManagerName = "KFHR-Radar-Setup";

    static void ConfigureWiFiManager(WiFiManager& wm, LGFX& tft)
    {
        wm.setTitle("KFHR Radar - Setup WiFi");
        wm.setCustomHeadElement("<style>body{background:#111;color:#00ff00;font-family:monospace;} div:has(> a){background:#00ff00;} a:hover{color:#111;}</style>");

        wm.setAPCallback([&tft](WiFiManager* wifiManager) {
            tft.fillScreen(lgfx::color888(0, 0, 0));
            tft.setTextColor(lgfx::color888(0, 255, 0));

            const int lineHeight = tft.fontHeight() + 10;
            tft.drawCentreString("- SETUP -", 160, 100);
            tft.drawCentreString("Connect to WiFi:", 160, 100 + lineHeight);
            tft.drawCentreString(WiFiManagerName, 160, 100 + lineHeight * 2);
        });
    }
}
