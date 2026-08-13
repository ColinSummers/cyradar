#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <vector>

#include "JsonParser.h"

struct Aircraft {
    String icao24;
    String callsign;
    String originCountry;
    long   timePosition;
    long   lastContact;
    float  longitude;
    float  latitude;
    float  baroAltitude;
    bool   onGround;
    float  velocity;
    float  trueTrack;
    float  verticalRate;
    float  geoAltitude;
    String squawk;
    bool   spi;
    int    positionSource;
    int    category;
};

namespace JsonParser {
    template<>
    Aircraft Parse<Aircraft>(const JsonVariant& state);
}
