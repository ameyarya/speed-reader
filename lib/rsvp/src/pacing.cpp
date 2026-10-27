// Pacing heuristics adapted from RSVP Nano's ReadingLoop
// (https://github.com/ionutdecebal/rsvpnano), MIT License,
// Copyright (c) 2026 RSVP Nano contributors. Simplified here to ASCII text.
#include "rsvp.h"

#include <algorithm>
#include <cctype>

namespace rsvp {
namespace {

constexpr int kLongWordAfterChars = 6;
constexpr int kLongWordPercentPerChar = 6;
constexpr int kVeryLongWordAfterChars = 10;
constexpr int kVeryLongWordPercentPerChar = 9;
constexpr int kUltraLongWordAfterChars = 14;
constexpr int kUltraLongWordPercentPerChar = 12;
constexpr int kLongWordMaxPercent = 170;
constexpr int kCompoundJoinerPercent = 14;
constexpr int kLongCompoundWordPercent = 18;
constexpr int kTechnicalConnectorPercent = 8;
constexpr int kSyllableBonusAfterCount = 2;
constexpr int kSyllableBonusPercentPerGroup = 10;
constexpr int kSyllableBonusMaxPercent = 50;
constexpr int kAllCapsComplexityPercent = 14;
constexpr int kMixedTokenComplexityPercent = 22;
constexpr int kNumericTokenComplexityPercent = 10;
constexpr int kDenseConnectorComplexityPercent = 12;
constexpr int kComplexWordMaxPercent = 85;
constexpr int kCommaPausePercent = 45;
constexpr int kDashPausePercent = 60;
constexpr int kClausePausePercent = 80;
constexpr int kEllipsisPausePercent = 110;
constexpr int kSentencePausePercent = 135;
constexpr int kStrongSentencePausePercent = 150;
constexpr int kParagraphPausePercent = 180;

bool isWordChar(char c) { return std::isalnum(static_cast<unsigned char>(c)) != 0; }
bool isVowel(char c) {
    switch (std::tolower(static_cast<unsigned char>(c))) {
    case 'a': case 'e': case 'i': case 'o': case 'u': case 'y': return true;
    default: return false;
    }
}
bool isClosing(char c) { return c == '"' || c == '\'' || c == ')' || c == ']' || c == '}' || c == '*' || c == '_'; }
bool isTechnical(char c) { return c == '/' || c == '_' || c == '@' || c == '.' || c == ':' || c == '\\'; }

int countIf(std::string_view w, bool (*pred)(char)) {
    return static_cast<int>(std::count_if(w.begin(), w.end(), pred));
}

// Strips trailing quotes and brackets so "end." and end." pause the same.
std::string_view withoutClosers(std::string_view w) {
    while (!w.empty() && isClosing(w.back())) w.remove_suffix(1);
    return w;
}

// Connectors between word characters, like the hyphen in "well-known".
int innerConnectors(std::string_view w, bool (*pred)(char)) {
    int n = 0;
    for (size_t i = 1; i + 1 < w.size(); ++i)
        if (pred(w[i]) && isWordChar(w[i - 1]) && isWordChar(w[i + 1])) ++n;
    return n;
}

bool isJoiner(char c) { return c == '-'; }

int syllableGroups(std::string_view w) {
    int groups = 0;
    bool inVowels = false;
    for (char c : w) {
        const bool v = isVowel(c);
        if (v && !inVowels) ++groups;
        inVowels = v;
    }
    return groups;
}

bool equalsIgnoreCase(std::string_view a, std::string_view b) {
    return a.size() == b.size() &&
           std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
               return std::tolower(static_cast<unsigned char>(x)) == std::tolower(static_cast<unsigned char>(y));
           });
}

bool isDottedInitialism(std::string_view w) {
    // U.S. / e.g. style: letter, dot, letter, dot...
    if (w.size() < 4 || w.back() != '.') return false;
    for (size_t i = 0; i < w.size(); ++i) {
        const bool wantLetter = i % 2 == 0;
        if (wantLetter ? !std::isalpha(static_cast<unsigned char>(w[i])) : w[i] != '.') return false;
    }
    return true;
}

bool looksLikeAbbreviation(std::string_view w, bool nextWordStartsLowercase) {
    static constexpr std::string_view kKnown[] = {
        "mr.", "mrs.", "ms.", "dr.", "prof.", "sr.", "jr.", "st.", "vs.", "etc.", "e.g.",
        "i.e.", "cf.", "no.", "fig.", "eq.", "inc.", "ltd.", "co.", "dept.", "mt.", "ft.",
    };
    while (!w.empty() && (w.front() == '(' || w.front() == '"' || w.front() == '\'')) w.remove_prefix(1);
    for (auto k : kKnown)
        if (equalsIgnoreCase(w, k)) return true;
    if (w.empty() || w.back() != '.') return false;
    if (isDottedInitialism(w)) return true;
    const int letters = countIf(w, isWordChar);
    if (letters <= 1) return true;  // initials like "J."
    return nextWordStartsLowercase && letters <= 4;
}

