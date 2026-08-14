#pragma once

#include <map>
#include <vector>

#include "models/TrackedAircraft.h"
#include "Overlays.h"
#include "ConfigurationWebServer.h"
#include "OpenSkyAuthTokenHandler.h"
#include <LovyanGFX.hpp>

class AircraftManager
{
private:
    double lat = 0.0;
    double lon = 0.0;
    double rad = 0.5;
    double displayRad = 0.5;
    String airportId;
    std::vector<RunwayInfo> runways;
    std::map<String, TrackedAircraft> trackedAircraft;
    std::vector<String> knownTails;

    bool displayInfoText = true;
    bool displayTriangles = true;

    unsigned long fetchInterval = 0;
    unsigned long lastFetch = 999999;

    ConfigurationWebServer& configServer;
    OpenSkyAuthTokenHandler& authHandler;
    HttpRequestManager& http;

    bool IsKnownTail(const String& callsign, const String& icao) const;
    void DrawRadarCircles(LGFX_Sprite& backbuffer) const;
    void DrawCrosshairs(LGFX_Sprite& backbuffer) const;
    void DrawCoastline(LGFX_Sprite& backbuffer) const;
    void DrawRunway(LGFX_Sprite& backbuffer) const;
    void DrawSidebar(LGFX_Sprite& backbuffer) const;
    std::pair<int, int> ProjectCoordinateToScreen(float predLat, float predLon) const;
    void DrawAircraftInfo(LGFX_Sprite& backbuffer, int x, int y, const TrackedAircraft& tracked, bool known) const;
    void DrawAircraftTriangle(LGFX_Sprite& backbuffer, int x, int y, const TrackedAircraft& tracked, bool known) const;

public:
    AircraftManager(ConfigurationWebServer& config, OpenSkyAuthTokenHandler& auth, HttpRequestManager& httpManager)
        : configServer(config), authHandler(auth), http(httpManager)
    {
    }
    ~AircraftManager() = default;

    void Initialise();
    void Update();
    void Draw(LGFX_Sprite& backbuffer);
    float GetSweepAngle() const;
    void SetDiameterNm(double nm);
    double GetDiameterNm() const;
    const String& GetAirportId() const { return airportId; }
    const std::vector<RunwayInfo>& GetRunways() const { return runways; }
};
