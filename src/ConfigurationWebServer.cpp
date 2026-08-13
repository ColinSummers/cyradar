#include "ConfigurationWebServer.h"
#include "Config.h"
#include <ESPmDNS.h>

static const char CONFIG_HTML[] PROGMEM = R"(
<html>
    <head>
        <meta name="viewport" content="width=device-width, initial-scale=1">
        <title>Configure KFHR Radar</title>
        <style>
            body { font-family: monospace; background: #111; color: #0f0; padding: 1em; }
            fieldset { border: 1px solid #0f0; padding: 1.5em; max-width: 40em; margin: auto; }
            legend { padding: 0 0.5em; }
            label { display: flex; gap: 0.5em; margin: 0.5em 0; align-items: center; flex-wrap: wrap; }
            label span { min-width: 12em; }
            input[type="number"], input[type="text"] { flex: 1; background: #111; border: 1px solid #0f0; color: #0f0; padding: 0.3em; font-family: monospace; }
            input[type="checkbox"] { accent-color: #0f0; }
            input[type="submit"] { background: #0f0; color: #000; border: none; padding: 0.5em 1em; cursor: pointer; font-family: monospace; margin-top: 1em; }
            .row { display: flex; gap: 1em; flex-wrap: wrap; }
            #result { margin-top: 0.5em; }
        </style>
    </head>
    <body>
        <fieldset>
            <legend>KFHR Radar Config</legend>
            <form id="cfg" action="/save" method="POST">
                <div class="row">
                    <label><span>Latitude:</span>
                        <input name="latitude" type="number" min="-90" step="0.000001" max="90" value='%LATITUDE%'>
                    </label>
                    <label><span>Longitude:</span>
                        <input name="longitude" type="number" min="-180" step="0.000001" max="180" value='%LONGITUDE%'>
                    </label>
                </div>
                <label><span>Radius (&deg;):</span>
                    <input name="radius" type="number" min="0.01" step="0.01" max="2.5" value='%RADIUS%'>
                </label>
                <label><span>OpenSky Client ID:</span>
                    <input name="opensky-id" value='%OPENSKY_ID%'>
                </label>
                <label><span>OpenSky Client Secret:</span>
                    <input name="opensky-secret" value='%OPENSKY_SECRET%'>
                </label>
                <label><span>Known tails:</span>
                    <input name="knowntails" type="text" value='%KNOWNTAILS%' placeholder="N12345, N67890">
                </label>
                <div class="row">
                    <label><span>Radar sweep:</span><input name="scanline" type="checkbox" %SCANLINE%></label>
                    <label><span>Aircraft info:</span><input name="infotext" type="checkbox" %INFOTEXT%></label>
                    <label><span>Directional:</span><input name="triangle" type="checkbox" %TRIANGLE%></label>
                </div>
                <input type="submit" value="Save &amp; Restart">
                <div id="result"></div>
            </form>
        </fieldset>
        <script>
            document.getElementById('cfg').addEventListener('submit', function(e) {
                e.preventDefault();
                fetch(this.action, { method: 'POST', body: new FormData(this) })
                    .then(r => r.text())
                    .then(html => document.getElementById('result').innerHTML = html);
            });
        </script>
    </body>
</html>
)";

void ConfigurationWebServer::ApplyDefaults() {
    prefs.begin("config", false);

    if (prefs.getString("latitude", "").isEmpty())
        prefs.putString("latitude", String(DEFAULT_LATITUDE, 4));
    if (prefs.getString("longitude", "").isEmpty())
        prefs.putString("longitude", String(DEFAULT_LONGITUDE, 4));
    if (prefs.getString("radius", "").isEmpty())
        prefs.putString("radius", String(DEFAULT_RADIUS, 2));
    if (prefs.getString("opensky-id", "").isEmpty() && strlen(OPENSKY_CLIENT_ID) > 0)
        prefs.putString("opensky-id", OPENSKY_CLIENT_ID);
    if (prefs.getString("opensky-secret", "").isEmpty() && strlen(OPENSKY_CLIENT_SECRET) > 0)
        prefs.putString("opensky-secret", OPENSKY_CLIENT_SECRET);
    if (prefs.getString("knowntails", "").isEmpty())
        prefs.putString("knowntails", "N80117, N2939J");
    if (prefs.getString("scanline", "").isEmpty())
        prefs.putString("scanline", "true");
    if (prefs.getString("infotext", "").isEmpty())
        prefs.putString("infotext", "true");
    if (prefs.getString("triangle", "").isEmpty())
        prefs.putString("triangle", "true");

    prefs.end();
}

void ConfigurationWebServer::Initialise() {
    ApplyDefaults();

    if (!MDNS.begin("kfhr-radar")) {
        Serial.println("[WARN] Failed to start mDNS");
    }

    server.on("/", HTTP_GET, [&](AsyncWebServerRequest* request) {
        prefs.begin("config", true);
        const String latitude        = prefs.getString("latitude", "");
        const String longitude       = prefs.getString("longitude", "");
        const String radius          = prefs.getString("radius", "0.5");
        const String openskyClientId = prefs.getString("opensky-id", "");
        String openskySecret         = prefs.getString("opensky-secret", "");
        const String knownTails      = prefs.getString("knowntails", "");
        const String scanlineEnabled = prefs.getString("scanline", "true");
        const String infoTextEnabled = prefs.getString("infotext", "true");
        const String triangleEnabled = prefs.getString("triangle", "true");
        prefs.end();

        std::fill(openskySecret.begin(), openskySecret.end(), '*');

        AsyncWebServerResponse* response = request->beginResponse(
            200, "text/html",
            (const uint8_t*)CONFIG_HTML, sizeof(CONFIG_HTML) - 1,
            [latitude, longitude, radius, openskyClientId, openskySecret, knownTails, scanlineEnabled, infoTextEnabled, triangleEnabled]
            (const String& var) -> String {
                if (var == "LATITUDE")       return latitude;
                if (var == "LONGITUDE")      return longitude;
                if (var == "RADIUS")         return radius;
                if (var == "OPENSKY_ID")     return openskyClientId;
                if (var == "OPENSKY_SECRET") return openskySecret;
                if (var == "KNOWNTAILS")     return knownTails;
                if (var == "SCANLINE")       return scanlineEnabled == "true" ? "checked" : "";
                if (var == "INFOTEXT")       return infoTextEnabled == "true" ? "checked" : "";
                if (var == "TRIANGLE")       return triangleEnabled == "true" ? "checked" : "";
                return "";
            }
        );
        request->send(response);
    });

    server.on("/save", HTTP_POST, [&](AsyncWebServerRequest* request) {
        auto TrySaveParam = [request, this](const char* paramName) {
            const auto* param = request->getParam(paramName, true);
            if (param == nullptr) return false;
            prefs.putString(paramName, param->value());
            return true;
        };

        prefs.begin("config", false);

        TrySaveParam("latitude");
        TrySaveParam("longitude");
        TrySaveParam("radius");
        TrySaveParam("opensky-id");
        TrySaveParam("knowntails");

        const auto* param = request->getParam("opensky-secret", true);
        if (param != nullptr) {
            const String& secret = param->value();
            if (secret.indexOf('*') == -1) {
                prefs.putString("opensky-secret", secret);
            }
        }

        prefs.putString("scanline", request->hasParam("scanline", true) ? "true" : "false");
        prefs.putString("triangle", request->hasParam("triangle", true) ? "true" : "false");
        prefs.putString("infotext", request->hasParam("infotext", true) ? "true" : "false");
        prefs.end();

        request->send(200, "text/html", "Saved - restarting...");
        shouldRestart = true;
        restartAt = millis() + 500;
    });

    server.begin();
}

const String ConfigurationWebServer::GetStoredString(const char* key)
{
    prefs.begin("config", true);
    const String value = prefs.getString(key, "");
    prefs.end();
    return value;
}
