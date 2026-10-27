// Host tests for the rsvp core. Run with tools/test.sh.
#include "rsvp.h"

#include <cstdio>
#include <string>

static int failures = 0;
#define CHECK(cond)                                                       \
    do {                                                                  \
        if (!(cond)) {                                                    \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);  \
            ++failures;                                                   \
        }                                                                 \
    } while (0)

using namespace rsvp;

static std::string norm(const char* s) {
    std::string out;
    normalizeWord(s, out);
    return out;
}

static void testNormalize() {
    CHECK(norm("\xE2\x80\x9CHello,\xE2\x80\x9D") == "\"Hello,\"");
    CHECK(norm("don\xE2\x80\x99t") == "don't");
    CHECK(norm("caf\xC3\xA9") == "cafe");
    CHECK(norm("well\xE2\x80\x94") == "well-");
    CHECK(norm("wait\xE2\x80\xA6") == "wait...");
    CHECK(norm("\xF0\x9F\x98\x80").empty());  // emoji
}

static void testPivot() {
    CHECK(pivotIndex("a") == 0);
    CHECK(pivotIndex("word") == 1);
    CHECK(pivotIndex("reading") == 2);
    CHECK(pivotIndex("\"quoted\"") == 3);  // skips the opening quote
    CHECK(pivotIndex("extraordinary") == 3);
    CHECK(pivotIndex("...") == 1);
}

static void testSentences() {
    CHECK(endsSentence("end.", false));
    CHECK(endsSentence("Really?\"", false));
    CHECK(!endsSentence("Mr.", false));
    CHECK(!endsSentence("e.g.", false));
    CHECK(!endsSentence("J.", false));
    CHECK(!endsSentence("word,", false));
}

static void testPacing() {
    Pacing p;
    const uint32_t plain = wordDurationMs("cat", false, false, 300, p);
    CHECK(plain == 200);
    CHECK(wordDurationMs("cat,", false, false, 300, p) > plain);
    CHECK(wordDurationMs("cat.", false, false, 300, p) > wordDurationMs("cat,", false, false, 300, p));
    CHECK(wordDurationMs("cat", false, true, 300, p) > wordDurationMs("cat.", false, false, 300, p));
    CHECK(wordDurationMs("incomprehensibility", false, false, 300, p) > plain + 200);
    CHECK(wordDurationMs("Mr.", false, false, 300, p) < wordDurationMs("cat.", false, false, 300, p));
}

static void testReaderPlayback() {
    const std::string text = "One two three. Four five six! Seven.";
    Reader r;
    r.setWpm(600);  // 100 ms per plain word
    r.open(text.data(), text.size(), 0);
    CHECK(r.totalWords() == 7);
    CHECK(r.display() == "One");
    CHECK(!r.tick(1000));  // paused

    r.play(0);
    uint32_t t = 0;
    int changes = 0;
    while (!r.finished() && t < 60000) {
        t += 10;
        if (r.tick(t)) ++changes;
    }
    CHECK(r.finished());
    CHECK(changes == 6);
    CHECK(r.display() == "Seven.");
    CHECK(r.progress() == 1.f);
}

static void testReaderNavigation() {
    const std::string text = "One two three. Four five six! Seven eight.";
    Reader r;
    r.open(text.data(), text.size(), text.find("five"));
    CHECK(r.display() == "five");
    r.previousSentence();
    CHECK(r.display() == "Four");
    r.previousSentence();
    CHECK(r.display() == "One");
    r.previousSentence();
    CHECK(r.display() == "One");
    r.nextSentence();
    CHECK(r.display() == "Four");
    r.nextSentence();
    CHECK(r.display() == "Seven");
    r.nextSentence();
    CHECK(r.display() == "Seven");  // no sentence after the last one

    // Resume positions snap back to the start of the word.
    r.open(text.data(), text.size(), text.find("five") + 2);
    CHECK(r.display() == "five");
    r.open(text.data(), text.size(), text.size());
    CHECK(r.display() == "eight.");
}

static void testLongWordChunks() {
    const std::string text = "pneumonoultramicroscopicsilicovolcanoconiosis ok";
    Reader r;
    r.open(text.data(), text.size(), 0);
    std::string joined;
    int pieces = 0;
    r.play(0);
    uint32_t t = 0;
    for (;;) {
        CHECK(r.display().size() <= Reader::kMaxChunk + 2);
        if (r.display() == "ok") break;
        std::string piece = r.display();
        if (piece.back() == '-') piece.pop_back();
        joined += piece;
        ++pieces;
        while (!r.tick(t += 10)) {}
    }
    CHECK(pieces >= 3);
    CHECK(joined == "pneumonoultramicroscopicsilicovolcanoconiosis");
}

static void testChapters() {
    const std::string M(1, kChapterMarker);
    const std::string text = M + "Chapter One\nIt begins. Here.\n\n" + M + "The Caf\xC3\xA9\nNext part.\n\n" + M + "\nLast.";
    Reader r;
    r.open(text.data(), text.size(), 0);
    CHECK(r.chapters().size() == 3);
    CHECK(r.chapters()[0].title == "Chapter One");
    CHECK(r.chapters()[1].title == "The Cafe");
    CHECK(r.chapters()[2].title == "Chapter 3");  // untitled marker gets a number
    CHECK(r.totalWords() == 6);                    // titles are not read
    CHECK(r.display() == "It");
    CHECK(r.currentChapter() == 0);

    // Playing straight through never shows a title word.
    r.setWpm(600);
    r.play(0);
    uint32_t t = 0;
    std::string seen;
    while (!r.finished() && t < 60000) {
        if (r.tick(t += 10)) seen += r.display() + " ";
    }
    CHECK(seen == "begins. Here. Next part. Last. ");

    r.goToChapter(1);
    CHECK(r.display() == "Next");
    CHECK(r.currentChapter() == 1);
    CHECK(!r.finished());
    r.previousSentence();  // already at a sentence start: back across the title into chapter one
    CHECK(r.display() == "Here.");
    CHECK(r.currentChapter() == 0);

    // A saved position inside a title line resumes at that chapter's first word.
    r.open(text.data(), text.size(), text.find("Caf"));
    CHECK(r.display() == "Next");

    // Books without markers have no chapters.
    const std::string plain = "No chapters here.";
    r.open(plain.data(), plain.size(), 0);
    CHECK(r.chapters().empty());
    CHECK(r.currentChapter() == -1);
}

static void testWpm() {
    Reader r;
    r.setWpm(5000);
    CHECK(r.wpm() == Reader::kMaxWpm);
    r.setWpm(1);
    CHECK(r.wpm() == Reader::kMinWpm);
    r.setWpm(300);
    r.adjustWpm(+1);
    CHECK(r.wpm() == 325);
}

int main() {
    testNormalize();
    testPivot();
    testSentences();
    testPacing();
    testReaderPlayback();
    testReaderNavigation();
    testLongWordChunks();
    testChapters();
    testWpm();
    if (failures) {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("all tests passed\n");
    return 0;
}
