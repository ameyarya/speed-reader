// NVS for the simulator: each namespace is a key=value file in $SIM_FS/.nvs/.
#pragma once

#include <Arduino.h>

#include <map>

class Preferences {
public:
    bool begin(const char* name, bool readOnly = false);
    bool remove(const char* key);

    uint32_t getUInt(const char* key, uint32_t def = 0) { return get(key, def); }
    void putUInt(const char* key, uint32_t v) { put(key, std::to_string(v)); }
    uint16_t getUShort(const char* key, uint16_t def = 0) { return static_cast<uint16_t>(get(key, def)); }
    void putUShort(const char* key, uint16_t v) { put(key, std::to_string(v)); }
    uint8_t getUChar(const char* key, uint8_t def = 0) { return static_cast<uint8_t>(get(key, def)); }
    void putUChar(const char* key, uint8_t v) { put(key, std::to_string(v)); }
    bool getBool(const char* key, bool def = false) { return get(key, def ? 1 : 0) != 0; }
    void putBool(const char* key, bool v) { put(key, v ? "1" : "0"); }
    String getString(const char* key, const String& def = String()) {
        auto it = values_.find(key);
        return it == values_.end() ? def : String(it->second);
    }
    void putString(const char* key, const String& v) { put(key, v.str()); }

private:
    uint32_t get(const char* key, uint32_t def) {
        auto it = values_.find(key);
        return it == values_.end() ? def : static_cast<uint32_t>(std::stoul(it->second));
    }
    void put(const char* key, const std::string& v);
    void save();

    std::string path_;
    std::map<std::string, std::string> values_;
};
