#include "ui.h"

#include <M5Unified.h>

namespace ui {
namespace {

M5Canvas canvas(&M5.Display);

constexpr int kW = 240;
constexpr int kH = 135;
// The red letter always lands here, so the eye never has to move.
constexpr int kAnchorX = 88;
constexpr int kGuideTop = 30;
constexpr int kGuideBottom = 100;
constexpr int kWordMiddle = (kGuideTop + kGuideBottom) / 2;

uint16_t kBackground, kText, kDim, kHint, kFaint, kAccent, kSelected;

struct WordFont {
    const lgfx::IFont* font;
    int capHeight;
};
const WordFont kWordFonts[] = {
    {&fonts::FreeSansBold18pt7b, 25},
    {&fonts::FreeSansBold12pt7b, 17},
    {&fonts::FreeSansBold9pt7b, 13},
};

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
    canvas.drawFastHLine(12, kGuideTop, kW - 24, kFaint);
    canvas.drawFastHLine(12, kGuideBottom, kW - 24, kFaint);
    canvas.fillRect(kAnchorX - 1, kGuideTop, 2, 7, kAccent);
    canvas.fillRect(kAnchorX - 1, kGuideBottom - 6, 2, 7, kAccent);
}

void drawWord(const String& word, int pivot) {
    if (word.isEmpty()) return;
    const String before = word.substring(0, pivot);
    const String focus = word.substring(pivot, pivot + 1);
    const String after = word.substring(pivot + 1);

    // Largest font that keeps the word on screen with the focus letter on the anchor.
    const WordFont* chosen = &kWordFonts[2];
    for (const auto& f : kWordFonts) {
        canvas.setFont(f.font);
        const int left = canvas.textWidth(before) + canvas.textWidth(focus) / 2;
        const int right = canvas.textWidth(focus) - canvas.textWidth(focus) / 2 + canvas.textWidth(after);
        if (left <= kAnchorX - 4 && right <= kW - kAnchorX - 4) {
            chosen = &f;
            break;
        }
    }
    canvas.setFont(chosen->font);
    canvas.setTextDatum(textdatum_t::baseline_left);
    const int baseline = kWordMiddle + chosen->capHeight / 2;
    int x = kAnchorX - canvas.textWidth(before) - canvas.textWidth(focus) / 2;
    canvas.setTextColor(kText);
    canvas.drawString(before, x, baseline);
    x += canvas.textWidth(before);
    canvas.setTextColor(kAccent);
    canvas.drawString(focus, x, baseline);
    x += canvas.textWidth(focus);
    canvas.setTextColor(kText);
    canvas.drawString(after, x, baseline);
}

} // namespace

void begin(uint8_t rotation) {
    M5.Display.setRotation(rotation);
    canvas.setPsram(true);
    canvas.setColorDepth(16);
    canvas.createSprite(kW, kH);
    kBackground = canvas.color565(0, 0, 0);
    kText = canvas.color565(240, 240, 235);
    kDim = canvas.color565(140, 140, 140);
    kHint = canvas.color565(90, 90, 90);
    kFaint = canvas.color565(55, 55, 55);
    kAccent = canvas.color565(255, 72, 56);
    kSelected = canvas.color565(32, 32, 32);
}

void setRotation(uint8_t rotation) { M5.Display.setRotation(rotation); }

void drawReader(const rsvp::Reader& reader, const String& title) {
    canvas.fillScreen(kBackground);

    // Progress along the top edge.
    canvas.fillRect(0, 0, static_cast<int>(kW * reader.progress()), 2, kAccent);

    if (!reader.playing()) {
        canvas.setFont(&fonts::Font2);
        canvas.setTextDatum(textdatum_t::top_left);
        canvas.setTextColor(kDim);
        String t = title;
        if (t.endsWith(".txt")) t.remove(t.length() - 4);
        canvas.drawString(fitText(t, kW - 40), 6, 8);
        drawBattery(kW - 4, 11);
    }

    drawGuides();
    drawWord(reader.display().c_str(), reader.pivot());

    canvas.setFont(&fonts::Font2);
    canvas.setTextColor(kDim);
    canvas.setTextDatum(textdatum_t::bottom_left);
    canvas.drawString(String(reader.wpm()) + " wpm", 6, kH - 6);
    canvas.setTextDatum(textdatum_t::bottom_center);
    if (reader.finished()) {
        canvas.setTextColor(kAccent);
        canvas.drawString("the end", kW / 2, kH - 6);
        canvas.setTextColor(kDim);
    } else if (!reader.playing()) {
        canvas.drawString("paused", kW / 2, kH - 6);
    } else {
        canvas.drawString(String(static_cast<int>(reader.progress() * 100)) + "%", kW / 2, kH - 6);
    }
    canvas.setTextDatum(textdatum_t::bottom_right);
    canvas.drawString(formatDuration(reader.secondsLeft()) + " left", kW - 6, kH - 6);

    canvas.pushSprite(0, 0);
}

