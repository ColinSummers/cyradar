#pragma once

#include <string>

#ifdef LGFX_SDL
// ---- Desktop: libcurl ----
#include <curl/curl.h>

namespace httpfetch {

static size_t writeCb(void* data, size_t size, size_t nmemb, std::string* out) {
    out->append((char*)data, size * nmemb);
    return size * nmemb;
}

static inline std::string get(const char* url) {
    CURL* curl = curl_easy_init();
    if (!curl) return "";
    std::string response;
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 15L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    CURLcode res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);
    if (res != CURLE_OK) return "";
    return response;
}

static inline void globalInit() { curl_global_init(CURL_GLOBAL_DEFAULT); }
static inline void globalCleanup() { curl_global_cleanup(); }

}

#else
// ---- ESP32: HTTPClient ----
#include <HTTPClient.h>

namespace httpfetch {

static inline std::string get(const char* url) {
    HTTPClient client;
    client.begin(url);
    client.setTimeout(15000);
    int code = client.GET();
    std::string body;
    if (code == 200) {
        String s = client.getString();
        body.assign(s.c_str(), s.length());
    }
    client.end();
    return body;
}

static inline void globalInit() {}
static inline void globalCleanup() {}

}

#endif
