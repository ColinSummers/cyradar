#pragma once
// Stub for simulator
class JsonVariant {};
class JsonArray {
public:
    size_t size() const { return 0; }
    const JsonVariant* begin() const { return nullptr; }
    const JsonVariant* end() const { return nullptr; }
};
