// Just enough of the Arduino core for the firmware to run in the desktop simulator.
#pragma once

#include <algorithm>
#include <cctype>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#define HEX 16
#define DEC 10

unsigned long millis();
void delay(unsigned long ms);
uint32_t esp_random();
inline void* ps_malloc(size_t n) { return std::malloc(n); }

class String {
public:
    String() = default;
    String(const char* s) : s_(s ? s : "") {}
    String(const std::string& s) : s_(s) {}
    String(char c) : s_(1, c) {}
    String(int v, int base = DEC) : s_(format(static_cast<long long>(v), base)) {}
    String(unsigned v, int base = DEC) : s_(format(static_cast<long long>(v), base)) {}
    String(long v, int base = DEC) : s_(format(static_cast<long long>(v), base)) {}
    String(unsigned long v, int base = DEC) : s_(format(static_cast<long long>(v), base)) {}
    String(long long v, int base = DEC) : s_(format(v, base)) {}
    String(unsigned long long v, int base = DEC) : s_(format(static_cast<long long>(v), base)) {}

    const char* c_str() const { return s_.c_str(); }
    // M5GFX only takes Arduino Strings in Arduino builds; elsewhere it takes const char*.
    operator const char*() const { return s_.c_str(); }
    size_t length() const { return s_.size(); }
    bool isEmpty() const { return s_.empty(); }
    char operator[](size_t i) const { return i < s_.size() ? s_[i] : '\0'; }

    String substring(size_t from) const { return from >= s_.size() ? String() : String(s_.substr(from)); }
    String substring(size_t from, size_t to) const {
        if (to > s_.size()) to = s_.size();
        if (from >= to) return String();
        return String(s_.substr(from, to - from));
    }
    int indexOf(char c, size_t from = 0) const { return find(s_.find(c, from)); }
    int indexOf(const String& t, size_t from = 0) const { return find(s_.find(t.s_, from)); }
    int lastIndexOf(char c) const { return find(s_.rfind(c)); }
    bool endsWith(const String& t) const {
        return s_.size() >= t.s_.size() && s_.compare(s_.size() - t.s_.size(), t.s_.size(), t.s_) == 0;
    }
    bool startsWith(const String& t) const { return s_.rfind(t.s_, 0) == 0; }
    void remove(size_t index) { if (index < s_.size()) s_.erase(index); }
    void remove(size_t index, size_t count) { if (index < s_.size()) s_.erase(index, count); }
    void trim() {
        const auto notSpace = [](unsigned char c) { return !std::isspace(c); };
        s_.erase(s_.begin(), std::find_if(s_.begin(), s_.end(), notSpace));
        s_.erase(std::find_if(s_.rbegin(), s_.rend(), notSpace).base(), s_.end());
    }
    long toInt() const { return std::atol(s_.c_str()); }
    int compareTo(const String& o) const { return s_.compare(o.s_); }

    String& operator+=(const String& o) { s_ += o.s_; return *this; }
    String& operator+=(const char* o) { s_ += o; return *this; }
    String& operator+=(char c) { s_ += c; return *this; }
    friend String operator+(const String& a, const String& b) { return String(a.s_ + b.s_); }
    friend String operator+(const String& a, const char* b) { return String(a.s_ + b); }
    friend String operator+(const char* a, const String& b) { return String(a + b.s_); }
    friend String operator+(const String& a, char b) { return String(a.s_ + b); }
    bool operator==(const String& o) const { return s_ == o.s_; }
    bool operator!=(const String& o) const { return s_ != o.s_; }
    bool operator==(const char* o) const { return s_ == o; }
    bool operator!=(const char* o) const { return s_ != o; }

    const std::string& str() const { return s_; }

private:
    static int find(size_t p) { return p == std::string::npos ? -1 : static_cast<int>(p); }
    static std::string format(long long v, int base) {
        char buf[32];
        if (base == HEX) std::snprintf(buf, sizeof(buf), "%llx", static_cast<unsigned long long>(v));
        else std::snprintf(buf, sizeof(buf), "%lld", v);
        return buf;
    }
    std::string s_;
};

// Serial goes to stdout; input comes from the script runner.
class SimSerial {
public:
    void begin(unsigned long) {}
    int available();
    int read();
    void flush() { std::fflush(stdout); }
    void println(const String& s) { std::printf("%s\n", s.c_str()); }
    void printf(const char* fmt, ...) __attribute__((format(printf, 2, 3))) {
        va_list ap;
        va_start(ap, fmt);
        std::vprintf(fmt, ap);
        va_end(ap);
    }
};
extern SimSerial Serial;
