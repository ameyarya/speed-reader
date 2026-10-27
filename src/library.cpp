#include "library.h"

#include <LittleFS.h>
#include <Preferences.h>
#include <algorithm>

#include "welcome.h"

namespace library {
namespace {

constexpr const char* kBooksDir = "/books";
constexpr size_t kMaxNameChars = 40;

Preferences settings;   // "reader": wpm, last book, rotation
Preferences positions;  // "positions": one key per book

// NVS keys are limited to 15 characters, so books are keyed by a hash of their name.
String positionKey(const String& name) {
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < name.length(); ++i) h = (h ^ static_cast<uint8_t>(name[i])) * 16777619u;
    char key[12];
    snprintf(key, sizeof(key), "p%08lx", static_cast<unsigned long>(h));
    return key;
}

void seedWelcomeBook() {
    File f = LittleFS.open(pathFor("Welcome.txt"), "w");
    if (!f) return;
    f.print(kWelcomeText);
    f.close();
}

} // namespace

bool begin() {
    settings.begin("reader", false);
    positions.begin("positions", false);
    if (!LittleFS.begin(true)) return false;  // formats on first boot
    if (!LittleFS.exists(kBooksDir)) {
        LittleFS.mkdir(kBooksDir);
        seedWelcomeBook();
    } else if (File f = LittleFS.open(pathFor("Welcome.txt"), "r")) {
        // Refresh the guide after a firmware update changes it (unless it was removed).
        const bool outdated = f.size() != strlen(kWelcomeText);
        f.close();
        if (outdated) seedWelcomeBook();
    }
    return true;
}

String pathFor(const String& name) { return String(kBooksDir) + "/" + name; }

std::vector<Book> list() {
    std::vector<Book> books;
    File dir = LittleFS.open(kBooksDir);
    if (!dir) return books;
    for (File f = dir.openNextFile(); f; f = dir.openNextFile()) {
        if (!f.isDirectory()) books.push_back({f.name(), f.size()});
        f.close();
    }
    std::sort(books.begin(), books.end(), [](const Book& a, const Book& b) {
        return a.name.compareTo(b.name) < 0;
    });
    return books;
}

char* load(const String& name, size_t& size) {
    File f = LittleFS.open(pathFor(name), "r");
    if (!f) return nullptr;
    size = f.size();
    char* data = static_cast<char*>(ps_malloc(size + 1));
    if (!data) {
        f.close();
        return nullptr;
    }
    size_t read = 0;
    while (read < size) {
        const size_t n = f.read(reinterpret_cast<uint8_t*>(data + read), size - read);
        if (n == 0) break;
        read += n;
    }
    f.close();
    size = read;
    data[size] = '\0';
    return data;
}

bool remove(const String& name) {
    positions.remove(positionKey(name).c_str());
    return LittleFS.remove(pathFor(name));
}

String safeName(const String& uploaded) {
    String base = uploaded;
    const int slash = std::max(base.lastIndexOf('/'), base.lastIndexOf('\\'));
    if (slash >= 0) base = base.substring(slash + 1);
    const int dot = base.lastIndexOf('.');
    if (dot > 0) base = base.substring(0, dot);

    String out;
    for (size_t i = 0; i < base.length() && out.length() < kMaxNameChars - 4; ++i) {
        const char c = base[i];
        const bool ok = isalnum(static_cast<unsigned char>(c)) || c == ' ' || c == '-' || c == '_' ||
                        c == '(' || c == ')' || c == ',' || c == '\'';
        out += ok ? c : '_';
    }
    out.trim();
    if (out.isEmpty()) out = "Book";
    return out + ".txt";
}

size_t freeBytes() { return LittleFS.totalBytes() - LittleFS.usedBytes(); }
size_t totalBytes() { return LittleFS.totalBytes(); }

size_t savedPosition(const String& name) { return positions.getUInt(positionKey(name).c_str(), 0); }

void savePosition(const String& name, size_t position) {
    const String key = positionKey(name);
    if (positions.getUInt(key.c_str(), UINT32_MAX) != position) positions.putUInt(key.c_str(), position);
}

String lastBook() { return settings.getString("last", ""); }
void setLastBook(const String& name) {
    if (settings.getString("last", "") != name) settings.putString("last", name);
}

uint16_t wpm() { return settings.getUShort("wpm", 300); }
void setWpm(uint16_t wpm) {
    if (settings.getUShort("wpm", 0) != wpm) settings.putUShort("wpm", wpm);
}

uint8_t rotation() { return settings.getUChar("rot", 1); }
void setRotation(uint8_t rotation) { settings.putUChar("rot", rotation); }

uint8_t brightness() { return settings.getUChar("bright", 3); }
void setBrightness(uint8_t level) {
    if (settings.getUChar("bright", 0) != level) settings.putUChar("bright", level);
}

uint8_t textSize() { return settings.getUChar("size", 2); }
void setTextSize(uint8_t size) {
    if (settings.getUChar("size", 255) != size) settings.putUChar("size", size);
}

} // namespace library
