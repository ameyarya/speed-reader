// Books on the stick's flash (LittleFS) and reading state in NVS.
#pragma once

#include <Arduino.h>
#include <vector>

struct Book {
    String name;  // file name inside /books, e.g. "Dracula.txt"
    size_t size;
};

namespace library {

bool begin();
std::vector<Book> list();

// Reads a whole book into PSRAM. Caller frees with free(). nullptr on failure.
char* load(const String& name, size_t& size);
bool remove(const String& name);

// Turns an uploaded file name into a safe name ending in .txt.
String safeName(const String& uploaded);
String pathFor(const String& name);

size_t freeBytes();
size_t totalBytes();

size_t savedPosition(const String& name);
void savePosition(const String& name, size_t position);

String lastBook();
void setLastBook(const String& name);

uint16_t wpm();
void setWpm(uint16_t wpm);

bool portrait();
void setPortrait(bool portrait);
// Upside down (hold A+B to flip).
bool flipped();
void setFlipped(bool flipped);

// Index into ui::kThemes.
uint8_t theme();
void setTheme(uint8_t theme);

// 1 (dimmest) to 5.
uint8_t brightness();
void setBrightness(uint8_t level);

// Index into ui::kTextSizes.
uint8_t textSize();
void setTextSize(uint8_t size);

} // namespace library
