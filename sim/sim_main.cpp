// Desktop simulator: runs the real firmware (src/) in an SDL window, with the
// Arduino pieces replaced by the stand-ins in sim/include.
//
// A script drives it, one command per line (see tools/sim_test.py):
//   a / b            click the front / side button
//   A / B            hold it (1.2 s)
//   hold a|b|ab MS   hold for MS milliseconds
//   wait MS          let time pass
//   advance MS       jump the firmware's clock forward MS milliseconds
//   shot NAME        save a screenshot to $SIM_OUT/NAME.ppm
//   expect TEXT      fail unless debugState() contains TEXT
//   reject TEXT      fail if debugState() contains TEXT
//   upload FILE      pretend FILE arrived over Wi-Fi (in "Add books" mode)
//   delete NAME      pretend NAME was removed on the upload page
//   serial TEXT      type TEXT on the serial port
//   reboot           restart the firmware, keeping storage
//   quit
#include <M5Unified.h>

#include <chrono>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <unistd.h>
#include <vector>

#include <Arduino.h>
#include <LittleFS.h>
#include <Preferences.h>
#include <esp_sleep.h>

void setup();
void loop();
String debugState();

namespace sim_upload {
void receive(const std::string& path);
void remove(const std::string& name);
}

namespace fs = std::filesystem;

// ---- Arduino core ------------------------------------------------------------

// "advance" in a script moves the firmware's clock forward (to test idle timers).
static unsigned long clockOffsetMs = 0;
unsigned long millis() { return lgfx::millis() + clockOffsetMs; }
void delay(unsigned long ms) { lgfx::delay(ms); }
uint32_t esp_random() { return static_cast<uint32_t>(std::rand()); }

namespace {
std::deque<char> serialInput;
bool asleep = false;
bool wokeByButton = false;
char** programArgv = nullptr;
struct SleepSignal {};
}  // namespace

SimSerial Serial;
int SimSerial::available() { return static_cast<int>(serialInput.size()); }
int SimSerial::read() {
    if (serialInput.empty()) return -1;
    const char c = serialInput.front();
    serialInput.pop_front();
    return c;
}

esp_sleep_source_t esp_sleep_get_wakeup_cause() {
    return wokeByButton ? ESP_SLEEP_WAKEUP_EXT1 : ESP_SLEEP_WAKEUP_UNDEFINED;
}
void esp_deep_sleep_start() { throw SleepSignal{}; }

// ---- LittleFS ------------------------------------------------------------------

namespace {
fs::path root() {
    const char* dir = std::getenv("SIM_FS");
    return dir ? fs::path(dir) : fs::temp_directory_path() / "speed-reader-sim";
}
fs::path hostPath(const String& path) { return root() / std::string(path.c_str()).substr(1); }
}  // namespace

struct File::Impl {
    fs::path path;
    std::string name;
    bool dir = false;
    std::fstream stream;
    std::vector<fs::path> entries;
    size_t next = 0;
};

size_t File::size() const { return impl_->dir ? 0 : static_cast<size_t>(fs::file_size(impl_->path)); }
const char* File::name() const { return impl_->name.c_str(); }
bool File::isDirectory() const { return impl_->dir; }
File File::openNextFile() {
    if (!impl_->dir || impl_->next >= impl_->entries.size()) return File();
    auto f = std::make_shared<Impl>();
    f->path = impl_->entries[impl_->next++];
    f->name = f->path.filename().string();
    f->dir = fs::is_directory(f->path);
    if (!f->dir) f->stream.open(f->path, std::ios::in | std::ios::binary);
    return File(f);
}
size_t File::read(uint8_t* buf, size_t len) {
    impl_->stream.read(reinterpret_cast<char*>(buf), static_cast<std::streamsize>(len));
    return static_cast<size_t>(impl_->stream.gcount());
}
size_t File::write(const uint8_t* buf, size_t len) {
    impl_->stream.write(reinterpret_cast<const char*>(buf), static_cast<std::streamsize>(len));
    impl_->stream.flush();
    return impl_->stream ? len : 0;
}

SimLittleFS LittleFS;
bool SimLittleFS::begin(bool) {
    fs::create_directories(root());
    return true;
}
bool SimLittleFS::exists(const String& path) { return fs::exists(hostPath(path)); }
bool SimLittleFS::mkdir(const String& path) { return fs::create_directories(hostPath(path)); }
bool SimLittleFS::remove(const String& path) { return fs::remove(hostPath(path)); }
File SimLittleFS::open(const String& path, const char* mode) {
    auto f = std::make_shared<File::Impl>();
    f->path = hostPath(path);
    f->name = f->path.filename().string();
    if (mode[0] == 'r') {
        if (!fs::exists(f->path)) return File();
        if (fs::is_directory(f->path)) {
            f->dir = true;
            for (const auto& e : fs::directory_iterator(f->path))
                if (e.path().filename().string()[0] != '.') f->entries.push_back(e.path());
            std::sort(f->entries.begin(), f->entries.end());
            return File(f);
        }
        f->stream.open(f->path, std::ios::in | std::ios::binary);
    } else {
        f->stream.open(f->path, std::ios::out | std::ios::binary | std::ios::trunc);
    }
    return f->stream ? File(f) : File();
}
// Same capacity as the stick's book partition.
size_t SimLittleFS::totalBytes() { return 0x4F0000; }
size_t SimLittleFS::usedBytes() {
    size_t used = 0;
    for (const auto& e : fs::recursive_directory_iterator(root()))
        if (e.is_regular_file()) used += static_cast<size_t>(e.file_size());
    return used;
}

