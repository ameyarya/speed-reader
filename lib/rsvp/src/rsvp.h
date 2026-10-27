// Speed Reader core: tokenizing, pacing and playback for RSVP reading.
// Pure C++17 with no Arduino dependencies, so it builds and tests on a host.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace rsvp {

// ---- Text -----------------------------------------------------------------

inline bool isSpace(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; }

// A line starting with this character marks a chapter: the rest of the line is
// its title. Marker lines are never shown as words. The upload page writes them.
constexpr char kChapterMarker = '\x1e';

// Appends a display-safe ASCII version of a raw UTF-8 word to `out`: curly quotes,
// dashes and ellipses map to ASCII, accented Latin letters lose their accents and
// anything else is dropped (the on-device fonts only cover printable ASCII).
void normalizeWord(std::string_view raw, std::string& out);
// Same, but keeps single spaces between words (for titles).
std::string normalizeLine(std::string_view raw);

// ---- Pacing ---------------------------------------------------------------

struct Pacing {
    uint16_t longWordDelayMs = 200;
    uint16_t complexWordDelayMs = 200;
    uint16_t punctuationDelayMs = 200;
};

// Optimal recognition point: the letter the eye should fix on.
int pivotIndex(std::string_view word);

bool endsSentence(std::string_view word, bool nextWordStartsLowercase);

// How long `word` stays on screen at `wpm`, including length, complexity and
// punctuation bonuses. `paragraphEnd` adds a longer pause after the last word
// of a paragraph.
uint32_t wordDurationMs(std::string_view word, bool nextWordStartsLowercase, bool paragraphEnd,
                        uint16_t wpm, const Pacing& pacing);

// ---- Reader ---------------------------------------------------------------

struct Chapter {
    size_t position;  // byte offset of the marker line
    std::string title;
};

class Reader {
public:
    static constexpr uint16_t kMinWpm = 100;
    static constexpr uint16_t kMaxWpm = 1000;
    // Words longer than this are shown in hyphenated pieces so they fit on screen.
    static constexpr size_t kMaxChunk = 14;

    // `data` must outlive the reader. `position` is a byte offset, usually one
    // returned earlier by position(); it is snapped to the start of its word.
    void open(const char* data, size_t size, size_t position);

    void play(uint32_t nowMs);
    void pause() { playing_ = false; }
    void toggle(uint32_t nowMs) { playing_ ? pause() : play(nowMs); }
    bool playing() const { return playing_; }
    bool finished() const { return finished_; }

    // Advances the word when its time is up. Returns true if the display changed.
    bool tick(uint32_t nowMs);

    void previousSentence();
    void nextSentence();
    void restart();

    const std::vector<Chapter>& chapters() const { return chapters_; }
    // Chapter containing the current word, or -1 before the first chapter.
    int currentChapter() const;
    void goToChapter(size_t index);

    uint16_t wpm() const { return wpm_; }
    void setWpm(int wpm);
    static constexpr uint16_t kWpmStep = 25;
    void adjustWpm(int direction) { setWpm(wpm_ + direction * kWpmStep); }

    Pacing& pacing() { return pacing_; }

    const std::string& display() const { return display_; }
    int pivot() const { return pivot_; }
    size_t position() const { return wordStart_; }
    float progress() const;
    size_t totalWords() const { return totalWords_; }
    uint32_t secondsLeft() const;

private:
    size_t lineStart(size_t pos) const;
    size_t nextWordStart(size_t from) const;
    size_t previousWordStart(size_t wordStart) const;
    size_t wordEnd(size_t wordStart) const;
    bool wordEndsSentenceAt(size_t wordStart) const;
    bool startsLowercase(size_t wordStart) const;
    bool paragraphEndsAfter(size_t wordEnd) const;
    size_t sentenceStartFor(size_t wordStart) const;

    // Loads the first displayable word at or after `start`; false at end of text.
    bool loadWordFrom(size_t start);
    bool advance();
    void buildDisplay();
    uint32_t currentDurationMs() const;

    const char* data_ = nullptr;
    size_t size_ = 0;
    size_t totalWords_ = 0;
    std::vector<Chapter> chapters_;

    size_t wordStart_ = 0;
    size_t wordEnd_ = 0;
    std::string word_;      // normalized current word
    size_t chunk_ = 0;      // offset of the shown piece inside word_
    size_t chunkLen_ = 0;
    std::string display_;
    int pivot_ = 0;

    uint16_t wpm_ = 300;
    Pacing pacing_;
    bool playing_ = false;
    bool finished_ = false;
    uint32_t lastAdvanceMs_ = 0;
    uint8_t rampLeft_ = 0;
};

} // namespace rsvp
