#pragma once

#include <ESPAsyncWebServer.h>
#include <Preferences.h>

class ConfigurationWebServer {
private:
    AsyncWebServer server;
    AsyncWebServer altServer;
    void registerRoutes(AsyncWebServer& s);

public:
    bool shouldRestart = false;
    unsigned long restartRequestedAt = 0;
    bool configActive = false;
    unsigned long configTouchedAt = 0;

    ConfigurationWebServer() : server(80), altServer(8080) {}

    void Initialise();
    void ApplyDefaults();
    [[nodiscard]] const String GetStoredString(const char* key);
};
