#pragma once

#include <WiFiManager.h>

namespace WiFiManagerHelpers
{
    constexpr const char* WiFiManagerName = "CYRadarWiFi";

    static void ConfigureWiFiManager(WiFiManager& wm, LGFX& tft)
    {
        wm.setTitle("CYRadar - Setup WiFi");
        wm.setCustomHeadElement("<style>body{background:#111;color:#00ff00;font-family:monospace;} div:has(> a){background:#00ff00;} a:hover{color:#111;}</style>");

        wm.setAPCallback([&tft](WiFiManager* wifiManager) {
            tft.fillScreen(lgfx::color888(0, 0, 0));
            tft.setTextColor(lgfx::color888(0, 255, 0));

            const int lineHeight = tft.fontHeight() + 10;
            int y = 80;
            tft.drawCentreString("- SETUP -", 160, y);
            tft.drawCentreString("Connect to WiFi:", 160, y + lineHeight);
            tft.drawCentreString(WiFiManagerName, 160, y + lineHeight * 2);
            tft.setTextColor(lgfx::color888(0, 128, 0));
            tft.drawCentreString("Then open 192.168.4.1", 160, y + lineHeight * 3 + 8);
        });
    }
}
