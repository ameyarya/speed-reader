#include "ui.h"

#include <M5Unified.h>

namespace ui {
namespace {

M5Canvas canvas(&M5.Display);

struct Theme {
    uint8_t bg[3], text[3], dim[3], hint[3], faint[3], accent[3], selected[3];
    bool underlinePivot;  // for themes where the accent alone is hard to see
};

// Same order as kThemes.
constexpr Theme kThemeTable[] = {
    // Dark
    {{0, 0, 0}, {240, 240, 235}, {140, 140, 140}, {90, 90, 90}, {55, 55, 55}, {255, 72, 56}, {32, 32, 32}, false},
    // Light
    {{250, 250, 247}, {20, 20, 20}, {105, 105, 105}, {150, 150, 150}, {205, 205, 200}, {215, 45, 35}, {228, 228, 222}, false},
    // Sepia
    {{244, 234, 212}, {62, 46, 30}, {125, 104, 80}, {160, 140, 112}, {212, 196, 166}, {186, 66, 36}, {230, 216, 188}, false},
    // E-ink: paper grey and black only, so the focus letter is underlined instead of coloured
    {{226, 226, 218}, {12, 12, 12}, {80, 80, 80}, {120, 120, 116}, {175, 175, 168}, {12, 12, 12}, {200, 200, 192}, true},
    // Night: dim red on black, easy on the eyes in the dark
    {{0, 0, 0}, {200, 70, 50}, {130, 45, 32}, {90, 32, 24}, {55, 20, 15}, {255, 165, 60}, {34, 12, 8}, false},
};

uint16_t kBackground, kText, kDim, kHint, kFaint, kAccent, kSelected;
bool kUnderlinePivot = false;

// The stick is used sideways: 240x135.
struct Layout {
    int w, h;
    // Reader
    int anchorX, guideTop, guideBottom;
    // Lists
    int listTop, rowH, visibleRows;
};
constexpr Layout L{240, 135, 88, 30, 100, 26, 26, 4};

struct WordFont {
    const lgfx::IFont* font;
    int capHeight;
};
// Largest first. The text size setting picks where to start; long words step down.
const WordFont kWordFonts[] = {
    {&fonts::FreeSansBold24pt7b, 34},
    {&fonts::FreeSansBold18pt7b, 25},
    {&fonts::FreeSansBold12pt7b, 17},
    {&fonts::FreeSansBold9pt7b, 13},
};
constexpr size_t kFontCount = sizeof(kWordFonts) / sizeof(kWordFonts[0]);
size_t largestFont = 1;

String formatDuration(uint32_t seconds) {
    if (seconds < 60) return "<1m";
    const uint32_t minutes = seconds / 60;
    if (minutes < 60) return String(minutes) + "m";
    return String(minutes / 60) + "h " + String(minutes % 60) + "m";
}

String fitText(const String& text, int maxWidth) {
    if (canvas.textWidth(text) <= maxWidth) return text;
    String cut = text;
    while (cut.length() > 1 && canvas.textWidth(cut + "...") > maxWidth) cut.remove(cut.length() - 1);
    return cut + "...";
}

// Centered text broken into lines at spaces. Returns the y after the last line.
int drawWrapped(const String& text, int cx, int y, int maxWidth, int lineH) {
    canvas.setTextDatum(textdatum_t::top_center);
    String line;
    int start = 0;
    while (start <= static_cast<int>(text.length())) {
        int space = text.indexOf(' ', start);
        if (space < 0) space = text.length();
        const String word = text.substring(start, space);
        const String candidate = line.isEmpty() ? word : line + " " + word;
        if (!line.isEmpty() && canvas.textWidth(candidate) > maxWidth) {
            canvas.drawString(line, cx, y);
            y += lineH;
            line = word;
        } else {
            line = candidate;
        }
        start = space + 1;
    }
    if (!line.isEmpty()) {
        canvas.drawString(fitText(line, maxWidth), cx, y);
        y += lineH;
    }
    return y;
}

void drawBattery(int right, int y) {
    const int level = M5.Power.getBatteryLevel();
    if (level < 0) return;
    const int w = 18, h = 9, x = right - w - 2;
    canvas.drawRect(x, y, w, h, kDim);
    canvas.fillRect(x + w, y + 3, 2, 3, kDim);
    const int fill = (w - 4) * std::min(level, 100) / 100;
    canvas.fillRect(x + 2, y + 2, fill, h - 4, level <= 15 ? kAccent : kDim);
}

void drawGuides() {
    canvas.drawFastHLine(12, L.guideTop, L.w - 24, kFaint);
    canvas.drawFastHLine(12, L.guideBottom, L.w - 24, kFaint);
    canvas.fillRect(L.anchorX - 1, L.guideTop, 2, 7, kAccent);
    canvas.fillRect(L.anchorX - 1, L.guideBottom - 6, 2, 7, kAccent);
}

void drawWord(const String& word, int pivot) {
    if (word.isEmpty()) return;
    const String before = word.substring(0, pivot);
    const String focus = word.substring(pivot, pivot + 1);
    const String after = word.substring(pivot + 1);

    // Largest font that keeps the word on screen with the focus letter on the anchor.
    const WordFont* chosen = &kWordFonts[kFontCount - 1];
    for (size_t i = largestFont; i < kFontCount; ++i) {
        const auto& f = kWordFonts[i];
        canvas.setFont(f.font);
        const int left = canvas.textWidth(before) + canvas.textWidth(focus) / 2;
        const int right = canvas.textWidth(focus) - canvas.textWidth(focus) / 2 + canvas.textWidth(after);
        if (left <= L.anchorX - 3 && right <= L.w - L.anchorX - 3) {
            chosen = &f;
            break;
        }
    }
    canvas.setFont(chosen->font);
    canvas.setTextDatum(textdatum_t::baseline_left);
    const int baseline = (L.guideTop + L.guideBottom) / 2 + chosen->capHeight / 2;
    int x = L.anchorX - canvas.textWidth(before) - canvas.textWidth(focus) / 2;
    canvas.setTextColor(kText);
    canvas.drawString(before, x, baseline);
    x += canvas.textWidth(before);
    canvas.setTextColor(kAccent);
    canvas.drawString(focus, x, baseline);
    if (kUnderlinePivot) canvas.fillRect(x, baseline + 4, canvas.textWidth(focus), 3, kAccent);
    x += canvas.textWidth(focus);
    canvas.setTextColor(kText);
    canvas.drawString(after, x, baseline);
}

void drawSelectedRow(int y) {
    canvas.fillRoundRect(4, y, L.w - 8, L.rowH - 2, 4, kSelected);
    canvas.fillRect(4, y + 4, 3, L.rowH - 10, kAccent);
}

// Header for lists: title on the left, then the hint, then the battery.
void drawListHeader(const String& title, const String& hint, bool hintIsActive) {
    canvas.setFont(&fonts::Font2);
    canvas.setTextDatum(textdatum_t::top_right);
    canvas.setTextColor(hintIsActive ? kAccent : kHint);
    canvas.drawString(hint, L.w - 30, 5);
    // A long hint (like the one while changing a setting) takes the title's place.
    const int room = L.w - 46 - canvas.textWidth(hint);
    canvas.setTextDatum(textdatum_t::top_left);
    canvas.setTextColor(kDim);
    if (canvas.textWidth(title) <= room || room >= 60) canvas.drawString(fitText(title, room), 8, 5);
    drawBattery(L.w - 4, 8);
}

void drawScrollHint(size_t first, size_t count) {
    if (first + L.visibleRows < count)
        canvas.fillTriangle(L.w / 2 - 4, L.h - 4, L.w / 2 + 4, L.h - 4, L.w / 2, L.h, kDim);
}

size_t firstVisible(size_t selected) {
    return selected >= static_cast<size_t>(L.visibleRows) ? selected - L.visibleRows + 1 : 0;
}

void applyTheme(uint8_t theme) {
    const Theme& t = kThemeTable[std::min<uint8_t>(theme, kThemeCount - 1)];
    auto c = [](const uint8_t rgb[3]) { return canvas.color565(rgb[0], rgb[1], rgb[2]); };
    kBackground = c(t.bg);
    kText = c(t.text);
    kDim = c(t.dim);
    kHint = c(t.hint);
    kFaint = c(t.faint);
    kAccent = c(t.accent);
    kSelected = c(t.selected);
    kUnderlinePivot = t.underlinePivot;
}

} // namespace

void begin() {
    canvas.setPsram(true);
    canvas.setColorDepth(16);
    applyTheme(0);
}

void setFlipped(bool flipped) {
    M5.Display.setRotation(flipped ? 3 : 1);
    if (canvas.width() == 0) canvas.createSprite(L.w, L.h);
}

void setTextSize(uint8_t size) {
    // Extra large starts at the 24pt font, Small at 9pt.
    largestFont = kTextSizeCount - 1 - std::min<uint8_t>(size, kTextSizeCount - 1);
}

void setTheme(uint8_t theme) { applyTheme(theme); }

void drawReader(const rsvp::Reader& reader, const String& title) {
    canvas.fillScreen(kBackground);
    const int percent = static_cast<int>(reader.progress() * 100);

    // Progress along the top edge.
    canvas.fillRect(0, 0, static_cast<int>(L.w * reader.progress()), 2, kAccent);

    if (!reader.playing()) {
        canvas.setFont(&fonts::Font2);
        canvas.setTextDatum(textdatum_t::top_left);
        canvas.setTextColor(kDim);
        String t = title;
        if (t.endsWith(".txt")) t.remove(t.length() - 4);
        const String stats = String(percent) + "%  " + String(reader.wpm()) + "wpm";
        canvas.setTextDatum(textdatum_t::top_right);
        canvas.drawString(stats, L.w - 28, 8);
        canvas.setTextDatum(textdatum_t::top_left);
        canvas.drawString(fitText(t, L.w - 40 - canvas.textWidth(stats) - 6), 6, 8);
        drawBattery(L.w - 4, 11);
    }

    drawGuides();
    drawWord(reader.display().c_str(), reader.pivot());

    canvas.setFont(&fonts::Font2);
    canvas.setTextDatum(textdatum_t::bottom_center);
    if (!reader.playing()) {
        // Paused: show what the buttons do instead of stats.
        canvas.setTextColor(reader.finished() ? kAccent : kDim);
        canvas.drawString(reader.finished() ? "the end   A: read again" : "A play   B back   hold A: menu",
                          L.w / 2, L.h - 6);
        canvas.pushSprite(0, 0);
        return;
    }

    canvas.setTextColor(kDim);
    canvas.setTextDatum(textdatum_t::bottom_left);
    canvas.drawString(String(reader.wpm()) + " wpm", 6, L.h - 6);
    canvas.setTextDatum(textdatum_t::bottom_center);
    canvas.drawString(String(percent) + "%", L.w / 2, L.h - 6);
    canvas.setTextDatum(textdatum_t::bottom_right);
    canvas.drawString(formatDuration(reader.secondsLeft()) + " left", L.w - 6, L.h - 6);
    canvas.pushSprite(0, 0);
}

void drawLibrary(const std::vector<LibraryItem>& items, size_t selected) {
    canvas.fillScreen(kBackground);
    drawListHeader("Library", "A open   B next", false);

    const size_t first = firstVisible(selected);
    for (size_t i = first; i < items.size() && i < first + L.visibleRows; ++i) {
        const int y = L.listTop + static_cast<int>(i - first) * L.rowH;
        const int mid = y + L.rowH / 2 - 1;
        const bool isSelected = i == selected;
        if (isSelected) drawSelectedRow(y);
        const bool isBook = items[i].percent >= 0;
        int rightSpace = 12;
        if (isBook) {
            canvas.setFont(&fonts::Font2);
            canvas.setTextDatum(textdatum_t::middle_right);
            canvas.setTextColor(kDim);
            const String value = String(items[i].percent) + "%";
            canvas.drawString(value, L.w - 12, mid);
            rightSpace = canvas.textWidth(value) + 18;
        }
        canvas.setFont(isSelected ? &fonts::FreeSansBold9pt7b : &fonts::FreeSans9pt7b);
        canvas.setTextDatum(textdatum_t::middle_left);
        canvas.setTextColor(!isBook ? kAccent : (isSelected ? kText : kDim));
        canvas.drawString(fitText(items[i].title, L.w - 14 - rightSpace), 14, mid);
    }
    drawScrollHint(first, items.size());
    canvas.pushSprite(0, 0);
}

void drawMenu(const String& title, const String& hint, const std::vector<MenuItem>& items, size_t selected,
              bool editing) {
    canvas.fillScreen(kBackground);
    drawListHeader(title, hint, editing);

    const size_t first = firstVisible(selected);
    for (size_t i = first; i < items.size() && i < first + L.visibleRows; ++i) {
        const int y = L.listTop + static_cast<int>(i - first) * L.rowH;
        const int mid = y + L.rowH / 2 - 1;
        const bool isSelected = i == selected;
        if (isSelected) drawSelectedRow(y);
        int valueWidth = 0;
        if (!items[i].value.isEmpty()) {
            canvas.setFont(&fonts::Font2);
            const String value = fitText(items[i].value, L.w / 2 - 10);
            valueWidth = canvas.textWidth(value);
            const int right = L.w - 14;
            if (isSelected && editing) {
                canvas.drawRoundRect(right - valueWidth - 6, y + 3, valueWidth + 12, L.rowH - 8, 4, kAccent);
                canvas.setTextColor(kText);
            } else {
                canvas.setTextColor(kDim);
            }
            canvas.setTextDatum(textdatum_t::middle_right);
            canvas.drawString(value, right, mid);
            valueWidth += 14;
        }
        canvas.setFont(isSelected ? &fonts::FreeSansBold9pt7b : &fonts::FreeSans9pt7b);
        canvas.setTextDatum(textdatum_t::middle_left);
        canvas.setTextColor(isSelected ? kText : kDim);
        canvas.drawString(fitText(items[i].label, L.w - 26 - valueWidth), 14, mid);
    }
    drawScrollHint(first, items.size());
    canvas.pushSprite(0, 0);
}

void drawUpload(const String& ssid, const String& password, const String& url, bool phoneJoined,
                const String& status) {
    canvas.fillScreen(kBackground);

    // Step 1 shows a Wi-Fi join code; once a phone is connected, a link to the page.
    const String code = phoneJoined ? url : "WIFI:T:WPA;S:" + ssid + ";P:" + password + ";;";
    canvas.fillRect(4, 8, 118, 118, TFT_WHITE);
    canvas.qrcode(code.c_str(), 9, 13, 108, 3);

    const int x = 130;
    const int textW = L.w - x - 4;
    const int line = 17;
    int y = 8;

    canvas.setTextDatum(textdatum_t::top_left);
    canvas.setFont(&fonts::FreeSansBold9pt7b);
    canvas.setTextColor(kText);
    canvas.drawString("Add books", x, y);
    y += line + 6;

    canvas.setFont(&fonts::Font2);
    if (!phoneJoined) {
        canvas.setTextColor(kDim);
        canvas.drawString("1. Scan to join", x, y);
        y += line;
        canvas.setTextColor(kText);
        canvas.drawString(fitText(ssid, textW), x, y);
        y += line;
        canvas.setTextColor(kDim);
        canvas.drawString("password", x, y);
        canvas.setTextColor(kText);
        canvas.drawString(password, x, y + line - 2);
    } else {
        canvas.setTextColor(kDim);
        canvas.drawString("2. Scan or open", x, y);
        y += line;
        canvas.setTextColor(kText);
        canvas.drawString(url.substring(7), x, y);  // without "http://"
        y += line;
        canvas.setTextColor(kDim);
        canvas.drawString("in your browser", x, y);
    }

    // The way out always stays visible, with the latest upload news above it.
    const int bottom = L.h - 6;
    canvas.setTextDatum(textdatum_t::bottom_left);
    if (!status.isEmpty()) {
        canvas.setTextColor(kAccent);
        canvas.drawString(fitText(status, textW), x, bottom - 19);
    }
    canvas.setTextColor(kText);
    canvas.drawString("Press A: done", x, bottom);
    canvas.pushSprite(0, 0);
}

void drawMessage(const String& title, const String& body) {
    canvas.fillScreen(kBackground);
    canvas.setFont(&fonts::FreeSansBold12pt7b);
    canvas.setTextColor(kAccent);
    int y = drawWrapped(title, L.w / 2, 28, L.w - 16, 26);
    canvas.setFont(&fonts::Font2);
    canvas.setTextColor(kDim);
    drawWrapped(body, L.w / 2, y + 8, L.w - 20, 18);
    canvas.setTextColor(kHint);
    canvas.setTextDatum(textdatum_t::bottom_center);
    canvas.drawString("press A", L.w / 2, L.h - 8);
    canvas.pushSprite(0, 0);
}

} // namespace ui
