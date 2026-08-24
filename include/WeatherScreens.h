#pragma once

#include <LovyanGFX.hpp>
#include "WeatherData.h"
#include "Overlays.h"
#include "RadarLayout.h"

static uint32_t wxFlightCatColor(const char* cat) {
    if (strcmp(cat, "VFR") == 0)  return lgfx::color888(0, 220, 0);
    if (strcmp(cat, "MVFR") == 0) return lgfx::color888(0, 140, 255);
    if (strcmp(cat, "IFR") == 0)  return lgfx::color888(255, 100, 180);
    if (strcmp(cat, "LIFR") == 0) return lgfx::color888(255, 0, 0);
    return lgfx::color888(128, 128, 128);
}

static void wxDrawMetarDetail(LGFX_Sprite& bb, const WxData& wx,
                              const char* airportId,
                              const RunwayInfo* runways, int runwayCount,
                              const char* versionStr, unsigned long nowMs)
{
    bb.fillScreen(0);

    const WxMetar* m = wx.findMetar(airportId);
    if (!m && wx.metarCount > 0) m = &wx.metars[0];
    if (!m) return;

    uint32_t green = lgfx::color888(0, 200, 0);
    uint32_t dim   = lgfx::color888(0, 100, 0);

    bb.setTextSize(2);
    bb.setTextColor(green);
    bb.drawString(airportId, 10, 8);
    bb.setTextColor(wxFlightCatColor(m->flightCat));
    bb.drawString(m->flightCat, 110, 8);

    bb.setTextSize(1);
    bb.setTextColor(green);

    char buf[80];
    int y = 38;
    const int lh = 14;

    if (m->windSpd == 0)
        snprintf(buf, sizeof(buf), "Wind    Calm");
    else if (m->windGust > 0)
        snprintf(buf, sizeof(buf), "Wind    %03d at %d G%d kt", m->windDir, m->windSpd, m->windGust);
    else
        snprintf(buf, sizeof(buf), "Wind    %03d at %d kt", m->windDir, m->windSpd);
    bb.drawString(buf, 10, y); y += lh;

    if (m->visibility >= 10.0f)
        snprintf(buf, sizeof(buf), "Vis     10+ SM");
    else
        snprintf(buf, sizeof(buf), "Vis     %.0f SM", m->visibility);
    bb.drawString(buf, 10, y); y += lh;

    snprintf(buf, sizeof(buf), "Sky     %s", m->sky);
    bb.drawString(buf, 10, y); y += lh;

    float tempF = m->tempC * 9.0f / 5.0f + 32.0f;
    float dewF  = m->dewpC * 9.0f / 5.0f + 32.0f;
    snprintf(buf, sizeof(buf), "Temp    %.0fF  Dew %.0fF", tempF, dewF);
    bb.drawString(buf, 10, y); y += lh;

    snprintf(buf, sizeof(buf), "Altim   %.2f\"", m->altimeter);
    bb.drawString(buf, 10, y); y += lh;

    if (m->windSpd > 0) {
        for (int r = 0; r < runwayCount; r++) {
            float a1 = fmodf(m->windDir - runways[r].heading1 + 360.0f, 360.0f);
            if (a1 > 180.0f) a1 -= 360.0f;
            float a2 = fmodf(m->windDir - runways[r].heading2 + 360.0f, 360.0f);
            if (a2 > 180.0f) a2 -= 360.0f;

            int rwyNum;
            float angle;
            if (fabsf(a1) <= 90.0f) {
                rwyNum = (int)(runways[r].heading1 / 10.0f + 0.5f);
                angle = a1;
            } else {
                rwyNum = (int)(runways[r].heading2 / 10.0f + 0.5f);
                angle = a2;
            }

            float rad = angle * M_PI / 180.0f;
            int hw = (int)(m->windSpd * cosf(rad) + 0.5f);
            int xw = (int)(fabsf(m->windSpd * sinf(rad)) + 0.5f);
            snprintf(buf, sizeof(buf), "Rwy %-3d %d hw  %d xw", rwyNum, hw, xw);
            bb.drawString(buf, 10, y);
            y += lh;
        }
    }
    y += 6;

    bb.setTextColor(dim);
    const char* raw = m->rawOb;
    int rawLen = strlen(raw);
    const int maxChars = (DISPLAY_W - 20) / 6;
    if (rawLen > maxChars) {
        int splitAt = -1;
        for (int i = maxChars; i >= maxChars / 2; i--) {
            if (raw[i] == ' ') { splitAt = i; break; }
        }
        if (splitAt < 0) splitAt = maxChars;
        char line1[128];
        strncpy(line1, raw, splitAt); line1[splitAt] = '\0';
        bb.drawString(line1, 10, y); y += 10;
        bb.drawString(raw + splitAt + 1, 10, y);
    } else {
        bb.drawString(raw, 10, y);
    }

#ifdef BOARD_FREENOVE_S3
    bb.drawRect(156, 198, 56, 32, lgfx::color888(0, 100, 0));
    bb.setTextColor(lgfx::color888(0, 160, 0));
    bb.drawString("Ping", 166, 206);
#endif
    bb.drawRect(218, 198, 96, 32, lgfx::color888(0, 100, 0));
    bb.setTextColor(lgfx::color888(0, 160, 0));
    bb.drawString("FW Update", 224, 206);

    int ageMin = (int)((nowMs - wx.fetchTime) / 60000);
    snprintf(buf, sizeof(buf), "WX: %d min ago", ageMin);
    bb.setTextColor(lgfx::color888(0, 80, 0));
    bb.drawString(buf, 4, DISPLAY_H - 22);
    bb.setTextColor(lgfx::color888(0, 120, 0));
    bb.drawString(versionStr, 4, DISPLAY_H - 10);
}

