#pragma once

#include "HttpRequestManager.h"

class OpenSkyAuthTokenHandler
{
private:
    HttpRequestManager& http;

    String bearerToken = "";
    unsigned long tokenFetchedAt = 0;
    static constexpr unsigned long TOKEN_TTL = 29UL * 60 * 1000;

    String FetchBearerToken(const String& url, const String& clientId, const String& clientSecret);

public:
    OpenSkyAuthTokenHandler(HttpRequestManager& httpRequestManager) : http(httpRequestManager) {}
    ~OpenSkyAuthTokenHandler() = default;

    [[nodiscard]] const String GetValidToken(const String& clientId, const String& clientSecret);
};
