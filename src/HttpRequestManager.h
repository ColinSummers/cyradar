#pragma once

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <vector>

struct HttpResult {
    bool success;
    int statusCode;
    String response;
    String errorMessage;
};

class HttpRequestManager
{
private:
    HTTPClient http;
    WiFiClientSecure secureClient;

    String BuildQueryString(const std::vector<std::pair<String, String>>& params) const;

public:
    HttpRequestManager() {
        secureClient.setInsecure();
        secureClient.setTimeout(10);
    }
    ~HttpRequestManager() = default;

    [[nodiscard]] HttpResult Get(const String& url, const std::vector<std::pair<String, String>>& params = {}, const std::vector<std::pair<String, String>>& headers = {});
    [[nodiscard]] HttpResult Post(const String& url, const String& body = "", const std::vector<std::pair<String, String>>& headers = {});

    bool GetJson(const String& url, JsonDocument& doc, const std::vector<std::pair<String, String>>& params = {}, const std::vector<std::pair<String, String>>& headers = {});
};
