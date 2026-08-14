#include "HttpRequestManager.h"

String HttpRequestManager::BuildQueryString(const std::vector<std::pair<String, String>>& params) const
{
    if (params.empty())
        return "";

    String queryStream = "?";

    bool first = true;
    for (const auto& [key, value] : params)
    {
        if (!first)
            queryStream += "&";

        queryStream += key + "=" + value;

        first = false;
    }

    return queryStream;
}

HttpResult HttpRequestManager::Get(const String& url, const std::vector<std::pair<String, String>>& params, const std::vector<std::pair<String, String>>& headers) {
    HttpResult result{ false, 0, "", "" };

    const String queryParams = BuildQueryString(params);
    const String fullUrl = url + queryParams;

    http.begin(secureClient, fullUrl);

    for (const auto& header : headers) {
        http.addHeader(header.first, header.second);
    }

    int responseCode = http.GET();
    result.statusCode = responseCode;

    if (responseCode >= 200 && responseCode < 300) {
        result.success = true;
        result.response = http.getString();
    }
    else {
        result.success = false;
        if (responseCode > 0) {
            result.errorMessage = String(responseCode);
            result.response = http.getString();
        } else {
            result.errorMessage = http.errorToString(responseCode);
        }
        Serial.printf("[GET] HTTP Error %d: %s\n", responseCode, result.errorMessage.c_str());
    }

    http.end();
    return result;
}

HttpResult HttpRequestManager::Post(const String& url, const String& body, const std::vector<std::pair<String, String>>& headers)
{
    HttpResult result{ false, 0, "", "" };

    http.begin(secureClient, url);

    for (const auto& header : headers) {
        http.addHeader(header.first, header.second);
    }

    int responseCode = http.POST(body);
    result.statusCode = responseCode;

    if (responseCode >= 200 && responseCode < 300) {
        result.success = true;
        result.response = http.getString();
    }
    else {
        result.success = false;
        if (responseCode > 0) {
            result.errorMessage = String(responseCode);
            result.response = http.getString();
        } else {
            result.errorMessage = http.errorToString(responseCode);
        }
        Serial.printf("[POST] HTTP Error %d: %s\n", responseCode, result.errorMessage.c_str());
    }

    http.end();
    return result;
}

bool HttpRequestManager::GetJson(const String& url, JsonDocument& doc, const std::vector<std::pair<String, String>>& params, const std::vector<std::pair<String, String>>& headers) {
    const String queryParams = BuildQueryString(params);
    const String fullUrl = url + queryParams;

    http.begin(secureClient, fullUrl);

    for (const auto& header : headers) {
        http.addHeader(header.first, header.second);
    }

    int code = http.GET();
    bool ok = false;

    if (code >= 200 && code < 300) {
        DeserializationError err = deserializeJson(doc, http.getStream());
        if (err)
            Serial.printf("[GET] JSON parse failed: %s\n", err.c_str());
        else
            ok = true;
    } else if (code > 0) {
        Serial.printf("[GET] HTTP Error: %d\n", code);
    } else {
        Serial.printf("[GET] Connection failed: %s\n", http.errorToString(code).c_str());
    }

    http.end();
    return ok;
}
