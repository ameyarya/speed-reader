# Speed Reader

A pocket speed reader for the **M5StickS3**. Books play one word at a time (RSVP), with one letter in red held at the same spot on screen, so your eyes never move. Add EPUB or text files from your phone over the stick's own Wi-Fi.

## Features

- **Fixed focus letter.** Each word is placed so its recognition point (the red letter) sits on the tick marks. Long words shrink to fit; very long ones are shown in hyphenated pieces.
- **Natural pacing.** Words get extra time for length, complexity (syllables, numbers, all-caps, URLs) and punctuation, with longer pauses at sentence and paragraph ends. Abbreviations like "Mr." and "e.g." don't count as sentence ends. Reading eases in over the first few words after you press play.
- **100 to 1000 words per minute**, in steps of 25.
- **Resume everywhere.** Your place in each book and your speed are saved; the stick reopens the last book on boot.
- **Add books over Wi-Fi.** The stick shows a QR code to join its network, then a second one for the upload page. EPUBs are converted to text in your browser, so it works without internet.
- **Progress, time left and battery** on screen; the screen dims after a minute idle and the stick powers off after ten.

## Controls

The front button is **A**, the side button is **B**.

| Where | Press | Hold |
|---|---|---|
| Library | A: open · B: next | B: previous |
| Reading (playing) | A: pause · B: faster | A: library · B: slower |
| Reading (paused) | A: play · B: back a sentence | A: library · B: forward a sentence |
| Add books | A: done | |
| Anywhere | | A + B: flip the screen |

## Build and flash

You need [PlatformIO](https://platformio.org/install/cli). Plug the stick in with USB-C, then:

```bash
pio run -t upload
```

If the upload can't find the stick, put it in download mode (see M5Stack's StickS3 docs for the button combination) and try again.

Run the reading engine's tests on your computer (no stick needed):

```bash
./tools/test.sh
```

## Adding books

1. In the library, choose **+ Add books (Wi-Fi)**.
2. Scan the QR code with your phone to join the `SpeedReader-XXXX` network (or type the password shown). Your phone may warn that there's no internet; that's expected.
3. Scan the second QR code, or open `http://192.168.4.1`.
4. Pick EPUB or TXT files. You can remove books from the same page.
5. Press A on the stick when you're done.

Books are stored as plain text in the stick's flash (about 4.9 MB, roughly 8 to 10 novels). Only Latin-alphabet text is supported: accents are dropped and other scripts aren't shown. EPUBs with DRM can't be read.

## How it's organized

```
lib/rsvp/        reading engine: text navigation, focus letter, pacing (plain C++, host-tested)
src/main.cpp     modes and button handling
src/ui.cpp       screens (drawn to an off-screen canvas, then pushed at once)
src/library.cpp  books on LittleFS, positions and settings in NVS
src/upload.cpp   Wi-Fi access point and upload API
src/upload_page.h  the upload page, including the in-browser EPUB converter
test/            host tests for lib/rsvp
```

## Credits

- Pacing heuristics are adapted from [RSVP Nano](https://github.com/ionutdecebal/rsvpnano) (MIT).
- The controls, sentence navigation and Wi-Fi upload were inspired by [RSVP-Reader](https://github.com/willherr72/RSVP-Reader) and Simple RSVP Reader. No code was copied from either.
- Built on [M5Unified](https://github.com/m5stack/M5Unified).

## License

MIT. See [LICENSE](LICENSE).
