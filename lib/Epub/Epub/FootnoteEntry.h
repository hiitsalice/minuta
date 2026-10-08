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

// Builds the label shown in the Endnotes list. Links that go to a note read "To endnote N";
// back-links (U+21A9 / U+2191 arrows, which the UI font can't draw, or links flagged as
// back-links) read "To reference N". N is the link's own text (e.g. "3", "*") or, for
// back-arrows, the trailing digits of the link's anchor.
inline void normalizeFootnoteLabel(char* number, const char* href, const bool isBacklink = false) {
  char marker[FOOTNOTE_NUMBER_LEN];
  marker[0] = '\0';
  const unsigned char* p = reinterpret_cast<const unsigned char*>(number);
  const bool isBackArrow = p[0] == 0xE2 && p[1] == 0x86 && (p[2] == 0xA9 || p[2] == 0x91);
  if (isBackArrow) {
    const size_t end = strlen(href);
    size_t start = end;
    while (start > 0 && href[start - 1] >= '0' && href[start - 1] <= '9') start--;
    if (start < end && end - start <= 8) {
      memcpy(marker, href + start, end - start);
      marker[end - start] = '\0';
    }
  } else {
    strncpy(marker, number, sizeof(marker) - 1);
    marker[sizeof(marker) - 1] = '\0';
    size_t markerLen = strlen(marker);
    while (markerLen > 1 && (marker[markerLen - 1] == '.' || marker[markerLen - 1] == ':')) {
      marker[--markerLen] = '\0';
    }
  }
  strcpy(number, (isBackArrow || isBacklink) ? "To reference" : "To endnote");
  if (marker[0] != '\0') {
    strcat(number, " ");
    strncat(number, marker, FOOTNOTE_NUMBER_LEN - 1 - strlen(number));
  }
}
