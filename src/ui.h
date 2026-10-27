// Screens, drawn into an off-screen canvas and pushed in one go to avoid flicker.
// Every screen lays itself out for both landscape (240x135) and portrait (135x240).
#pragma once

#include <Arduino.h>
#include <vector>

#include "rsvp.h"

namespace ui {

constexpr const char* kTextSizes[] = {"Small", "Medium", "Large", "Extra large"};
constexpr uint8_t kTextSizeCount = 4;

constexpr const char* kThemes[] = {"Dark", "Light", "Sepia", "E-ink", "Night"};
constexpr uint8_t kThemeCount = 5;

struct MenuItem {
    String label;
    String value;  // shown right-aligned; empty for none
};

struct LibraryItem {
    String title;
    int percent;  // -1 for menu entries that are not books
};

void begin();
void setOrientation(bool portrait, bool flipped);
bool portrait();
void setTextSize(uint8_t size);
void setTheme(uint8_t theme);
// Longest piece of a word the reader should show at once on this screen.
size_t maxWordChunk();

void drawReader(const rsvp::Reader& reader, const String& title);
void drawLibrary(const std::vector<LibraryItem>& items, size_t selected);
void drawUpload(const String& ssid, const String& password, const String& url, bool phoneJoined,
                const String& status);
// A scrolling list. While `editing`, the selected row's value is framed to show
// that B changes it.
void drawMenu(const String& title, const String& hint, const std::vector<MenuItem>& items, size_t selected,
              bool editing = false);
void drawMessage(const String& title, const String& body);

} // namespace ui