void drawLibrary(const std::vector<LibraryItem>& items, size_t selected) {
    constexpr int kRowH = 26;
    constexpr int kTop = 26;
    constexpr int kVisible = 4;

    canvas.fillScreen(kBackground);
    canvas.setFont(&fonts::Font2);
    canvas.setTextDatum(textdatum_t::top_left);
    canvas.setTextColor(kDim);
    canvas.drawString("Library", 8, 5);
    canvas.setTextDatum(textdatum_t::top_right);
    canvas.setTextColor(kHint);
    canvas.drawString("A open   B next", kW - 30, 5);
    drawBattery(kW - 4, 8);

    const size_t first = selected >= kVisible ? selected - kVisible + 1 : 0;
    for (size_t i = first; i < items.size() && i < first + kVisible; ++i) {
        const int y = kTop + static_cast<int>(i - first) * kRowH;
        const bool isSelected = i == selected;
        if (isSelected) {
            canvas.fillRoundRect(4, y, kW - 8, kRowH - 2, 4, kSelected);
            canvas.fillRect(4, y + 4, 3, kRowH - 10, kAccent);
        }
        canvas.setFont(isSelected ? &fonts::FreeSansBold9pt7b : &fonts::FreeSans9pt7b);
        canvas.setTextDatum(textdatum_t::middle_left);
        canvas.setTextColor(items[i].percent < 0 ? kAccent : (isSelected ? kText : kDim));
        const int rightSpace = items[i].percent >= 0 ? 44 : 12;
        canvas.drawString(fitText(items[i].title, kW - 16 - rightSpace), 14, y + kRowH / 2 - 1);
        if (items[i].percent >= 0) {
            canvas.setFont(&fonts::Font2);
            canvas.setTextDatum(textdatum_t::middle_right);
            canvas.setTextColor(kDim);
            canvas.drawString(String(items[i].percent) + "%", kW - 12, y + kRowH / 2 - 1);
        }
    }

    // Scroll hint when more rows are hidden below.
    if (first + kVisible < items.size()) canvas.fillTriangle(kW / 2 - 4, kH - 4, kW / 2 + 4, kH - 4, kW / 2, kH, kDim);

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
    canvas.setTextDatum(textdatum_t::top_left);
    canvas.setFont(&fonts::FreeSansBold9pt7b);
    canvas.setTextColor(kText);
    canvas.drawString("Add books", x, 8);

    canvas.setFont(&fonts::Font2);
    if (!phoneJoined) {
        canvas.setTextColor(kDim);
        canvas.drawString("1. Scan to join", x, 32);
        canvas.setTextColor(kText);
        canvas.drawString(fitText(ssid, kW - x - 4), x, 50);
        canvas.setTextColor(kDim);
        canvas.drawString("password", x, 68);
        canvas.setTextColor(kText);
        canvas.drawString(password, x, 84);
    } else {
        canvas.setTextColor(kDim);
        canvas.drawString("2. Scan or open", x, 32);
        canvas.setTextColor(kText);
        canvas.drawString(url.substring(7), x, 50);  // without "http://"
        canvas.setTextColor(kDim);
        canvas.drawString("in your browser", x, 68);
    }

    canvas.setTextColor(status.isEmpty() ? kDim : kAccent);
    canvas.drawString(status.isEmpty() ? "A: done" : fitText(status, kW - x - 4), x, 112);

    canvas.pushSprite(0, 0);
}

void drawMessage(const String& title, const String& body) {
    canvas.fillScreen(kBackground);
    canvas.setTextDatum(textdatum_t::middle_center);
    canvas.setFont(&fonts::FreeSansBold12pt7b);
    canvas.setTextColor(kAccent);
    canvas.drawString(title, kW / 2, 50);
    canvas.setFont(&fonts::Font2);
    canvas.setTextColor(kDim);
    canvas.drawString(fitText(body, kW - 16), kW / 2, 85);
    canvas.pushSprite(0, 0);
}

} // namespace ui