static void wxDrawWeatherMap(LGFX_Sprite& bb, const WxData& wx, unsigned long nowMs)
{
    bb.fillScreen(0);

    const float latMin = 47.40f, latMax = 48.90f;
    const float lonMin = -123.60f, lonMax = -122.10f;
    const int mapTop = 14, mapBot = DISPLAY_H - 14;
    const int mapLeft = 10, mapRight = DISPLAY_W - 10;
    const int mapW = mapRight - mapLeft;
    const int mapH = mapBot - mapTop;

    for (int i = 0; i < ROUTE_STATION_COUNT; i++) {
        int sx = mapLeft + (int)((ROUTE_STATIONS[i].lon - lonMin) / (lonMax - lonMin) * mapW);
        int sy = mapTop + (int)((latMax - ROUTE_STATIONS[i].lat) / (latMax - latMin) * mapH);

        uint32_t color = wxFlightCatColor(wx.findMetarCat(ROUTE_STATIONS[i].icao));
        bb.fillCircle(sx, sy, 6, color);
        bb.drawCircle(sx, sy, 7, lgfx::color888(60, 60, 60));

        bb.setTextSize(1);
        bb.setTextColor(lgfx::color888(180, 180, 180));
        int labelX = sx + 10, labelY = sy - 4;
        if (labelX + 28 > DISPLAY_W - 4) labelX = sx - 32;
        bb.drawString(ROUTE_STATIONS[i].icao, labelX, labelY);
    }

    int ageMin = (int)((nowMs - wx.fetchTime) / 60000);
    char buf[32];
    snprintf(buf, sizeof(buf), "%d min ago", ageMin);
    bb.setTextColor(lgfx::color888(0, 60, 0));
    bb.drawString(buf, 4, DISPLAY_H - 10);
}

static void wxDrawTafMap(LGFX_Sprite& bb, const WxData& wx,
                         const char* topRow[4], const char* botRow[4],
                         unsigned long nowMs)
{
    bb.fillScreen(0);

    const int cols = 4;
    const int colW = 72;
    const int gridW = cols * colW;
    const int xOff = (DISPLAY_W - gridW) / 2;
    const int topLabelY = 4;
    const int botLabelY = DISPLAY_H / 2 + 2;
    const int dotR = 4;
    const int dotSpacing = 12;

    bb.setTextSize(1);

    auto drawColumn = [&](int cx, const char* icao, int anchorY) {
        const WxTaf* taf = wx.findTaf(icao);
        if (taf && taf->periodCount > 0) {
            for (int p = 0; p < taf->periodCount; p++) {
                int dy = anchorY + p * dotSpacing;
                bb.fillCircle(cx, dy, dotR, wxFlightCatColor(taf->periods[p].flightCat));
                if (taf->periods[p].isTempo)
                    bb.drawCircle(cx, dy, dotR + 1, lgfx::color888(120, 120, 120));
                bb.setTextColor(lgfx::color888(0, 160, 0));
                bb.drawString(taf->periods[p].label, cx + dotR + 4, dy - 4);
            }
        } else {
            bb.fillCircle(cx, anchorY, dotR, wxFlightCatColor(wx.findMetarCat(icao)));
        }
    };

    for (int c = 0; c < cols; c++) {
        int cx = xOff + c * colW + colW / 2;

        bb.setTextColor(lgfx::color888(0, 160, 0));
        bb.drawCentreString(topRow[c] + 1, cx, topLabelY);
        drawColumn(cx, topRow[c], topLabelY + 14);

        bb.setTextColor(lgfx::color888(0, 160, 0));
        bb.drawCentreString(botRow[c] + 1, cx, botLabelY);
        drawColumn(cx, botRow[c], botLabelY + 14);
    }

    int ageMin = (int)((nowMs - wx.fetchTime) / 60000);
    char buf[32];
    snprintf(buf, sizeof(buf), "TAF  %d min ago", ageMin);
    bb.setTextColor(lgfx::color888(0, 60, 0));
    bb.drawString(buf, 4, DISPLAY_H - 10);
}
