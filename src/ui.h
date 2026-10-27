// Screens, drawn into an off-screen canvas and pushed in one go to avoid flicker.
#pragma once

#include <Arduino.h>
#include <vector>

#include "rsvp.h"

namespace ui {

struct LibraryItem {
    String title;
    int percent;  // -1 for menu entries that are not books
};

void begin(uint8_t rotation);
void setRotation(uint8_t rotation);

void drawReader(const rsvp::Reader& reader, const String& title);
void drawLibrary(const std::vector<LibraryItem>& items, size_t selected);
void drawUpload(const String& ssid, const String& password, const String& url, bool phoneJoined,
                const String& status);
void drawMessage(const String& title, const String& body);

} // namespace ui