int lengthBonusPercent(std::string_view w) {
    const int n = countIf(w, isWordChar);
    if (n == 0) return 0;
    int bonus = 0;
    if (n > kLongWordAfterChars) bonus += (n - kLongWordAfterChars) * kLongWordPercentPerChar;
    if (n > kVeryLongWordAfterChars) bonus += (n - kVeryLongWordAfterChars) * kVeryLongWordPercentPerChar;
    if (n > kUltraLongWordAfterChars) bonus += (n - kUltraLongWordAfterChars) * kUltraLongWordPercentPerChar;
    const int joiners = innerConnectors(w, isJoiner);
    if (joiners > 0) {
        bonus += joiners * kCompoundJoinerPercent;
        if (n >= kVeryLongWordAfterChars) bonus += kLongCompoundWordPercent;
    }
    const int tech = innerConnectors(w, isTechnical);
    if (tech > 0) bonus += tech * kTechnicalConnectorPercent;
    return std::min(kLongWordMaxPercent, bonus);
}

int complexityBonusPercent(std::string_view w) {
    int bonus = 0;
    const int groups = syllableGroups(w);
    if (groups > kSyllableBonusAfterCount)
        bonus += std::min(kSyllableBonusMaxPercent, (groups - kSyllableBonusAfterCount) * kSyllableBonusPercentPerGroup);

    int letters = 0, digits = 0, upper = 0;
    for (char c : w) {
        const auto u = static_cast<unsigned char>(c);
        if (std::isalpha(u)) ++letters;
        if (std::isdigit(u)) ++digits;
        if (std::isupper(u)) ++upper;
    }
    if (letters > 0 && digits > 0) bonus += kMixedTokenComplexityPercent;
    else if (digits >= 3) bonus += kNumericTokenComplexityPercent;
    if (upper >= 2 && upper == letters) bonus += kAllCapsComplexityPercent;

    const int tech = innerConnectors(w, isTechnical);
    if (tech >= 2) bonus += (tech - 1) * kDenseConnectorComplexityPercent;
    return std::min(kComplexWordMaxPercent, bonus);
}

int punctuationPausePercent(std::string_view word, bool nextWordStartsLowercase) {
    const std::string_view w = withoutClosers(word);
    if (w.empty()) return 0;
    if (w.size() >= 3 && w.substr(w.size() - 3) == "...") return kEllipsisPausePercent;
    switch (w.back()) {
    case ',': return kCommaPausePercent;
    case '-': return kDashPausePercent;
    case ';':
    case ':': return kClausePausePercent;
    case '.': return looksLikeAbbreviation(w, nextWordStartsLowercase) ? 0 : kSentencePausePercent;
    case '!':
    case '?': return kStrongSentencePausePercent;
    default: return 0;
    }
}

uint32_t scaled(int percent, uint16_t delayMs) { return static_cast<uint32_t>(percent) * delayMs / 100U; }

} // namespace

int pivotIndex(std::string_view w) {
    size_t first = 0;
    while (first < w.size() && !isWordChar(w[first])) ++first;
    if (first == w.size()) return static_cast<int>(w.size() / 2);
    size_t last = w.size() - 1;
    while (last > first && !isWordChar(w[last])) --last;
    const size_t len = last - first + 1;
    size_t offset = 0;
    if (len <= 1) offset = 0;
    else if (len <= 5) offset = 1;
    else if (len <= 9) offset = 2;
    else if (len <= 13) offset = 3;
    else offset = 4;
    return static_cast<int>(first + offset);
}

bool endsSentence(std::string_view word, bool nextWordStartsLowercase) {
    const std::string_view w = withoutClosers(word);
    if (w.empty()) return false;
    if (w.back() == '!' || w.back() == '?') return true;
    if (w.back() != '.') return false;
    if (w.size() >= 3 && w.substr(w.size() - 3) == "...") return !nextWordStartsLowercase;
    return !looksLikeAbbreviation(w, nextWordStartsLowercase);
}

uint32_t wordDurationMs(std::string_view word, bool nextWordStartsLowercase, bool paragraphEnd,
                        uint16_t wpm, const Pacing& pacing) {
    if (wpm == 0 || word.empty()) return 0;
    uint32_t ms = 60000U / wpm;
    ms += scaled(lengthBonusPercent(word), pacing.longWordDelayMs);
    ms += scaled(complexityBonusPercent(word), pacing.complexWordDelayMs);
    int pause = punctuationPausePercent(word, nextWordStartsLowercase);
    if (paragraphEnd) pause = std::max(pause, kParagraphPausePercent);
    ms += scaled(pause, pacing.punctuationDelayMs);
    return ms;
}

} // namespace rsvp
