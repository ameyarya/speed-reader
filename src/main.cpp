// Speed Reader for the M5StickS3: one word at a time, with the focus letter fixed in place.
//
// Lists (library, menu, chapters, settings): B next, hold B previous, A choose
// Reading:  A play/pause, hold A for the menu (chapters, settings, library)
//           playing: B faster, hold B slower
//           paused:  B back a sentence, hold B forward a sentence
// Settings: A on a setting to change it, then B up, hold B down, A done
// Anywhere: hold A+B to flip the screen, power button to sleep (A or B wakes)
#include <M5Unified.h>
#include <driver/gpio.h>
#include <driver/rtc_io.h>
#include <esp_sleep.h>

#include "library.h"
#include "rsvp.h"
#include "ui.h"
#include "upload.h"

namespace {

enum class Mode { Library, Reading, Menu, Chapters, Settings, Upload, Message };

// Display backlight for brightness settings 1..5.
constexpr uint8_t kBrightnessLevels[] = {0, 12, 40, 90, 160, 255};
constexpr uint8_t kMaxBrightness = 5;
constexpr uint8_t kDimBrightness = 8;
constexpr uint32_t kDimAfterMs = 60 * 1000;
constexpr uint32_t kSleepAfterMs = 10 * 60 * 1000;
// StickS3 wiring: front button A, side button B, display backlight.
constexpr gpio_num_t kButtonAPin = GPIO_NUM_11;
constexpr gpio_num_t kButtonBPin = GPIO_NUM_12;
constexpr gpio_num_t kBacklightPin = GPIO_NUM_38;
constexpr uint32_t kSaveEveryMs = 20 * 1000;
constexpr uint32_t kRepeatMs = 280;
constexpr uint32_t kFlipHoldMs = 800;
constexpr uint32_t kStatusRefreshMs = 30 * 1000;

Mode mode = Mode::Library;
rsvp::Reader reader;
char* bookData = nullptr;
String bookName;

std::vector<Book> books;
size_t librarySelected = 0;

// Reader menu
enum MenuEntry : size_t { kMenuResume, kMenuChapters, kMenuSettings, kMenuLibrary, kMenuCount };
size_t menuSelected = 0;

size_t chapterSelected = 0;

// Settings
enum SettingEntry : size_t {
    kSetTheme, kSetBrightness, kSetSpeed, kSetTextSize, kSetOrientation, kSetBack, kSetCount
};
size_t settingSelected = 0;
bool editingSetting = false;
Mode settingsReturn = Mode::Library;

Mode messageReturn = Mode::Library;
String messageTitle, messageBody;

bool portrait = false;
bool flipped = false;
uint8_t theme = 0;
uint8_t brightness = 3;
uint8_t textSize = 2;
uint16_t wpm = 300;

bool redraw = true;
bool dimmed = false;
bool ignoreUntilRelease = false;
bool combo = false;
bool comboFlipped = false;
uint32_t comboStartMs = 0;
uint32_t lastInputMs = 0;
uint32_t lastSaveMs = 0;
uint32_t nextRepeatMs = 0;
uint32_t lastStatusDrawMs = 0;

bool uploadJoined = false;
String uploadStatus;

// Library rows: books, then these two.
size_t addBooksRow() { return books.size(); }
size_t settingsRow() { return books.size() + 1; }
size_t libraryCount() { return books.size() + 2; }

String titleOf(const String& name) { return name.endsWith(".txt") ? name.substring(0, name.length() - 4) : name; }

void step(size_t& index, size_t count, int direction) { index = (index + count + direction) % count; }

void applyBrightness() { M5.Display.setBrightness(kBrightnessLevels[brightness]); }

void applyOrientation() {
    ui::setOrientation(portrait, flipped);
    reader.setMaxChunk(ui::maxWordChunk());
}

void refreshBooks() {
    books = library::list();
    if (librarySelected >= libraryCount()) librarySelected = libraryCount() - 1;
}

void saveProgress() {
    if (!bookData) return;
    library::savePosition(bookName, reader.position());
    library::setWpm(wpm);
    lastSaveMs = millis();
}

void closeBook() {
    if (!bookData) return;
    reader.pause();
    saveProgress();
    free(bookData);
    bookData = nullptr;
}

void showMessage(const String& title, const String& body, Mode returnTo) {
    messageTitle = title;
    messageBody = body;
    messageReturn = returnTo;
    mode = Mode::Message;
    redraw = true;
}

void openBook(const String& name) {
    closeBook();
    size_t size = 0;
    bookData = library::load(name, size);
    if (!bookData) {
        showMessage("Can't open book", "It may be too big for memory", Mode::Library);
        return;
    }
    bookName = name;
    reader.setWpm(wpm);
    reader.setMaxChunk(ui::maxWordChunk());
    reader.open(bookData, size, library::savedPosition(name));
    library::setLastBook(name);
    mode = Mode::Reading;
    redraw = true;
}

void goToLibrary() {
    closeBook();
    refreshBooks();
    for (size_t i = 0; i < books.size(); ++i)
        if (books[i].name == bookName) librarySelected = i;
    mode = Mode::Library;
    redraw = true;
}

void openMenu() {
    reader.pause();
    saveProgress();
    menuSelected = kMenuResume;
    mode = Mode::Menu;
    redraw = true;
}

void openChapters() {
    if (reader.chapters().empty()) {
        showMessage("No chapters", "Re-upload the EPUB to add chapters", Mode::Menu);
        return;
    }
    const int current = reader.currentChapter();
    chapterSelected = current < 0 ? 0 : static_cast<size_t>(current);
    mode = Mode::Chapters;
    redraw = true;
}

void openSettings(Mode returnTo) {
    settingsReturn = returnTo;
    settingSelected = 0;
    editingSetting = false;
    mode = Mode::Settings;
    redraw = true;
}

void closeSettings() {
    editingSetting = false;
    library::setBrightness(brightness);
    library::setWpm(wpm);
    library::setTextSize(textSize);
    library::setTheme(theme);
    library::setPortrait(portrait);
    mode = settingsReturn;
    redraw = true;
}

void startUpload() {
    upload::begin();
    uploadJoined = false;
    uploadStatus = "";
    mode = Mode::Upload;
    redraw = true;
}

void leaveUpload() {
    const String added = upload::lastAdded();
    upload::end();
    goToLibrary();
    // Land on the book that was just added, ready to open with A.
    for (size_t i = 0; i < books.size(); ++i)
        if (books[i].name == added) librarySelected = i;
}

void changeSetting(int direction) {
    switch (settingSelected) {
    case kSetTheme:
        theme = static_cast<uint8_t>((theme + ui::kThemeCount + direction) % ui::kThemeCount);
        ui::setTheme(theme);
        break;
    case kSetOrientation:
        portrait = !portrait;
        applyOrientation();
        break;
    case kSetBrightness:
        brightness = static_cast<uint8_t>(std::clamp<int>(brightness + direction, 1, kMaxBrightness));
        applyBrightness();
        break;
    case kSetSpeed:
        wpm = static_cast<uint16_t>(std::clamp<int>(wpm + direction * rsvp::Reader::kWpmStep,
                                                    rsvp::Reader::kMinWpm, rsvp::Reader::kMaxWpm));
        reader.setWpm(wpm);
        break;
    case kSetTextSize:
        textSize = static_cast<uint8_t>((textSize + ui::kTextSizeCount + direction) % ui::kTextSizeCount);
        ui::setTextSize(textSize);
        break;
    }
}

// ---- Drawing ----------------------------------------------------------------

void drawLibrary() {
    std::vector<ui::LibraryItem> items;
    for (const auto& b : books) {
        const size_t pos = library::savedPosition(b.name);
        items.push_back({titleOf(b.name), b.size ? static_cast<int>(static_cast<uint64_t>(pos) * 100 / b.size) : 0});
    }
    items.push_back({"+ Add books (Wi-Fi)", -1});
    items.push_back({"Settings", -1});
    ui::drawLibrary(items, librarySelected);
}

void drawMenu() {
    const size_t count = reader.chapters().size();
    const int current = reader.currentChapter();
    String chapters = count ? String(current + 1 > 0 ? current + 1 : 1) + " of " + String(count) : "none";
    ui::drawMenu(titleOf(bookName), "hold A: resume",
                 {{"Resume", String(static_cast<int>(reader.progress() * 100)) + "%"},
                  {"Chapters", chapters},
                  {"Settings", ""},
                  {"Library", ""}},
                 menuSelected);
}

void drawChapters() {
    std::vector<ui::MenuItem> items;
    const int current = reader.currentChapter();
    for (size_t i = 0; i < reader.chapters().size(); ++i) {
        const auto& c = reader.chapters()[i];
        items.push_back({c.title.c_str(), static_cast<int>(i) == current ? "here" : ""});
    }
    ui::drawMenu("Chapters", "A go   hold A: back", items, chapterSelected);
}

void drawSettings() {
    const bool narrow = ui::portrait();
    std::vector<ui::MenuItem> items = {
        {"Theme", ui::kThemes[theme]},
        {"Brightness", String(brightness) + " of " + String(kMaxBrightness)},
        {narrow ? "Speed" : "Reading speed", String(wpm) + " wpm"},
        {"Text size", ui::kTextSizes[textSize]},
        {"Orientation", portrait ? "Vertical" : "Horizontal"},
        {"Back", ""},
    };
    const char* hint = editingSetting ? (narrow ? "B up  hold B down" : "B up  hold B down  A done")
                                      : (narrow ? "A change  hold A back" : "A change   hold A: back");
    ui::drawMenu("Settings", hint, items, settingSelected, editingSetting);
}

void draw() {
    switch (mode) {
    case Mode::Library: drawLibrary(); break;
    case Mode::Reading: ui::drawReader(reader, bookName); break;
    case Mode::Menu: drawMenu(); break;
    case Mode::Chapters: drawChapters(); break;
    case Mode::Settings: drawSettings(); break;
    case Mode::Upload:
        ui::drawUpload(upload::ssid(), upload::password(), upload::url(), uploadJoined, uploadStatus);
        break;
    case Mode::Message: ui::drawMessage(messageTitle, messageBody); break;
    }
    lastStatusDrawMs = millis();
}

// ---- Input ------------------------------------------------------------------

void onAClick() {
    switch (mode) {
    case Mode::Library:
        if (librarySelected < books.size()) openBook(books[librarySelected].name);
        else if (librarySelected == addBooksRow()) startUpload();
        else openSettings(Mode::Library);
        break;
    case Mode::Reading:
        if (reader.finished()) {
            reader.restart();
            reader.play(millis());
        } else {
            reader.toggle(millis());
            if (!reader.playing()) saveProgress();
        }
        break;
    case Mode::Menu:
        switch (menuSelected) {
        case kMenuResume: mode = Mode::Reading; break;
        case kMenuChapters: openChapters(); break;
        case kMenuSettings: openSettings(Mode::Menu); break;
        case kMenuLibrary: goToLibrary(); break;
        }
        break;
    case Mode::Chapters:
        reader.goToChapter(chapterSelected);
        saveProgress();
        mode = Mode::Reading;  // paused on the chapter's first word
        break;
    case Mode::Settings:
        if (settingSelected == kSetBack) closeSettings();
        else editingSetting = !editingSetting;
        break;
    case Mode::Upload: leaveUpload(); break;
    case Mode::Message: mode = messageReturn; break;
    }
    redraw = true;
}

void onAHold() {
    switch (mode) {
    case Mode::Reading: openMenu(); break;
    case Mode::Menu: mode = Mode::Reading; break;
    case Mode::Chapters: mode = Mode::Menu; break;
    case Mode::Settings: closeSettings(); break;
    default: return;
    }
    redraw = true;
}

void onBClick() {
    switch (mode) {
    case Mode::Library: step(librarySelected, libraryCount(), +1); break;
    case Mode::Reading:
        if (reader.playing()) {
            reader.adjustWpm(+1);
            wpm = reader.wpm();
        } else {
            reader.previousSentence();
        }
        break;
    case Mode::Menu: step(menuSelected, kMenuCount, +1); break;
    case Mode::Chapters: step(chapterSelected, reader.chapters().size(), +1); break;
    case Mode::Settings:
        if (editingSetting) changeSetting(+1);
        else step(settingSelected, kSetCount, +1);
        break;
    case Mode::Upload: leaveUpload(); break;
    case Mode::Message: mode = messageReturn; break;
    }
    redraw = true;
}

// Called once when the hold starts and then repeatedly while B stays down.
void onBHoldRepeat() {
    switch (mode) {
    case Mode::Library: step(librarySelected, libraryCount(), -1); break;
    case Mode::Reading:
        if (reader.playing()) {
            reader.adjustWpm(-1);
            wpm = reader.wpm();
        } else {
            reader.nextSentence();
        }
        break;
    case Mode::Menu: step(menuSelected, kMenuCount, -1); break;
    case Mode::Chapters: step(chapterSelected, reader.chapters().size(), -1); break;
    case Mode::Settings:
        if (editingSetting) changeSetting(-1);
        else step(settingSelected, kSetCount, -1);
        break;
    default: return;
    }
    redraw = true;
}

void flipScreen() {
    flipped = !flipped;
    library::setFlipped(flipped);
    applyOrientation();
    redraw = true;
}

// Deep sleep until A or B is pressed. Waking restarts the firmware, which
// reopens the book paused where you left off.
void sleepNow() {
    closeBook();
    upload::end();
    showMessage("Sleeping", "Press the front button to wake", mode);
    draw();
    delay(900);

    // Wait for the buttons to be let go, or they would wake it straight away.
    for (int i = 0; i < 300 && (M5.BtnA.isPressed() || M5.BtnB.isPressed()); ++i) {
        delay(10);
        M5.update();
    }
    M5.Display.setBrightness(0);
    M5.Display.sleep();
    M5.Display.waitDisplay();

    // Keep the backlight off while asleep.
    gpio_set_direction(kBacklightPin, GPIO_MODE_OUTPUT);
    gpio_set_level(kBacklightPin, 0);
    gpio_hold_en(kBacklightPin);
    gpio_deep_sleep_hold_en();

    for (gpio_num_t pin : {kButtonAPin, kButtonBPin}) {
        rtc_gpio_pullup_en(pin);
        rtc_gpio_pulldown_dis(pin);
    }
    esp_sleep_enable_ext1_wakeup_io((1ULL << kButtonAPin) | (1ULL << kButtonBPin), ESP_EXT1_WAKEUP_ANY_LOW);
    esp_deep_sleep_start();
}

void handleInput(uint32_t now) {
    auto& a = M5.BtnA;
    auto& b = M5.BtnB;

    if (M5.BtnPWR.wasClicked()) {
        sleepNow();
        return;
    }

    if (a.wasPressed() || b.wasPressed()) {
        lastInputMs = now;
        if (dimmed) {
            // The press that wakes the screen does nothing else.
            dimmed = false;
            applyBrightness();
            ignoreUntilRelease = true;
        }
    }
    if (ignoreUntilRelease) {
        if (!a.isPressed() && !b.isPressed()) ignoreUntilRelease = false;
        return;
    }

    if (a.isPressed() && b.isPressed()) {
        if (!combo) {
            combo = true;
            comboFlipped = false;
            comboStartMs = now;
        }
        if (!comboFlipped && now - comboStartMs >= kFlipHoldMs) {
            comboFlipped = true;
            flipScreen();
        }
        return;
    }
    if (combo) {
        if (!a.isPressed() && !b.isPressed()) combo = false;
        return;
    }

    if (a.wasClicked()) onAClick();
    if (a.wasHold()) onAHold();
    if (b.wasClicked()) onBClick();
    if (b.wasHold()) {
        onBHoldRepeat();
        nextRepeatMs = now + kRepeatMs * 2;
    } else if (b.isHolding() && now >= nextRepeatMs) {
        onBHoldRepeat();
        nextRepeatMs = now + kRepeatMs;
    }
}

void handleIdle(uint32_t now) {
    if (mode == Mode::Reading && reader.playing()) lastInputMs = now;
    const uint32_t idle = now - lastInputMs;
    if (!dimmed && idle >= kDimAfterMs) {
        dimmed = true;
        M5.Display.setBrightness(std::min(kDimBrightness, kBrightnessLevels[brightness]));
    }
    if (idle >= kSleepAfterMs && mode != Mode::Upload) sleepNow();
}

} // namespace