// ---- Preferences -----------------------------------------------------------------

bool Preferences::begin(const char* name, bool) {
    fs::create_directories(root() / ".nvs");
    path_ = (root() / ".nvs" / name).string();
    values_.clear();
    std::ifstream in(path_);
    std::string line;
    while (std::getline(in, line)) {
        const auto eq = line.find('=');
        if (eq != std::string::npos) values_[line.substr(0, eq)] = line.substr(eq + 1);
    }
    return true;
}
bool Preferences::remove(const char* key) {
    values_.erase(key);
    save();
    return true;
}
void Preferences::put(const char* key, const std::string& v) {
    values_[key] = v;
    save();
}
void Preferences::save() {
    std::ofstream out(path_, std::ios::trunc);
    for (const auto& [k, v] : values_) out << k << '=' << v << '\n';
}

// ---- Script runner ---------------------------------------------------------------

namespace {

// M5Unified's desktop build reads BtnA from pin 39 and BtnB from pin 38
// (and the unused BtnC/BtnPWR from 37/36).
constexpr int kPinA = 39;
constexpr int kPinB = 38;

struct Step {
    std::string cmd;
    std::string arg;
};

std::vector<Step> script;
size_t stepIndex = 0;
uint32_t waitUntil = 0;
int failures = 0;
// Pending button release.
uint32_t releaseAt = 0;
bool releasePending = false;
// Firmware loop() runs since the last button change. Steps wait for a few, so a
// slow frame can never make the firmware miss a press or a release.
int loopsSinceChange = 0;
constexpr int kSettleLoops = 3;

void press(bool a, bool b) {
    if (a) lgfx::gpio_lo(kPinA);
    if (b) lgfx::gpio_lo(kPinB);
}
void releaseAll() {
    for (int pin : {kPinA, kPinB, 37, 36}) lgfx::gpio_hi(pin);
}

void writeShot(const std::string& name) {
    const char* out = std::getenv("SIM_OUT");
    const fs::path dir = out ? fs::path(out) : fs::path(".");
    fs::create_directories(dir);
    const fs::path path = dir / (name + ".ppm");
    FILE* f = std::fopen(path.string().c_str(), "wb");
    if (!f) return;
    const int w = M5.Display.width(), h = M5.Display.height();
    std::fprintf(f, "P6\n%d %d\n255\n", w, h);
    std::vector<lgfx::rgb888_t> row(w);
    for (int y = 0; y < h; ++y) {
        M5.Display.readRect(0, y, w, 1, row.data());
        for (int x = 0; x < w; ++x) {
            const uint8_t px[3] = {row[x].r, row[x].g, row[x].b};
            std::fwrite(px, 1, 3, f);
        }
    }
    std::fclose(f);
    std::printf("[sim] shot %s\n", name.c_str());
}

// Restarting or waking resets all of the stick's memory, so the simulator does the
// same: it starts the program again (same storage), carrying on from the next step.
[[noreturn]] void restartProgram(const char* wakeButton) {
    char step[16], fails[16];
    std::snprintf(step, sizeof(step), "%zu", stepIndex);
    std::snprintf(fails, sizeof(fails), "%d", failures);
    setenv("SIM_START_STEP", step, 1);
    setenv("SIM_FAILURES", fails, 1);
    if (wakeButton) setenv("SIM_WAKE_PRESS", wakeButton, 1);
    else unsetenv("SIM_WAKE_PRESS");
    std::fflush(stdout);
    execv(programArgv[0], programArgv);
    std::perror("execv");
    std::exit(2);
}

void loadScript() {
    std::string text;
    if (const char* file = std::getenv("SIM_SCRIPT")) {
        std::ifstream in(file);
        std::stringstream ss;
        ss << in.rdbuf();
        text = ss.str();
    }
    std::istringstream lines(text);
    std::string line;
    while (std::getline(lines, line)) {
        const auto hash = line.find('#');
        if (hash != std::string::npos && (hash == 0 || line[hash - 1] == ' ')) line = line.substr(0, hash);
        while (!line.empty() && std::isspace(static_cast<unsigned char>(line.back()))) line.pop_back();
        size_t start = 0;
        while (start < line.size() && std::isspace(static_cast<unsigned char>(line[start]))) ++start;
        line = line.substr(start);
        if (line.empty()) continue;
        const auto space = line.find(' ');
        script.push_back({line.substr(0, space), space == std::string::npos ? "" : line.substr(space + 1)});
    }
    if (const char* start = std::getenv("SIM_START_STEP")) stepIndex = std::stoul(start);
    if (const char* fails = std::getenv("SIM_FAILURES")) failures = std::atoi(fails);
}

// Runs script steps that are due. Returns false once the script asks to quit.
bool runScript(uint32_t now) {
    if (loopsSinceChange < kSettleLoops) return true;
    if (releasePending) {
        if (now < releaseAt) return true;
        releaseAll();
        releasePending = false;
        loopsSinceChange = 0;
        waitUntil = now + 80;
        return true;
    }
    while (now >= waitUntil && stepIndex < script.size()) {
        if (std::getenv("SIM_TRACE") && stepIndex > 0)
            std::printf("[sim] after %s %s: %s\n", script[stepIndex - 1].cmd.c_str(), script[stepIndex - 1].arg.c_str(),
                        asleep ? "asleep" : debugState().c_str());
        const Step& s = script[stepIndex++];
        auto holdFor = [&](bool a, bool b, uint32_t ms) {
            if (asleep) {
                // The press that wakes the stick: it is still held while the firmware boots.
                std::printf("[sim] wake\n");
                restartProgram(a ? "a" : "b");
            }
            press(a, b);
            releaseAt = now + ms;
            releasePending = true;
            loopsSinceChange = 0;
        };
        if (s.cmd == "a" || s.cmd == "b") {
            holdFor(s.cmd == "a", s.cmd == "b", 90);
            return true;
        } else if (s.cmd == "A" || s.cmd == "B") {
            holdFor(s.cmd == "A", s.cmd == "B", 1200);
            return true;
        } else if (s.cmd == "hold") {
            std::istringstream in(s.arg);
            std::string which;
            uint32_t ms = 1200;
            in >> which >> ms;
            holdFor(which.find('a') != std::string::npos, which.find('b') != std::string::npos, ms);
            return true;
        } else if (s.cmd == "wait") {
            waitUntil = now + static_cast<uint32_t>(std::stoul(s.arg));
        } else if (s.cmd == "advance") {
            clockOffsetMs += std::stoul(s.arg);
            waitUntil = millis() + 100;
        } else if (s.cmd == "shot") {
            writeShot(s.arg);
        } else if (s.cmd == "expect" || s.cmd == "reject") {
            const std::string state = asleep ? std::string("mode=asleep") : debugState().str();
            const bool found = state.find(s.arg) != std::string::npos;
            const bool ok = (s.cmd == "expect") == found;
            std::printf("[sim] %s %s %s: %s\n", ok ? "PASS" : "FAIL", s.cmd.c_str(), s.arg.c_str(),
                        state.c_str());
            if (!ok) ++failures;
        } else if (s.cmd == "upload") {
            sim_upload::receive(s.arg);
        } else if (s.cmd == "delete") {
            sim_upload::remove(s.arg);
        } else if (s.cmd == "serial") {
            for (char c : s.arg + "\n") serialInput.push_back(c);
        } else if (s.cmd == "reboot") {
            std::printf("[sim] reboot\n");
            restartProgram(nullptr);
        } else if (s.cmd == "quit") {
            return false;
        } else {
            std::printf("[sim] FAIL unknown command: %s\n", s.cmd.c_str());
            ++failures;
        }
    }
    return stepIndex < script.size() || now < waitUntil || releasePending;
}

int simMain(bool* running) {
    releaseAll();
    loadScript();
    if (const char* wake = std::getenv("SIM_WAKE_PRESS")) {
        wokeByButton = true;
        press(wake[0] == 'a', wake[0] == 'b');
        releaseAt = millis() + 90;
        releasePending = true;
    }
    const bool scripted = !script.empty();
    try {
        setup();
    } catch (SleepSignal&) {
        asleep = true;
    }
    while (*running) {
        const uint32_t now = millis();
        if (scripted && !runScript(now)) break;
        if (asleep) {
            // A or B wakes it: like the real stick, the firmware starts again from setup().
            if (!lgfx::gpio_in(kPinA) || !lgfx::gpio_in(kPinB)) {
                std::printf("[sim] wake\n");
                restartProgram(!lgfx::gpio_in(kPinA) ? "a" : "b");
            }
            ++loopsSinceChange;
            delay(5);
            continue;
        }
        try {
            loop();
            ++loopsSinceChange;
        } catch (SleepSignal&) {
            std::printf("[sim] deep sleep\n");
            asleep = true;
            M5.Display.fillScreen(TFT_BLACK);
        }
    }
    if (scripted) std::printf("[sim] %s (%d failure%s)\n", failures ? "FAILED" : "OK", failures,
                              failures == 1 ? "" : "s");
    std::fflush(stdout);
    std::exit(failures ? 1 : 0);
}

}  // namespace

int main(int, char** argv) {
    programArgv = argv;
    lgfx::Panel_sdl::addKeyCodeMapping(SDLK_a, kPinA);
    lgfx::Panel_sdl::addKeyCodeMapping(SDLK_b, kPinB);
    return lgfx::Panel_sdl::main(simMain, 128);
}
