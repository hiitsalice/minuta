#pragma once

#include <cstring>

#define FOOTNOTE_NUMBER_LEN 32
#define FOOTNOTE_HREF_LEN 256
// ponytail: bumped from 96 to 256; calibre-generated EPUBs with long filenames
// and URL-encoded characters routinely exceed 96 chars (e.g.
// "Author-Title_split_NNN.html#_ftnN" encoded is ~150 chars).

struct FootnoteEntry {
  char number[FOOTNOTE_NUMBER_LEN];
  char href[FOOTNOTE_HREF_LEN];

  FootnoteEntry() {
    number[0] = '\0';
    href[0] = '\0';
  }
};

// True if a link's text looks like a note marker ("1", "12", "a", "*", a superscript
// digit) rather than a cross-reference such as "see Chapter 3".
inline bool isFootnoteMarkerText(const char* text) {
  const size_t len = strlen(text);
  if (len == 0 || len > 6) return false;
  int letterRun = 0;
  for (size_t i = 0; i < len; i++) {
    const unsigned char c = static_cast<unsigned char>(text[i]);
    if (c == ' ' || c == '\t' || c == '\n' || c == '\r') return false;
    const bool isLetter = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
    letterRun = isLetter ? letterRun + 1 : 0;
    if (letterRun >= 3) return false;
  }
  return true;
}
