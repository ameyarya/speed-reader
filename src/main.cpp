// Speed Reader for the M5StickS3: one word at a time, with the focus letter fixed in place.
//
// Library:  A open, B next, hold B previous
// Reading:  A play/pause, hold A back to library
//           playing: B faster, hold B slower
//           paused:  B back a sentence, hold B forward a sentence
// Anywhere: hold A+B to flip the screen
#include <M5Unified.h>

#include "library.h"
#include "rsvp.h"
#include "ui.h"
#include "upload.h"

namespace {

enum class Mode { Library, Reading, Upload, Message };

constexpr uint8_t kBrightness = 150;
constexpr uint8_t kDimBrightness = 16;
constexpr uint32_t kDimAfterMs = 60 * 1000;
constexpr uint32_t kPowerOffAfterMs = 10 * 60 * 1000;
constexpr uint32_t kSaveEveryMs = 20 * 1000;
constexpr uint32_t kRepeatMs = 280;
constexpr uint32_t kFlipHoldMs = 800;
constexpr uint32_t kStatusRefreshMs = 1000;

Mode mode = Mode::Library;
rsvp::Reader reader;
char* bookData = nullptr;
String bookName;

std::vector<Book> books;
size_t selected = 0;

uint8_t rotation = 1;
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

size_t itemCount() { return books.size() + 1; }  // books, then "Add books"

void refreshBooks() {
    books = library::list();
    if (selected >= itemCount()) selected = itemCount() - 1;
}

void saveProgress() {
    if (!bookData) return;
    library::savePosition(bookName, reader.position());
    library::setWpm(reader.wpm());
    lastSaveMs = millis();
}

void closeBook() {
    if (!bookData) return;
    reader.pause();
    saveProgress();
    free(bookData);
    bookData = nullptr;
}

void showMessage(const String& title, const String& body) {
    mode = Mode::Message;
    ui::drawMessage(title, body);
}

void openBook(const String& name) {
    closeBook();
    size_t size = 0;
    bookData = library::load(name, size);
    if (!bookData) {
        showMessage("Can't open book", "It may be too big for memory");
        return;
    }
    bookName = name;
    reader.setWpm(library::wpm());
    reader.open(bookData, size, library::savedPosition(name));
    library::setLastBook(name);
    mode = Mode::Reading;
    redraw = true;
}

void goToLibrary() {
    closeBook();
    refreshBooks();
    mode = Mode::Library;
    redraw = true;
}

void leaveUpload() {
    const String added = upload::lastAdded();
    upload::end();
    goToLibrary();
    // Land on the book that was just added, ready to open with A.
    for (size_t i = 0; i < books.size(); ++i)
        if (books[i].name == added) selected = i;
}

void openSelected() {
    if (selected < books.size()) {
        openBook(books[selected].name);
    } else {
        upload::begin();
        uploadJoined = false;
        uploadStatus = "";
        mode = Mode::Upload;
        redraw = true;
    }
}

void drawLibrary() {
    std::vector<ui::LibraryItem> items;
    for (const auto& b : books) {
        String title = b.name;
        if (title.endsWith(".txt")) title.remove(title.length() - 4);
        const size_t pos = library::savedPosition(b.name);
        const int percent = b.size ? static_cast<int>(static_cast<uint64_t>(pos) * 100 / b.size) : 0;
        items.push_back({title, percent});
    }
    items.push_back({"+ Add books (Wi-Fi)", -1});
    ui::drawLibrary(items, selected);
}

void draw() {
    switch (mode) {
    case Mode::Library: drawLibrary(); break;
    case Mode::Reading: ui::drawReader(reader, bookName); break;
    case Mode::Upload:
        ui::drawUpload(upload::ssid(), upload::password(), upload::url(), uploadJoined, uploadStatus);
        break;
    case Mode::Message: break;  // drawn once by showMessage
    }
    lastStatusDrawMs = millis();
}

// ---- Input ------------------------------------------------------------------

void onAClick() {
    switch (mode) {
    case Mode::Library: openSelected(); break;
    case Mode::Reading:
        if (reader.finished()) {
            reader.restart();
            reader.play(millis());
        } else {
            reader.toggle(millis());
            if (!reader.playing()) saveProgress();
        }
        break;
    case Mode::Upload: leaveUpload(); break;
    case Mode::Message: goToLibrary(); break;
    }
    redraw = true;
}

void onAHold() {
    if (mode == Mode::Reading) goToLibrary();
}

void onBClick() {
    switch (mode) {
    case Mode::Library: selected = (selected + 1) % itemCount(); break;
    case Mode::Reading:
        if (reader.playing()) reader.adjustWpm(+1);
        else reader.previousSentence();
        break;
    case Mode::Upload: leaveUpload(); break;
    case Mode::Message: goToLibrary(); break;
    }
    redraw = true;
}

// Called once when the hold starts and then repeatedly while B stays down.
void onBHoldRepeat() {
    switch (mode) {
    case Mode::Library: selected = (selected + itemCount() - 1) % itemCount(); break;
    case Mode::Reading:
        if (reader.playing()) reader.adjustWpm(-1);
        else reader.nextSentence();
        break;
    default: return;
    }
    redraw = true;
}

void flipScreen() {
    rotation = rotation == 1 ? 3 : 1;
    library::setRotation(rotation);
    ui::setRotation(rotation);
    if (mode == Mode::Message) goToLibrary();
    redraw = true;
}

void handleInput(uint32_t now) {
    auto& a = M5.BtnA;
    auto& b = M5.BtnB;

    if (a.wasPressed() || b.wasPressed()) {
        lastInputMs = now;
        if (dimmed) {
            // The press that wakes the screen does nothing else.
            dimmed = false;
            M5.Display.setBrightness(kBrightness);
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
    const bool busy = (mode == Mode::Reading && reader.playing());
    if (busy) lastInputMs = now;
    const uint32_t idle = now - lastInputMs;
    if (!dimmed && idle >= kDimAfterMs) {
        dimmed = true;
        M5.Display.setBrightness(kDimBrightness);
    }
    if (idle >= kPowerOffAfterMs && mode != Mode::Upload) {
        closeBook();
        M5.Display.setBrightness(0);
        M5.Power.powerOff();
    }
}

} // namespace

void setup() {
    auto cfg = M5.config();
    M5.begin(cfg);
    Serial.begin(115200);
    M5.Display.setBrightness(kBrightness);

    const bool storageOk = library::begin();
    rotation = library::rotation();
    ui::begin(rotation);
    lastInputMs = millis();

    if (!storageOk) {
        showMessage("Storage error", "Couldn't mount the book storage");
        return;
    }

    refreshBooks();
    // Pick up where you left off, paused on the last word you read.
    const String last = library::lastBook();
    for (size_t i = 0; i < books.size(); ++i) {
        if (books[i].name == last) {
            selected = i;
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
        if (now - lastStatusDrawMs >= 30 * kStatusRefreshMs) redraw = true;  // battery
        break;
    case Mode::Message: break;
    }

    handleIdle(now);

    if (redraw) {
        redraw = false;
        draw();
    }
    delay(1);
}
