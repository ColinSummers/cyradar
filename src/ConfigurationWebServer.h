#pragma once

#include <ESPAsyncWebServer.h>
#include <Preferences.h>

class ConfigurationWebServer {
private:
    AsyncWebServer server;
    AsyncWebServer altServer;
    Preferences prefs;
    void registerRoutes(AsyncWebServer& s);

public:
    bool shouldRestart = false;
    unsigned long restartAt = 0;
    unsigned long configActiveUntil = 0;

    ConfigurationWebServer() : server(80), altServer(8080), prefs() {}

    void Initialise();
    void ApplyDefaults();
    [[nodiscard]] const String GetStoredString(const char* key);
};
