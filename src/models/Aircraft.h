#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <vector>

#include "JsonParser.h"

struct Aircraft {
    String icao24;
    String callsign;
    String originCountry;
    long   timePosition = 0;
    long   lastContact = 0;
    float  longitude = 0;
    float  latitude = 0;
    float  baroAltitude = 0;
    bool   onGround = false;
    float  velocity = 0;
    float  trueTrack = 0;
    float  verticalRate = 0;
    float  geoAltitude = 0;
    String squawk;
    bool   spi = false;
    int    positionSource = 0;
    int    category = 0;
};

namespace JsonParser {
    template<>
    Aircraft Parse<Aircraft>(const JsonVariant& state);
}
