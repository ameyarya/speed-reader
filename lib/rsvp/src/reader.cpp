#include "rsvp.h"

#include <algorithm>
#include <cctype>

namespace rsvp {
namespace {

// Extra time for the first words after pressing play, so the eye can settle.
constexpr uint8_t kRampPercent[] = {220, 170, 135, 110};
constexpr uint8_t kRampWords = sizeof(kRampPercent);
// Past this many words of lag (a long redraw, a busy loop) we stop catching up.
constexpr uint32_t kMaxCatchUpWords = 4;
// Sentence navigation gives up after this many words without an ending.
constexpr size_t kMaxSentenceScanWords = 600;
// Average words per minute lost to pacing pauses, used for the time-left estimate.
constexpr uint32_t kPacingOverheadPercent = 118;

} // namespace

void Reader::open(const char* data, size_t size, size_t position) {
    data_ = data;
    size_ = size;
    playing_ = false;
    finished_ = false;

    totalWords_ = 0;
    chapters_.clear();
    for (size_t i = nextWordStart(0); i < size_; i = nextWordStart(wordEnd(i))) ++totalWords_;
    for (size_t i = 0; i < size_; ++i) {
        if (data_[i] != kChapterMarker || (i > 0 && data_[i - 1] != '\n')) continue;
        size_t end = i + 1;
        while (end < size_ && data_[end] != '\n') ++end;
        std::string title = normalizeLine(std::string_view(data_ + i + 1, end - i - 1));
        if (title.empty()) title = "Chapter " + std::to_string(chapters_.size() + 1);
        chapters_.push_back({i, std::move(title)});
        i = end;
    }

    position = std::min(position, size_);
    while (position > 0 && !isSpace(data_[position - 1])) --position;
    if (position < size_ && data_[lineStart(position)] == kChapterMarker) position = lineStart(position);
    if (!loadWordFrom(position)) {
        // Saved spot was at the very end; show the start again.
        if (!loadWordFrom(0)) finished_ = true;
    }
}

void Reader::restart() {
    playing_ = false;
    finished_ = !loadWordFrom(0);
}

size_t Reader::lineStart(size_t pos) const {
    while (pos > 0 && data_[pos - 1] != '\n') --pos;
    return pos;
}

size_t Reader::nextWordStart(size_t from) const {
    for (;;) {
        while (from < size_ && isSpace(data_[from])) ++from;
        if (from >= size_ || data_[from] != kChapterMarker) return from;
        while (from < size_ && data_[from] != '\n') ++from;  // skip the marker line
    }
}

size_t Reader::wordEnd(size_t wordStart) const {
    while (wordStart < size_ && !isSpace(data_[wordStart])) ++wordStart;
    return wordStart;
}

size_t Reader::previousWordStart(size_t wordStart) const {
    size_t p = wordStart;
    for (;;) {
        while (p > 0 && isSpace(data_[p - 1])) --p;
        if (p == 0) return nextWordStart(0) <= wordStart ? nextWordStart(0) : wordStart;
        size_t q = p;
        while (q > 0 && !isSpace(data_[q - 1])) --q;
        const size_t line = lineStart(q);
        if (data_[line] != kChapterMarker) return q;
        p = line;  // that word belongs to a chapter title; keep going back
    }
}

bool Reader::startsLowercase(size_t wordStart) const {
    for (size_t i = wordStart; i < size_ && !isSpace(data_[i]); ++i) {
        const auto c = static_cast<unsigned char>(data_[i]);
        if (std::isalpha(c)) return std::islower(c) != 0;
        if (std::isdigit(c)) return false;
    }
    return false;
}

bool Reader::wordEndsSentenceAt(size_t wordStart) const {
    std::string w;
    normalizeWord(std::string_view(data_ + wordStart, wordEnd(wordStart) - wordStart), w);
    return endsSentence(w, startsLowercase(nextWordStart(wordEnd(wordStart))));
}

bool Reader::paragraphEndsAfter(size_t end) const {
    int newlines = 0;
    for (size_t i = end; i < size_ && isSpace(data_[i]); ++i)
        if (data_[i] == '\n' && ++newlines >= 2) return true;
    return false;
}

size_t Reader::sentenceStartFor(size_t wordStart) const {
    size_t p = wordStart;
    for (size_t n = 0; p > 0 && n < kMaxSentenceScanWords; ++n) {
        const size_t prev = previousWordStart(p);
        if (prev == p || wordEndsSentenceAt(prev)) break;
        p = prev;
    }
    return p;
}

bool Reader::loadWordFrom(size_t start) {
    for (size_t p = nextWordStart(start); p < size_; p = nextWordStart(wordEnd(p))) {
        word_.clear();
        normalizeWord(std::string_view(data_ + p, wordEnd(p) - p), word_);
        if (word_.empty()) continue;  // nothing we can draw, e.g. an emoji
        wordStart_ = p;
        wordEnd_ = wordEnd(p);
        chunk_ = 0;
        buildDisplay();
        return true;
    }
    return false;
}

void Reader::buildDisplay() {
    const size_t remaining = word_.size() - chunk_;
    if (remaining <= maxChunk_ + 2) {
        chunkLen_ = remaining;
        display_ = word_.substr(chunk_);
    } else {
        // Prefer splitting right after an existing hyphen.
        size_t cut = maxChunk_ - 2;
        const size_t hyphen = word_.rfind('-', chunk_ + maxChunk_ - 1);
        if (hyphen != std::string::npos && hyphen > chunk_ + 3) cut = hyphen + 1 - chunk_;
        chunkLen_ = cut;
        display_ = word_.substr(chunk_, cut);
        if (display_.back() != '-') display_.push_back('-');
    }
    pivot_ = pivotIndex(display_);
}

bool Reader::advance() {
    if (chunk_ + chunkLen_ < word_.size()) {
        chunk_ += chunkLen_;
        buildDisplay();
        return true;
    }
    const size_t keepStart = wordStart_;
    if (loadWordFrom(wordEnd_)) return true;
    wordStart_ = keepStart;
    return false;
}

uint32_t Reader::currentDurationMs() const {
    const bool lastChunk = chunk_ + chunkLen_ >= word_.size();
    const size_t next = nextWordStart(wordEnd_);
    uint32_t ms = wordDurationMs(display_, lastChunk && startsLowercase(next),
                                 lastChunk && paragraphEndsAfter(wordEnd_), wpm_, pacing_);
    if (rampLeft_ > 0) ms = ms * kRampPercent[kRampWords - rampLeft_] / 100U;
    return std::max<uint32_t>(ms, 1);
}

void Reader::play(uint32_t nowMs) {
    if (finished_ || size_ == 0) return;
    playing_ = true;
    lastAdvanceMs_ = nowMs;
    rampLeft_ = kRampWords;
}

bool Reader::tick(uint32_t nowMs) {
    if (!playing_) return false;
    const uint32_t duration = currentDurationMs();
    const uint32_t elapsed = nowMs - lastAdvanceMs_;
    if (elapsed < duration) return false;
    lastAdvanceMs_ = elapsed > duration * kMaxCatchUpWords ? nowMs : lastAdvanceMs_ + duration;
    if (rampLeft_ > 0) --rampLeft_;
    if (!advance()) {
        playing_ = false;
        finished_ = true;
        return false;
    }
    return true;
}

void Reader::previousSentence() {
    finished_ = false;
    size_t start = sentenceStartFor(wordStart_);
    // Already at a sentence start (and not mid-way through a split word): go one further back.
    if (start == wordStart_ && chunk_ == 0 && start > 0) start = sentenceStartFor(previousWordStart(start));
    loadWordFrom(start);
}

void Reader::nextSentence() {
    size_t p = wordStart_;
    for (size_t n = 0; n < kMaxSentenceScanWords; ++n) {
        const bool ends = wordEndsSentenceAt(p);
        const size_t next = nextWordStart(wordEnd(p));
        if (next >= size_) return;  // no further sentence; stay put
        p = next;
        if (ends) break;
    }
    loadWordFrom(p);
}

int Reader::currentChapter() const {
    int current = -1;
    for (size_t i = 0; i < chapters_.size() && chapters_[i].position <= wordStart_; ++i) current = static_cast<int>(i);
    return current;
}

void Reader::goToChapter(size_t index) {
    if (index >= chapters_.size()) return;
    if (loadWordFrom(chapters_[index].position)) finished_ = false;
}

void Reader::setMaxChunk(size_t chars) {
    maxChunk_ = std::max<size_t>(chars, 6);
    if (!word_.empty()) buildDisplay();
}

void Reader::setWpm(int wpm) {
    wpm_ = static_cast<uint16_t>(std::clamp<int>(wpm, kMinWpm, kMaxWpm));
}

float Reader::progress() const {
    if (size_ == 0) return 0.f;
    if (finished_) return 1.f;
    return static_cast<float>(wordStart_) / static_cast<float>(size_);
}

uint32_t Reader::secondsLeft() const {
    const float left = (1.f - progress()) * static_cast<float>(totalWords_);
    return static_cast<uint32_t>(left * 60.f / wpm_ * kPacingOverheadPercent / 100.f);
}

} // namespace rsvp
