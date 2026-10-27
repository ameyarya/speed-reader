// LittleFS for the simulator: files live in a folder on the Mac ($SIM_FS).
#pragma once

#include <Arduino.h>

#include <memory>
#include <vector>

class File {
public:
    File() = default;
    explicit operator bool() const { return static_cast<bool>(impl_); }
    size_t size() const;
    const char* name() const;
    bool isDirectory() const;
    File openNextFile();
    size_t read(uint8_t* buf, size_t len);
    size_t write(const uint8_t* buf, size_t len);
    size_t print(const char* s) { return write(reinterpret_cast<const uint8_t*>(s), std::strlen(s)); }
    void close() { impl_.reset(); }

    struct Impl;
    explicit File(std::shared_ptr<Impl> impl) : impl_(std::move(impl)) {}

private:
    std::shared_ptr<Impl> impl_;
};

class SimLittleFS {
public:
    bool begin(bool formatOnFail = false);
    bool exists(const String& path);
    bool mkdir(const String& path);
    bool remove(const String& path);
    File open(const String& path, const char* mode = "r");
    size_t totalBytes();
    size_t usedBytes();
};
extern SimLittleFS LittleFS;
