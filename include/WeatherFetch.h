#pragma once

#include "WeatherData.h"
#include "WeatherParse.h"
#include "HttpFetch.h"
#include <cstdio>
#include <string>

namespace wxfetch {

static void buildUrl(char* url, int maxLen, const char* endpoint, const char* stations) {
    char encoded[128];
    int j = 0;
    for (int i = 0; stations[i] && j < (int)sizeof(encoded) - 1; i++) {
        if (stations[i] == ' ') {
            if (j > 0 && encoded[j-1] != ',') encoded[j++] = ',';
        } else {
            encoded[j++] = stations[i];
        }
    }
    encoded[j] = '\0';
    snprintf(url, maxLen, "https://aviationweather.gov/api/data/%s?ids=%s&format=json",
             endpoint, encoded);
}

static void fetchAll(WxData& wx, const char* metarStations, const char* tafStations,
                     unsigned long nowMs)
{
    char url[256];

    buildUrl(url, sizeof(url), "metar", metarStations);
    std::string metarJson = httpfetch::get(url);
    if (!metarJson.empty())
        wxparse::parseMETARs(metarJson.c_str(), wx);

    buildUrl(url, sizeof(url), "taf", tafStations);
    std::string tafJson = httpfetch::get(url);
    wxparse::parseTAFs(tafJson.empty() ? "[]" : tafJson.c_str(), wx, tafStations);

    wx.fetchTime = nowMs;
}

}
