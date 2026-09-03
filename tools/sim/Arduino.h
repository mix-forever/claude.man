// Minimal Arduino.h stand-in for the host-side display simulator.
#pragma once
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <string>
#include <chrono>
#include <thread>

#define PROGMEM
#define pgm_read_byte(p) (*(const unsigned char*)(p))

inline uint32_t millis() {
    using namespace std::chrono;
    static auto t0 = steady_clock::now();
    return (uint32_t)duration_cast<milliseconds>(steady_clock::now() - t0).count();
}
inline void delay(uint32_t ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }

class String {
    std::string s;
public:
    String() {}
    String(const char* c) : s(c ? c : "") {}
    String(const std::string& c) : s(c) {}
    String(int v) : s(std::to_string(v)) {}
    String(unsigned v) : s(std::to_string(v)) {}
    String(long v) : s(std::to_string(v)) {}
    String(unsigned long v) : s(std::to_string(v)) {}
    const char* c_str() const { return s.c_str(); }
    size_t length() const { return s.size(); }
    bool isEmpty() const { return s.empty(); }
    char operator[](size_t i) const { return s[i]; }
    String operator+(const String& o) const { return String(s + o.s); }
    String& operator+=(const String& o) { s += o.s; return *this; }
    String& operator+=(char c) { s += c; return *this; }
    bool operator==(const String& o) const { return s == o.s; }
    void trim() { size_t a = s.find_first_not_of(" \t\r\n"), b = s.find_last_not_of(" \t\r\n"); s = (a == std::string::npos) ? "" : s.substr(a, b - a + 1); }
};
inline String operator+(const char* a, const String& b) { return String(a) + b; }
