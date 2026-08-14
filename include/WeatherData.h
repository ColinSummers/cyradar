#pragma once

#include <cstring>

static constexpr int WX_MAX_STATIONS = 12;
static constexpr int WX_MAX_TAF_PERIODS = 16;

struct WxMetar {
    char icao[8];
    int windDir;
    int windSpd;
    int windGust;
    float visibility;
    char sky[48];
    float tempC;
    float dewpC;
    float altimeter;
    char flightCat[8];
    char rawOb[160];
};

struct WxTafPeriod {
    char flightCat[8];
    char label[8];
    bool isTempo;
};

struct WxTaf {
    char icao[8];
    WxTafPeriod periods[WX_MAX_TAF_PERIODS];
    int periodCount;
};

struct WxData {
    WxMetar metars[WX_MAX_STATIONS];
    int metarCount;
    WxTaf tafs[WX_MAX_STATIONS];
    int tafCount;
    unsigned long fetchTime;

    const WxMetar* findMetar(const char* icao) const {
        for (int i = 0; i < metarCount; i++)
            if (strcmp(metars[i].icao, icao) == 0) return &metars[i];
        return nullptr;
    }

    const char* findMetarCat(const char* icao) const {
        const WxMetar* m = findMetar(icao);
        return m ? m->flightCat : "???";
    }

    const WxTaf* findTaf(const char* icao) const {
        for (int i = 0; i < tafCount; i++)
            if (strcmp(tafs[i].icao, icao) == 0) return &tafs[i];
        return nullptr;
    }
};
