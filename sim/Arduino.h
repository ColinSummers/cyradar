#pragma once

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <cstdlib>
#include <algorithm>
#include <string>
#include <chrono>
#include <thread>

using std::min;
using std::max;

inline float radians(float deg) { return deg * M_PI / 180.0f; }

inline unsigned long millis() {
    static auto start = std::chrono::steady_clock::now();
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start).count();
}

inline void delay(unsigned long ms) {
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

class String {
    std::string _s;
public:
    String() {}
    String(const char* s) : _s(s ? s : "") {}
    String(const std::string& s) : _s(s) {}
    String(int val) : _s(std::to_string(val)) {}
    String(float val, int dec = 2) {
        char buf[32];
        snprintf(buf, sizeof(buf), "%.*f", dec, val);
        _s = buf;
    }
    String(double val, int dec = 2) {
        char buf[32];
        snprintf(buf, sizeof(buf), "%.*f", dec, val);
        _s = buf;
    }

    const char* c_str() const { return _s.c_str(); }
    operator const char*() const { return _s.c_str(); }
    unsigned int length() const { return _s.length(); }
    bool isEmpty() const { return _s.empty(); }

    void trim() {
        size_t start = _s.find_first_not_of(" \t\r\n");
        size_t end = _s.find_last_not_of(" \t\r\n");
        if (start == std::string::npos) { _s.clear(); return; }
        _s = _s.substr(start, end - start + 1);
    }

    void toUpperCase() {
        for (auto& c : _s) c = toupper(c);
    }

    double toDouble() const { return atof(_s.c_str()); }
    float toFloat() const { return (float)atof(_s.c_str()); }
    int toInt() const { return atoi(_s.c_str()); }

    int indexOf(const String& s) const {
        auto pos = _s.find(s._s);
        return pos == std::string::npos ? -1 : (int)pos;
    }
    int indexOf(char c) const {
        auto pos = _s.find(c);
        return pos == std::string::npos ? -1 : (int)pos;
    }

    String substring(unsigned int from) const { return String(_s.substr(from)); }
    String substring(unsigned int from, unsigned int to) const { return String(_s.substr(from, to - from)); }

    String& operator+=(const String& rhs) { _s += rhs._s; return *this; }
    String& operator+=(const char* rhs) { _s += rhs; return *this; }
    friend String operator+(const String& a, const String& b) { return String(a._s + b._s); }
    friend String operator+(const String& a, const char* b) { return String(a._s + b); }
    friend String operator+(const char* a, const String& b) { return String(std::string(a) + b._s); }
    bool operator==(const String& rhs) const { return _s == rhs._s; }
    bool operator==(const char* rhs) const { return _s == rhs; }
    bool operator!=(const String& rhs) const { return _s != rhs._s; }
    bool operator<(const String& rhs) const { return _s < rhs._s; }

    char* begin() { return &_s[0]; }
    char* end() { return &_s[0] + _s.size(); }
    const char* begin() const { return _s.c_str(); }
    const char* end() const { return _s.c_str() + _s.size(); }
};

struct SerialClass {
    void begin(int) {}
    void print(const char* s) { printf("%s", s); }
    void print(const String& s) { printf("%s", s.c_str()); }
    void print(int v) { printf("%d", v); }
    void println(const char* s) { printf("%s\n", s); }
    void println(const String& s) { printf("%s\n", s.c_str()); }
    void println(int v) { printf("%d\n", v); }
    void println() { ::printf("\n"); }
    template<typename... Args>
    void printf(const char* fmt, Args... args) { ::printf(fmt, args...); }
};

inline SerialClass Serial;
