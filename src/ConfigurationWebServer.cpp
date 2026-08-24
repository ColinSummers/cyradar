#include "ConfigurationWebServer.h"
#include "Config.h"
#include <ESPmDNS.h>

static const char CONFIG_HTML[] PROGMEM = R"html(
<html>
<head>
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <title>Radar Config</title>
    <style>
        body{font-family:monospace;background:#111;color:#0f0;padding:1em}
        fieldset{border:1px solid #0f0;padding:1em;max-width:44em;margin:0 auto 1em}
        legend{padding:0 .5em}
        label{display:flex;gap:.5em;margin:.4em 0;align-items:center;flex-wrap:wrap}
        label span{min-width:10em}
        input[type=number],input[type=text]{flex:1;background:#111;border:1px solid #0f0;color:#0f0;padding:.3em;font-family:monospace}
        textarea{width:100%%;background:#111;border:1px solid #0f0;color:#0f0;padding:.3em;font-family:monospace;resize:vertical;box-sizing:border-box}
        input[type=checkbox]{accent-color:#0f0}
        button,input[type=submit]{background:#0f0;color:#000;border:none;padding:.4em .8em;cursor:pointer;font-family:monospace}
        .row{display:flex;gap:1em;flex-wrap:wrap}
        .err{color:#f00}
        #result{margin-top:.5em}
        .apt-row{display:flex;gap:.5em;align-items:center;margin-bottom:.5em}
        .apt-row input{max-width:6em;text-transform:uppercase}
        .sub{color:#080;font-size:.9em;margin:.3em 0 .2em}
    </style>
</head>
<body>
<form id="cfg" action="/save" method="POST">
    <fieldset>
        <legend>Airport</legend>
        <div class="apt-row">
            <input name="airport" type="text" value="%AIRPORT%" maxlength="4">
            <button type="button" id="btn-lookup">Lookup</button>
            <span id="ls"></span>
        </div>
        <div class="row">
            <label><span>Latitude:</span>
                <input name="latitude" type="number" min="-90" step="0.0001" max="90" value="%LATITUDE%">
            </label>
            <label><span>Longitude:</span>
                <input name="longitude" type="number" min="-180" step="0.0001" max="180" value="%LONGITUDE%">
            </label>
        </div>
        <label><span>Ignore above (ft MSL):</span>
            <input name="maxalt" type="number" min="500" step="500" max="60000" value="%MAXALT%">
        </label>
        <label><span>Class D radius (nm):</span>
            <input name="classd" type="number" min="0" step="0.5" max="10" value="%CLASSD%">
        </label>
    </fieldset>

    <input name="runways" type="hidden" value="%RUNWAYS_ESC%">

    <fieldset>
        <legend>Radar Display</legend>
        <label><span>Diameter (nm):</span>
            <input name="diameter" type="number" min="1" step="1" max="100" value="%DIAMETER%">
        </label>
        <div class="row">
            <label><span>Sweep:</span><input name="scanline" type="checkbox" %SCANLINE%></label>
            <label><span>Info text:</span><input name="infotext" type="checkbox" %INFOTEXT%></label>
        </div>
    </fieldset>

    <fieldset>
        <legend>Weather Stations</legend>
        <div class="sub">METAR map (space-separated ICAO):</div>
        <textarea name="metars" rows="2">%METARS%</textarea>
        <div class="sub">TAF grid (8 stations, top row then bottom row):</div>
        <textarea name="tafs" rows="2">%TAFS%</textarea>
    </fieldset>

    <fieldset>
        <legend>OpenSky</legend>
        <label><span>Client ID:</span>
            <input name="opensky-id" value="%OPENSKY_ID%">
        </label>
        <label><span>Client Secret:</span>
            <input name="opensky-secret" value="%OPENSKY_SECRET%">
        </label>
    </fieldset>

    <fieldset>
        <legend>Known Tails</legend>
        <textarea name="knowntails" rows="4" placeholder="N12345 N67890(John)">%KNOWNTAILS%</textarea>
    </fieldset>

    <fieldset>
        <legend>About</legend>
        <div class="sub">CYRadar v%FW_VER%</div>
        <div class="sub">Built %BUILD_DATE% %BUILD_TIME%</div>
        <div class="sub"><a href="https://github.com/ColinSummers/cyradar" style="color:#0a0">github.com/ColinSummers/cyradar</a></div>
    </fieldset>

    <input type="submit" value="Save &amp; Restart" style="margin-top:.5em">
    <div id="result"></div>
</form>
<script>
document.getElementById('btn-lookup').addEventListener('click', function(){
    var id=document.querySelector('[name=airport]').value.toUpperCase().trim();
    var s=document.getElementById('ls');
    if(id.length<2){s.textContent='Enter identifier';s.className='err';return;}
    s.textContent='Looking up...';s.className='';

    fetch('https://davidmegginson.github.io/ourairports-data/runways.csv')
    .then(function(r){return r.text()})
    .then(function(csv){
        var lines=csv.split('\n');
        var hdr=lines[0].split(',').map(function(c){return c.replace(/"/g,'')});
        var ci=function(n){return hdr.indexOf(n)};
        var found=[];
        var apLat=0,apLon=0;
        for(var i=1;i<lines.length;i++){
            var cols=lines[i].split(',').map(function(c){return c.replace(/"/g,'')});
            if(cols[ci('airport_ident')]!==id)continue;
            if(cols[ci('closed')]==='1')continue;
            var le=cols[ci('le_ident')],he=cols[ci('he_ident')];
            var h1=parseFloat(cols[ci('le_heading_degT')]);
            var h2=parseFloat(cols[ci('he_heading_degT')]);
            var lat1=parseFloat(cols[ci('le_latitude_deg')]);
            var lon1=parseFloat(cols[ci('le_longitude_deg')]);
            var lat2=parseFloat(cols[ci('he_latitude_deg')]);
            var lon2=parseFloat(cols[ci('he_longitude_deg')]);
            if(isNaN(h1)||isNaN(h2))continue;
            found.push({id:le+'/'+he,h1:h1,h2:h2,lat1:lat1,lon1:lon1,lat2:lat2,lon2:lon2});
            if(!isNaN(lat1)&&!isNaN(lat2)){apLat=(lat1+lat2)/2;apLon=(lon1+lon2)/2;}
        }
        document.querySelector('[name=runways]').value=found.length?JSON.stringify(found):'[]';
        if(apLat&&apLon){
            document.querySelector('[name=latitude]').value=apLat.toFixed(4);
            document.querySelector('[name=longitude]').value=apLon.toFixed(4);
            s.textContent=found.length?found.length+' rwy':'Found (no runways)';s.className='';
        } else {
            s.textContent='Not found in runway DB';s.className='err';
        }
    })
    .catch(function(){s.textContent='Lookup failed - enter coords manually';s.className='err'});
});

document.getElementById('cfg').addEventListener('submit', function(e){
    e.preventDefault();
    var lat=document.querySelector('[name=latitude]').value;
    var lon=document.querySelector('[name=longitude]').value;
    if(!lat||!lon||lat==='0'||lon==='0'){
        document.getElementById('result').innerHTML='<span class="err">Latitude/longitude required. Use Lookup or enter manually.</span>';
        return;
    }
    fetch(this.action,{method:'POST',body:new FormData(this)})
    .then(function(r){return r.text()})
    .then(function(h){document.getElementById('result').innerHTML=h});
});
</script>
</body>
</html>
)html";

static String htmlEscape(const String& s) {
    String out;
    out.reserve(s.length() + 16);
    for (unsigned i = 0; i < s.length(); i++) {
        char c = s[i];
        if (c == '"') out += "&quot;";
        else if (c == '&') out += "&amp;";
        else if (c == '<') out += "&lt;";
        else if (c == '>') out += "&gt;";
        else out += c;
    }
    return out;
}

void ConfigurationWebServer::ApplyDefaults() {
    prefs.begin("config", false);

    if (prefs.getString("airport", "").isEmpty())
        prefs.putString("airport", "KFHR");
    if (prefs.getString("latitude", "").isEmpty())
        prefs.putString("latitude", String(DEFAULT_LATITUDE, 4));
    if (prefs.getString("longitude", "").isEmpty())
        prefs.putString("longitude", String(DEFAULT_LONGITUDE, 4));
    if (prefs.getString("diameter", "").isEmpty())
        prefs.putString("diameter", String(DEFAULT_DIAMETER_NM));
    if (prefs.getString("maxalt", "").isEmpty())
        prefs.putString("maxalt", "8000");
    if (prefs.getString("opensky-id", "").isEmpty() && strlen(OPENSKY_CLIENT_ID) > 0)
        prefs.putString("opensky-id", OPENSKY_CLIENT_ID);
    if (prefs.getString("opensky-secret", "").isEmpty() && strlen(OPENSKY_CLIENT_SECRET) > 0)
        prefs.putString("opensky-secret", OPENSKY_CLIENT_SECRET);
    if (prefs.getString("knowntails", "").isEmpty())
        prefs.putString("knowntails", "N80117 N2939J");
    if (prefs.getString("metars", "").isEmpty())
        prefs.putString("metars", "KFHR KNUW KPAE KBFI KBVS KBLI KORS");
    if (prefs.getString("tafs", "").isEmpty())
        prefs.putString("tafs", "KFHR KNUW KPAE KBFI KBVS KBLI KORS KCLM");
    if (prefs.getString("runways", "").isEmpty())
        prefs.putString("runways", "[{\"id\":\"16/34\",\"h1\":177,\"h2\":357,\"lat1\":48.5266,\"lon1\":-123.025,\"lat2\":48.5173,\"lon2\":-123.024}]");
    if (prefs.getString("scanline", "").isEmpty())
        prefs.putString("scanline", "true");
    if (prefs.getString("infotext", "").isEmpty())
        prefs.putString("infotext", "true");
    prefs.end();
}

void ConfigurationWebServer::Initialise() {
    ApplyDefaults();

    if (!MDNS.begin("cyradar")) {
        Serial.println("[WARN] Failed to start mDNS");
    }

    server.on("/", HTTP_GET, [&](AsyncWebServerRequest* request) {
        configActiveUntil = millis() + 60000;
        prefs.begin("config", true);
        const String airport         = prefs.getString("airport", "KFHR");
        const String latitude        = prefs.getString("latitude", "");
        const String longitude       = prefs.getString("longitude", "");
        const String diameter        = prefs.getString("diameter", "8");
        const String maxalt          = prefs.getString("maxalt", "8000");
        const String classd          = prefs.getString("classd", "0");
        const String openskyClientId = prefs.getString("opensky-id", "");
        String openskySecret         = prefs.getString("opensky-secret", "");
        const String knownTails      = prefs.getString("knowntails", "");
        const String metars          = prefs.getString("metars", "");
        const String tafs            = prefs.getString("tafs", "");
        const String runwaysJson     = prefs.getString("runways", "[]");
        const String scanlineEnabled = prefs.getString("scanline", "true");
        const String infoTextEnabled = prefs.getString("infotext", "true");
        prefs.end();

        std::fill(openskySecret.begin(), openskySecret.end(), '*');
        const String runwaysEsc = htmlEscape(runwaysJson);

        AsyncWebServerResponse* response = request->beginResponse(
            200, "text/html",
            (const uint8_t*)CONFIG_HTML, sizeof(CONFIG_HTML) - 1,
            [airport, latitude, longitude, diameter, maxalt, classd, openskyClientId, openskySecret,
             knownTails, metars, tafs, runwaysEsc,
             scanlineEnabled, infoTextEnabled]
            (const String& var) -> String {
                if (var == "AIRPORT")        return airport;
                if (var == "LATITUDE")       return latitude;
                if (var == "LONGITUDE")      return longitude;
                if (var == "DIAMETER")       return diameter;
                if (var == "MAXALT")         return maxalt;
                if (var == "CLASSD")         return classd;
                if (var == "OPENSKY_ID")     return openskyClientId;
                if (var == "OPENSKY_SECRET") return openskySecret;
                if (var == "KNOWNTAILS")     return knownTails;
                if (var == "METARS")         return metars;
                if (var == "TAFS")           return tafs;
                if (var == "RUNWAYS_ESC")    return runwaysEsc;
                if (var == "SCANLINE")       return scanlineEnabled == "true" ? "checked" : "";
                if (var == "INFOTEXT")       return infoTextEnabled == "true" ? "checked" : "";
                if (var == "FW_VER")         return FW_VERSION;
                if (var == "BUILD_DATE")     return __DATE__;
                if (var == "BUILD_TIME")     return __TIME__;
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

        TrySaveParam("airport");
        TrySaveParam("latitude");
        TrySaveParam("longitude");
        TrySaveParam("diameter");
        TrySaveParam("maxalt");
        TrySaveParam("classd");
        TrySaveParam("opensky-id");
        TrySaveParam("knowntails");
        TrySaveParam("metars");
        TrySaveParam("tafs");
        TrySaveParam("runways");

        const auto* param = request->getParam("opensky-secret", true);
        if (param != nullptr) {
            const String& secret = param->value();
            if (secret.indexOf('*') == -1) {
                prefs.putString("opensky-secret", secret);
            }
        }

        prefs.putString("scanline", request->hasParam("scanline", true) ? "true" : "false");
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
