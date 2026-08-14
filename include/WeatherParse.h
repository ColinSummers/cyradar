#pragma once

#include "WeatherData.h"
#include <cstring>
#include <cstdlib>
#include <ctime>
#include <cmath>

namespace wxparse {

static const char* findKey(const char* obj, const char* key) {
    char needle[64];
    snprintf(needle, sizeof(needle), "\"%s\"", key);
    const char* p = strstr(obj, needle);
    if (!p) return nullptr;
    p += strlen(needle);
    while (*p == ' ' || *p == ':') p++;
    return p;
}

static void getString(const char* obj, const char* key, char* out, int maxLen) {
    out[0] = '\0';
    const char* p = findKey(obj, key);
    if (!p) return;
    if (*p == 'n') return; // null
    if (*p != '"') {
        // bare value (number/bool)
        int i = 0;
        while (*p && *p != ',' && *p != '}' && *p != ']' && i < maxLen - 1) {
            if (*p != ' ' && *p != '\r' && *p != '\n') out[i++] = *p;
            p++;
        }
        out[i] = '\0';
        return;
    }
    p++; // skip opening quote
    int i = 0;
    while (*p && *p != '"' && i < maxLen - 1) out[i++] = *p++;
    out[i] = '\0';
}

static float getFloat(const char* obj, const char* key) {
    char buf[32];
    getString(obj, key, buf, sizeof(buf));
    if (buf[0] == '\0') return 0.0f;
    return (float)atof(buf);
}

static int getInt(const char* obj, const char* key) {
    char buf[32];
    getString(obj, key, buf, sizeof(buf));
    if (buf[0] == '\0') return 0;
    return atoi(buf);
}

static long getLong(const char* obj, const char* key) {
    char buf[32];
    getString(obj, key, buf, sizeof(buf));
    if (buf[0] == '\0') return 0;
    return atol(buf);
}

// Find matching brace/bracket pair starting at p
static const char* findClosing(const char* p, char open, char close) {
    int depth = 0;
    for (; *p; p++) {
        if (*p == open) depth++;
        else if (*p == close) { depth--; if (depth == 0) return p; }
    }
    return nullptr;
}

// Iterate objects in a JSON array. Returns pointer to '{', sets end to '}'.
// Call with *pos = start of array ('[').
static const char* nextObject(const char** pos) {
    const char* p = *pos;
    while (*p && *p != '{') {
        if (*p == ']') return nullptr;
        p++;
    }
    if (!*p) return nullptr;
    const char* start = p;
    const char* end = findClosing(p, '{', '}');
    if (!end) return nullptr;
    *pos = end + 1;
    return start;
}

static void buildCloudString(const char* obj, char* out, int maxLen) {
    const char* p = findKey(obj, "clouds");
    if (!p || *p != '[') { strncpy(out, "Clear", maxLen); return; }

    const char* arrEnd = findClosing(p, '[', ']');
    if (!arrEnd || arrEnd - p < 5) { strncpy(out, "Clear", maxLen); return; }

    const char* scan = p + 1;
    const char* layer = nextObject(&scan);
    if (!layer) { strncpy(out, "Clear", maxLen); return; }

    char cover[8];
    getString(layer, "cover", cover, sizeof(cover));
    int base = getInt(layer, "base");

    if (strcmp(cover, "CLR") == 0 || strcmp(cover, "SKC") == 0) {
        strncpy(out, "Clear", maxLen);
        return;
    }

    const char* desc = cover;
    if (strcmp(cover, "FEW") == 0) desc = "Few";
    else if (strcmp(cover, "SCT") == 0) desc = "Scattered";
    else if (strcmp(cover, "BKN") == 0) desc = "Broken";
    else if (strcmp(cover, "OVC") == 0) desc = "Overcast";

    snprintf(out, maxLen, "%s at %d'", desc, base);
}

static int ceilingFromObject(const char* obj) {
    const char* p = findKey(obj, "clouds");
    if (!p || *p != '[') return 99999;

    const char* arrEnd = findClosing(p, '[', ']');
    if (!arrEnd) return 99999;

    int ceiling = 99999;
    const char* scan = p + 1;
    const char* layer;
    while ((layer = nextObject(&scan)) != nullptr && scan <= arrEnd) {
        char cover[8];
        getString(layer, "cover", cover, sizeof(cover));
        if (strcmp(cover, "BKN") == 0 || strcmp(cover, "OVC") == 0 || strcmp(cover, "OVX") == 0) {
            int base = getInt(layer, "base");
            if (base < ceiling) ceiling = base;
        }
    }
    return ceiling;
}

static void computeFlightCat(float vis, int ceiling, char* out, int maxLen) {
    const char* cat;
    if (ceiling < 500 || vis < 1.0f) cat = "LIFR";
    else if (ceiling < 1000 || vis < 3.0f) cat = "IFR";
    else if (ceiling <= 3000 || vis <= 5.0f) cat = "MVFR";
    else cat = "VFR";
    strncpy(out, cat, maxLen);
    out[maxLen - 1] = '\0';
}

static void timeLabel(long ts, char* out, int maxLen) {
    time_t t = ts;
    struct tm* local = localtime(&t);
    int h = local->tm_hour;
    if (h == 0) snprintf(out, maxLen, "12a");
    else if (h < 12) snprintf(out, maxLen, "%da", h);
    else if (h == 12) snprintf(out, maxLen, "12p");
    else snprintf(out, maxLen, "%dp", h - 12);
}

// Parse METAR JSON array into WxData. json should be the full response body.
static void parseMETARs(const char* json, WxData& wx) {
    wx.metarCount = 0;
    if (!json || *json != '[') return;

    const char* scan = json + 1;
    const char* obj;
    while ((obj = nextObject(&scan)) != nullptr && wx.metarCount < WX_MAX_STATIONS) {
        WxMetar& m = wx.metars[wx.metarCount];
        memset(&m, 0, sizeof(m));

        getString(obj, "icaoId", m.icao, sizeof(m.icao));
        m.windDir = getInt(obj, "wdir");
        m.windSpd = getInt(obj, "wspd");
        m.windGust = getInt(obj, "wgst");
        m.visibility = getFloat(obj, "visib");
        if (m.visibility <= 0) m.visibility = 10.0f;
        float altHpa = getFloat(obj, "altim");
        m.altimeter = (altHpa > 100) ? altHpa / 33.8639f : altHpa;
        m.tempC = getFloat(obj, "temp");
        m.dewpC = getFloat(obj, "dewp");
        getString(obj, "fltCat", m.flightCat, sizeof(m.flightCat));
        if (m.flightCat[0] == '\0') strncpy(m.flightCat, "VFR", sizeof(m.flightCat));
        getString(obj, "rawOb", m.rawOb, sizeof(m.rawOb));
        buildCloudString(obj, m.sky, sizeof(m.sky));

        wx.metarCount++;
    }
}

// Parse TAF JSON array into WxData. json should be the full response body.
// stationList is space-separated ICAOs to ensure all get entries.
static void parseTAFs(const char* json, WxData& wx, const char* stationList) {
    wx.tafCount = 0;
    if (json && *json == '[') {
        const char* scan = json + 1;
        const char* obj;
        while ((obj = nextObject(&scan)) != nullptr && wx.tafCount < WX_MAX_STATIONS) {
            WxTaf& t = wx.tafs[wx.tafCount];
            memset(&t, 0, sizeof(t));

            getString(obj, "icaoId", t.icao, sizeof(t.icao));

            const char* fcstsP = findKey(obj, "fcsts");
            if (fcstsP && *fcstsP == '[') {
                const char* fcstsEnd = findClosing(fcstsP, '[', ']');
                const char* pscan = fcstsP + 1;
                const char* period;
                while ((period = nextObject(&pscan)) != nullptr &&
                       pscan <= fcstsEnd &&
                       t.periodCount < WX_MAX_TAF_PERIODS) {
                    WxTafPeriod& tp = t.periods[t.periodCount];
                    memset(&tp, 0, sizeof(tp));

                    long from = getLong(period, "timeFrom");
                    timeLabel(from, tp.label, sizeof(tp.label));

                    char change[16];
                    getString(period, "fcstChange", change, sizeof(change));
                    tp.isTempo = (strcmp(change, "TEMPO") == 0);

                    char visStr[16];
                    getString(period, "visib", visStr, sizeof(visStr));
                    float vis = 10.0f;
                    if (strcmp(visStr, "6+") == 0 || strcmp(visStr, "P6SM") == 0) vis = 7.0f;
                    else if (visStr[0] != '\0') vis = (float)atof(visStr);

                    int ceiling = ceilingFromObject(period);
                    computeFlightCat(vis, ceiling, tp.flightCat, sizeof(tp.flightCat));

                    t.periodCount++;
                }
            }
            wx.tafCount++;
        }
    }

    // Add empty entries for stations with no TAF
    if (stationList) {
        char buf[256];
        strncpy(buf, stationList, sizeof(buf) - 1);
        buf[sizeof(buf) - 1] = '\0';
        char* token = strtok(buf, " ");
        while (token && wx.tafCount < WX_MAX_STATIONS) {
            bool found = false;
            for (int i = 0; i < wx.tafCount; i++)
                if (strcmp(wx.tafs[i].icao, token) == 0) { found = true; break; }
            if (!found) {
                WxTaf& t = wx.tafs[wx.tafCount];
                memset(&t, 0, sizeof(t));
                strncpy(t.icao, token, sizeof(t.icao) - 1);
                wx.tafCount++;
            }
            token = strtok(nullptr, " ");
        }
    }
}

} // namespace wxparse
