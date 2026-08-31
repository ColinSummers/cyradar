#pragma once

struct GeoPoint {
    float lat;
    float lon;
};

struct CoastSegment {
    const GeoPoint* points;
    int count;
};

struct RunwayInfo {
    char id[8];
    float heading1, heading2;
    float lat1, lon1, lat2, lon2;
};

// Pacific NW regional runways (OurAirports, ODbL)
// Drawn when they fall within the radar view at wider zoom levels
struct NearbyRunway {
    float lat1, lon1, lat2, lon2;
};

constexpr NearbyRunway PNW_RUNWAYS[] = {
    // KAWO (Arlington)
    {48.1617f, -122.1690f, 48.1559f, -122.1570f},
    {48.1693f, -122.1570f, 48.1547f, -122.1560f},
    // KBFI (Boeing Field)
    {47.5380f, -122.3075f, 47.5292f, -122.3000f},
    {47.5405f, -122.3114f, 47.5167f, -122.2913f},
    // KBVS (Skagit)
    {48.4641f, -122.4240f, 48.4686f, -122.4140f},
    {48.4778f, -122.4310f, 48.4689f, -122.4130f},
    // KCLM (Port Angeles)
    {48.1210f, -123.5120f, 48.1167f, -123.4860f},
    {48.1266f, -123.5050f, 48.1191f, -123.4980f},
    // KFHR (Friday Harbor)
    {48.5266f, -123.0250f, 48.5173f, -123.0240f},
    // KNUW (NAS Whidbey)
    {48.3513f, -122.6730f, 48.3524f, -122.6400f},
    {48.3617f, -122.6630f, 48.3419f, -122.6480f},
    // KOLM (Olympia)
    {46.9686f, -122.8920f, 46.9659f, -122.8760f},
    {46.9764f, -122.9020f, 46.9616f, -122.9070f},
    // KORS (Orcas Island)
    {48.7123f, -122.9110f, 48.7044f, -122.9110f},
    // KPAE (Paine Field)
    {47.9064f, -122.2717f, 47.8982f, -122.2716f},
    {47.9213f, -122.2859f, 47.8966f, -122.2853f},
    // KRNT (Renton)
    {47.5005f, -122.2169f, 47.4858f, -122.2147f},
    // KSEA (SeaTac)
    {47.4638f, -122.3110f, 47.4380f, -122.3110f},
    {47.4638f, -122.3080f, 47.4312f, -122.3080f},
    {47.4638f, -122.3180f, 47.4405f, -122.3180f},
    // KTIW (Tacoma Narrows)
    {47.2747f, -122.5770f, 47.2611f, -122.5790f},
    // S43 (Harvey Field)
    {47.9083f, -122.1040f, 47.9013f, -122.1010f},
    {47.9082f, -122.1050f, 47.9017f, -122.1020f},
};
constexpr int PNW_RUNWAY_COUNT = sizeof(PNW_RUNWAYS) / sizeof(PNW_RUNWAYS[0]);

// Los Angeles basin regional runways (OurAirports, ODbL)
constexpr NearbyRunway LA_RUNWAYS[] = {
    // KFUL (Fullerton)
    {33.8711f, -117.9848f, 33.8730f, -117.9748f},
    // KHHR (Hawthorne)
    {33.9225f, -118.3430f, 33.9232f, -118.3270f},
    // KLAX (6L/24R)
    {33.9491f, -118.4312f, 33.9521f, -118.4020f},
    // KLAX (6R/24L)
    {33.9468f, -118.4347f, 33.9505f, -118.3991f},
    // KLAX (7L/25R)
    {33.9356f, -118.4221f, 33.9399f, -118.3798f},
    // KLAX (7R/25L)
    {33.9337f, -118.4191f, 33.9374f, -118.3827f},
    // KSMO (03/21)
    {34.0124f, -118.4554f, 34.0193f, -118.4473f},
    // KTOA (Zamperini/Torrance 11L/29R)
    {33.8081f, -118.3457f, 33.7995f, -118.3328f},
    // KTOA (11R/29L)
    {33.8053f, -118.3441f, 33.8002f, -118.3364f},
    // KVNY (Van Nuys 16L/34R)
    {34.2190f, -118.4900f, 34.2080f, -118.4890f},
    // KVNY (16R/34L)
    {34.2189f, -118.4913f, 34.1970f, -118.4892f},
};
constexpr int LA_RUNWAY_COUNT = sizeof(LA_RUNWAYS) / sizeof(LA_RUNWAYS[0]);

// Weather stations along FHR→BFI route and return-flight outs
struct MetarStation {
    const char* icao;
    float lat;
    float lon;
};

constexpr MetarStation ROUTE_STATIONS[] = {
    // Going down: FHR → BFI
    {"KFHR", 48.5220f, -123.0244f},
    {"KNUW", 48.3518f, -122.6558f},
    {"KPAE", 47.9063f, -122.2816f},
    {"KBFI", 47.5300f, -122.3020f},
    // Return outs
    {"KBVS", 48.4708f, -122.4208f},
    {"KBLI", 48.7927f, -122.5375f},
    {"KORS", 48.7082f, -122.9105f},
};
constexpr int ROUTE_STATION_COUNT = sizeof(ROUTE_STATIONS) / sizeof(ROUTE_STATIONS[0]);
