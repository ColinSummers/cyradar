#pragma once

#include <ESPAsyncWebServer.h>
#include <Preferences.h>

class ConfigurationWebServer {
private:
    AsyncWebServer server;
    Preferences prefs;

public:
    bool shouldRestart = false;
    unsigned long restartAt = 0;
    unsigned long configActiveUntil = 0;

    ConfigurationWebServer(int port = 80) : server(port), prefs() {}

    void Initialise();
    void ApplyDefaults();
    [[nodiscard]] const String GetStoredString(const char* key);
};
