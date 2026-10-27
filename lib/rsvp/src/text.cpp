#include "rsvp.h"

namespace rsvp {
namespace {

// Base letters for U+00C0..U+00FF; '\0' means drop (multiplication/division signs).
constexpr char kLatin1Base[65] =
    "AAAAAAACEEEEIIIIDNOOOOO\0OUUUUYTs"
    "aaaaaaaceeeeiiiidnooooo\0ouuuuyty";

// Number of bytes in the UTF-8 sequence starting with `lead`, or 1 if invalid.
size_t sequenceLength(unsigned char lead) {
    if (lead < 0x80) return 1;
    if ((lead & 0xE0) == 0xC0) return 2;
    if ((lead & 0xF0) == 0xE0) return 3;
    if ((lead & 0xF8) == 0xF0) return 4;
    return 1;
}

uint32_t decode(std::string_view s, size_t i, size_t len) {
    const auto b = [&](size_t k) { return static_cast<unsigned char>(s[i + k]); };
    switch (len) {
    case 2: return ((b(0) & 0x1Fu) << 6) | (b(1) & 0x3Fu);
    case 3: return ((b(0) & 0x0Fu) << 12) | ((b(1) & 0x3Fu) << 6) | (b(2) & 0x3Fu);
    case 4: return ((b(0) & 0x07u) << 18) | ((b(1) & 0x3Fu) << 12) | ((b(2) & 0x3Fu) << 6) | (b(3) & 0x3Fu);
    default: return b(0);
    }
}

} // namespace

void normalizeWord(std::string_view raw, std::string& out) {
    for (size_t i = 0; i < raw.size();) {
        const auto lead = static_cast<unsigned char>(raw[i]);
        size_t len = sequenceLength(lead);
        if (i + len > raw.size()) len = raw.size() - i;
        const uint32_t cp = decode(raw, i, len);
        i += len;

        if (cp >= 0x21 && cp < 0x7F) {
            out.push_back(static_cast<char>(cp));
        } else if (cp >= 0xC0 && cp <= 0xFF) {
            if (char base = kLatin1Base[cp - 0xC0]) out.push_back(base);
        } else {
            switch (cp) {
            case 0x2018: case 0x2019: case 0x201A: case 0x201B: case 0x2032: out.push_back('\''); break;
            case 0x201C: case 0x201D: case 0x201E: case 0x201F: case 0x00AB: case 0x00BB: case 0x2033:
                out.push_back('"'); break;
            case 0x2010: case 0x2011: case 0x2012: case 0x2013: case 0x2014: case 0x2015: case 0x2212:
                out.push_back('-'); break;
            case 0x2026: out.append("..."); break;
            case 0x0152: out.append("OE"); break;
            case 0x0153: out.append("oe"); break;
            default: break;  // no glyph for it
            }
        }
    }
}

std::string normalizeLine(std::string_view raw) {
    std::string out;
    size_t i = 0;
    while (i < raw.size()) {
        while (i < raw.size() && isSpace(raw[i])) ++i;
        size_t end = i;
        while (end < raw.size() && !isSpace(raw[end])) ++end;
        if (end == i) break;
        const size_t before = out.size();
        if (!out.empty()) out.push_back(' ');
        const size_t wordStart = out.size();
        normalizeWord(raw.substr(i, end - i), out);
        if (out.size() == wordStart) out.resize(before);  // nothing drawable in that word
        i = end;
    }
    return out;
}

} // namespace rsvp
