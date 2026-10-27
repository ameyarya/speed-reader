#include "upload.h"

#include <LittleFS.h>
#include <WebServer.h>
#include <WiFi.h>

#include "library.h"
#include "upload_page.h"

namespace upload {
namespace {

// Keep this much flash free so settings and the file system never run out.
constexpr size_t kReserveBytes = 32 * 1024;

WebServer server(80);
String ssid_, password_, status_;
bool libraryChanged_ = false;
bool running_ = false;

File uploadFile;
String uploadName;
bool uploadFailed = false;
String uploadError;

String randomPassword() {
    static const char kChars[] = "abcdefghjkmnpqrstuvwxyz23456789";
    String p;
    for (int i = 0; i < 8; ++i) p += kChars[esp_random() % (sizeof(kChars) - 1)];
    return p;
}

String jsonEscape(const String& s) {
    String out;
    for (size_t i = 0; i < s.length(); ++i) {
        const char c = s[i];
        if (c == '"' || c == '\\') out += '\\';
        if (static_cast<unsigned char>(c) >= 0x20) out += c;
    }
    return out;
}

String titleOf(const String& name) { return name.endsWith(".txt") ? name.substring(0, name.length() - 4) : name; }

void handleIndex() { server.send_P(200, "text/html; charset=utf-8", kUploadPage); }

void handleBooks() {
    String json = "{\"books\":[";
    bool first = true;
    for (const auto& b : library::list()) {
        if (!first) json += ',';
        first = false;
        json += "{\"name\":\"" + jsonEscape(b.name) + "\",\"size\":" + String(b.size) + "}";
    }
    json += "],\"free\":" + String(library::freeBytes()) + ",\"total\":" + String(library::totalBytes()) + "}";
    server.send(200, "application/json", json);
}

void handleUploadChunk() {
    HTTPUpload& up = server.upload();
    switch (up.status) {
    case UPLOAD_FILE_START: {
        uploadFailed = false;
        uploadError = "";
        uploadName = library::safeName(up.filename);
        const size_t expected = server.arg("size").toInt();
        // Replacing a book frees its old space first.
        size_t available = library::freeBytes();
        if (File old = LittleFS.open(library::pathFor(uploadName), "r")) {
            available += old.size();
            old.close();
        }
        if (expected + kReserveBytes > available) {
            uploadFailed = true;
            uploadError = "Not enough space on the stick";
            return;
        }
        uploadFile = LittleFS.open(library::pathFor(uploadName), "w");
        if (!uploadFile) {
            uploadFailed = true;
            uploadError = "Could not create the file";
        }
        status_ = "Receiving " + titleOf(uploadName);
        break;
    }
    case UPLOAD_FILE_WRITE:
        if (uploadFailed || !uploadFile) return;
        if (uploadFile.write(up.buf, up.currentSize) != up.currentSize) {
            uploadFailed = true;
            uploadError = "Stick is full";
            uploadFile.close();
            LittleFS.remove(library::pathFor(uploadName));
        }
        break;
    case UPLOAD_FILE_END:
        if (uploadFile) uploadFile.close();
        break;
    case UPLOAD_FILE_ABORTED:
        if (uploadFile) uploadFile.close();
        LittleFS.remove(library::pathFor(uploadName));
        uploadFailed = true;
        uploadError = "Upload interrupted";
        break;
    }
}

void handleUploadDone() {
    if (uploadFailed) {
        status_ = uploadError;
        server.send(400, "text/plain", uploadError);
        return;
    }
    library::savePosition(uploadName, 0);  // a replaced book starts over
    status_ = "Added " + titleOf(uploadName);
    libraryChanged_ = true;
    server.send(200, "text/plain", uploadName);
}

void handleDelete() {
    const String name = server.arg("name");
    if (name.isEmpty() || name.indexOf('/') >= 0 || !LittleFS.exists(library::pathFor(name))) {
        server.send(404, "text/plain", "No such book");
        return;
    }
    library::remove(name);
    status_ = "Removed " + titleOf(name);
    libraryChanged_ = true;
    server.send(200, "text/plain", "ok");
}

} // namespace

void begin() {
    if (running_) return;
    uint8_t mac[6];
    WiFi.macAddress(mac);
    char id[5];
    snprintf(id, sizeof(id), "%02X%02X", mac[4], mac[5]);
    ssid_ = String("SpeedReader-") + id;
    password_ = randomPassword();
    status_ = "";

    WiFi.mode(WIFI_AP);
    WiFi.softAP(ssid_.c_str(), password_.c_str());

    server.on("/", HTTP_GET, handleIndex);
    server.on("/api/books", HTTP_GET, handleBooks);
    server.on("/api/upload", HTTP_POST, handleUploadDone, handleUploadChunk);
    server.on("/api/delete", HTTP_POST, handleDelete);
    server.onNotFound([] { server.sendHeader("Location", "/"); server.send(302); });
    server.begin();
    running_ = true;
}

void loop() {
    if (running_) server.handleClient();
}

void end() {
    if (!running_) return;
    server.stop();
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_OFF);
    running_ = false;
}

const String& ssid() { return ssid_; }
const String& password() { return password_; }
String url() { return "http://" + WiFi.softAPIP().toString(); }
bool phoneJoined() { return WiFi.softAPgetStationNum() > 0; }
const String& status() { return status_; }

bool takeLibraryChanged() {
    const bool changed = libraryChanged_;
    libraryChanged_ = false;
    return changed;
}

} // namespace upload
