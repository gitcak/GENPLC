#ifndef ARDUINO_H
#define ARDUINO_H

// Minimal Arduino.h stub for IntelliSense / desktop tooling
// Install ESP32 Arduino package for full functionality

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <string>
#include <algorithm>

class String {
public:
    String() = default;
    String(const char* str) : _s(str ? str : "") {}
    String(const std::string& str) : _s(str) {}
    String(const String& str) = default;
    String(int v) : _s(std::to_string(v)) {}
    String(unsigned int v) : _s(std::to_string(v)) {}
    String(long v) : _s(std::to_string(v)) {}
    String(unsigned long v) : _s(std::to_string(v)) {}

    ~String() = default;
    String& operator=(const String& rhs) = default;

    const char* c_str() const { return _s.c_str(); }
    unsigned int length() const { return static_cast<unsigned int>(_s.size()); }

    bool endsWith(const char* suffix) const {
        if (!suffix) return false;
        const std::string suf(suffix);
        if (suf.size() > _s.size()) return false;
        return _s.compare(_s.size() - suf.size(), suf.size(), suf) == 0;
    }

    int indexOf(const char* needle, int fromIndex = 0) const {
        if (!needle) return -1;
        if (fromIndex < 0) fromIndex = 0;
        const auto pos = _s.find(needle, static_cast<size_t>(fromIndex));
        return pos == std::string::npos ? -1 : static_cast<int>(pos);
    }

    int indexOf(char c, int fromIndex = 0) const {
        if (fromIndex < 0) fromIndex = 0;
        const auto pos = _s.find(c, static_cast<size_t>(fromIndex));
        return pos == std::string::npos ? -1 : static_cast<int>(pos);
    }

    String substring(int beginIndex, int endIndex) const {
        if (beginIndex < 0) beginIndex = 0;
        if (endIndex < beginIndex) endIndex = beginIndex;
        return _s.substr(static_cast<size_t>(beginIndex), static_cast<size_t>(endIndex - beginIndex));
    }

    void remove(unsigned int index, unsigned int count) {
        if (index >= _s.size()) return;
        _s.erase(index, count);
    }

    void trim() {
        auto isSpace = [](unsigned char ch) { return ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n'; };
        while (!_s.empty() && isSpace(static_cast<unsigned char>(_s.front()))) _s.erase(_s.begin());
        while (!_s.empty() && isSpace(static_cast<unsigned char>(_s.back()))) _s.pop_back();
    }

    bool empty() const { return _s.empty(); } // not Arduino API; helpful for tooling

    String& operator+=(const String& rhs) { _s += rhs._s; return *this; }
    String& operator+=(const char* rhs) { _s += (rhs ? rhs : ""); return *this; }
    String& operator+=(char rhs) { _s.push_back(rhs); return *this; }

    friend String operator+(const String& a, const String& b) { return a._s + b._s; }
    friend bool operator==(const String& a, const char* b) { return a._s == (b ? b : ""); }
    friend bool operator==(const String& a, const String& b) { return a._s == b._s; }

private:
    std::string _s;
};

class HardwareSerial {
public:
    void begin(unsigned long baud) { (void)baud; }
    void begin(unsigned long baud, int config, int rxPin, int txPin) { (void)baud; (void)config; (void)rxPin; (void)txPin; }
    void print(const char* str) { (void)str; }
    void println(const char* str) { (void)str; }
    size_t available() { return 0; }
    int read() { return -1; }
    size_t write(const uint8_t* data, size_t len) { (void)data; return len; }
    void write(uint8_t byte) { (void)byte; }
    void flush() {}
};

extern HardwareSerial Serial;
extern HardwareSerial Serial2;

#define SERIAL_8N1 0

#define HIGH 0x1
#define LOW  0x0
#define INPUT 0x0
#define OUTPUT 0x1

void pinMode(uint8_t pin, uint8_t mode) {}
void digitalWrite(uint8_t pin, uint8_t val) {}
int digitalRead(uint8_t pin) { return 0; }
void delay(unsigned long ms) {}

unsigned long millis() { return 0; }
unsigned long micros() { return 0; }

#endif // ARDUINO_H