void setup() {
    // Undo what sleepNow() set up, so the pins work normally again.
    const bool wokeByButton = esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_EXT1;
    gpio_hold_dis(kBacklightPin);
    gpio_deep_sleep_hold_dis();
    rtc_gpio_deinit(kButtonAPin);
    rtc_gpio_deinit(kButtonBPin);

    auto cfg = M5.config();
    M5.begin(cfg);
    Serial.begin(115200);
    // The press that woke the stick shouldn't also count as a button press.
    ignoreUntilRelease = wokeByButton;

    const bool storageOk = library::begin();
    portrait = library::portrait();
    flipped = library::flipped();
    theme = std::min<uint8_t>(library::theme(), ui::kThemeCount - 1);
    brightness = std::clamp<uint8_t>(library::brightness(), 1, kMaxBrightness);
    textSize = std::min<uint8_t>(library::textSize(), ui::kTextSizeCount - 1);
    wpm = library::wpm();
    applyBrightness();
    ui::begin();
    ui::setTheme(theme);
    ui::setTextSize(textSize);
    applyOrientation();
    lastInputMs = millis();

    if (!storageOk) {
        showMessage("Storage error", "Couldn't mount the book storage", Mode::Message);
        return;
    }

    refreshBooks();
    // Pick up where you left off, paused on the last word you read.
    const String last = library::lastBook();
    for (size_t i = 0; i < books.size(); ++i) {
        if (books[i].name == last) {
            librarySelected = i;
            openBook(last);
            break;
        }
    }
}

void loop() {
    M5.update();
    const uint32_t now = millis();
    handleInput(now);

    switch (mode) {
    case Mode::Reading: {
        const bool wasPlaying = reader.playing();
        if (reader.tick(now)) redraw = true;
        if (wasPlaying && !reader.playing()) {  // reached the end
            saveProgress();
            redraw = true;
        }
        if (reader.playing() && now - lastSaveMs >= kSaveEveryMs) saveProgress();
        break;
    }
    case Mode::Upload: {
        upload::loop();
        if (upload::takeLibraryChanged()) refreshBooks();
        if (upload::takeDoneRequested()) {
            leaveUpload();
            redraw = true;
            break;
        }
        const bool joined = upload::phoneJoined();
        if (joined != uploadJoined || upload::status() != uploadStatus) {
            uploadJoined = joined;
            uploadStatus = upload::status();
            redraw = true;
        }
        break;
    }
    case Mode::Library:
        if (now - lastStatusDrawMs >= kStatusRefreshMs) redraw = true;  // battery
        break;
    default: break;
    }

    handleIdle(now);

    if (redraw) {
        redraw = false;
        draw();
    }
    delay(1);
}
