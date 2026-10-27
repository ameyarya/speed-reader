// "Add books" mode: the stick runs its own Wi-Fi network and a small upload page.
#pragma once

#include <Arduino.h>

namespace upload {

void begin();
void loop();
void end();

const String& ssid();
const String& password();
String url();
bool phoneJoined();

// Last thing that happened, e.g. "Added Dracula". Empty until the first upload.
const String& status();
// True once after each change to the stored books.
bool takeLibraryChanged();
// True once after "Done" is pressed on the upload page.
bool takeDoneRequested();
// File name of the most recently added book, or empty.
const String& lastAdded();

} // namespace upload
