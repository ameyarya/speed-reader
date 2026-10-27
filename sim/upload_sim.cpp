// "Add books" mode for the simulator: no Wi-Fi, books arrive from the script.
#include <LittleFS.h>

#include <fstream>
#include <sstream>

#include "../src/library.h"
#include "../src/upload.h"

namespace upload {
namespace {
bool running = false;
uint32_t startedMs = 0;
String ssid_ = "SpeedReader-SIM", password_ = "sim2345k", status_, lastAdded_;
bool libraryChanged_ = false;
}  // namespace

void begin() {
    running = true;
    startedMs = millis();
    status_ = "";
    lastAdded_ = "";
}
void loop() {}
void end() { running = false; }
const String& ssid() { return ssid_; }
const String& password() { return password_; }
String url() { return "http://192.168.4.1"; }
// A phone "joins" two seconds after the mode opens.
bool phoneJoined() { return running && millis() - startedMs > 2000; }
const String& status() { return status_; }
bool takeLibraryChanged() {
    const bool changed = libraryChanged_;
    libraryChanged_ = false;
    return changed;
}
bool takeDoneRequested() { return false; }
const String& lastAdded() { return lastAdded_; }

} // namespace upload

namespace sim_upload {

// Same steps as handleUploadChunk/handleUploadDone in src/upload.cpp.
void receive(const std::string& path) {
    if (!upload::running) {
        std::printf("[sim] upload ignored: not in Add books mode\n");
        return;
    }
    std::ifstream in(path, std::ios::binary);
    std::stringstream ss;
    ss << in.rdbuf();
    const std::string data = ss.str();
    const String name = library::safeName(path.substr(path.find_last_of('/') + 1).c_str());
    File f = LittleFS.open(library::pathFor(name), "w");
    f.write(reinterpret_cast<const uint8_t*>(data.data()), data.size());
    f.close();
    library::savePosition(name, 0);
    upload::status_ = "Added " + name.substring(0, name.length() - 4);
    upload::lastAdded_ = name;
    upload::libraryChanged_ = true;
    std::printf("[sim] uploaded %s (%zu bytes)\n", name.c_str(), data.size());
}

// Same as handleDelete in src/upload.cpp.
void remove(const std::string& name) {
    library::remove(name.c_str());
    upload::status_ = String("Removed ") + name.c_str();
    upload::libraryChanged_ = true;
    std::printf("[sim] removed %s\n", name.c_str());
}

} // namespace sim_upload
