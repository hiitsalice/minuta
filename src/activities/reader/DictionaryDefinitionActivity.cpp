#include "DictionaryDefinitionActivity.h"

#include <Epub/Page.h>
#include <GfxRenderer.h>
#include <I18n.h>
#include <Logging.h>

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <memory>
#include <string>

#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "components/UiAppHelpers.h"
#include "fontIds.h"
#include "util/DictHtmlPages.h"
#include "util/HtmlToPlainText.h"

namespace fui = freeink::ui;

namespace {

// The first line of each definition starts this far in, with a thin line in front (like Hidden Shortcuts).
constexpr int kIndent = 36;
// Cambridge sub-labels (Verbs, Adjectives, ...) sit one step in between: heading, then label, then content.
constexpr int kSubIndent = 18;
// "Thesaurus+" gets a short line in front of it: 6px of line, then a 6px gap before the word.
constexpr int kThesaurusLead = 12;
// Text is wrapped this much narrower than the row's text area, so no line is ever cut off.
// Raise it if any row still ends in "...".
constexpr int kWrapSafety = 4;
// The last line of each paragraph is left-aligned. Set to true to stretch it as well.
constexpr bool kJustifyLastLine = false;
// Text in scripts the UI font cannot draw (Cyrillic, Greek, Arabic, ...) is removed: it would only log errors.
constexpr bool kStripUnsupportedGlyphs = true;

// The HTML parser lays each paragraph out on one very wide line; the lines are wrapped again here.
constexpr uint16_t kLayoutWidth = 30000;
constexpr uint16_t kLayoutHeight = 4000;

// The laid-out Pages keep the whole definition resident while they are read back, so very large
// definitions take the plain-text path, which holds no page copies.
constexpr size_t kMaxStyledHtmlBytes = 16 * 1024;

bool isDictionaryHeading(const char* text) {
  static constexpr const char* HEADINGS[] = {
      "Noun", "Verb", "Adjective", "Adverb", "Pronoun",
      "Preposition", "Conjunction", "Interjection", "Determiner",
      "Article", "Numeral", "Particle", "Prefix", "Suffix",
      "Phrase", "Proverb", "Idiom", "Synonym", "Synonyms",
      "Antonym", "Antonyms", "Etymology", "Usage Note",
      "Usage Notes", "Alternative Forms", "Derived Terms",
      "Related Terms",
  };
  return std::any_of(std::begin(HEADINGS), std::end(HEADINGS),
                     [text](const char* heading) { return strcmp(text, heading) == 0; });
}

bool isBlankByte(const char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }

std::string trimmed(const std::string& s) {
  size_t first = 0;
  size_t last = s.size();
  while (first < last && isBlankByte(s[first])) first++;
  while (last > first && isBlankByte(s[last - 1])) last--;
  return s.substr(first, last - first);
}

std::string lowerAscii(std::string s) {
  for (char& c : s) {
    if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
  }
  return s;
}

std::string capitalizeWords(const std::string& s) {
  std::string out = s;
  bool wordStart = true;
  for (char& c : out) {
    if (c == ' ' || c == '\t') {
      wordStart = true;
      continue;
    }
    if (wordStart && c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
    wordStart = false;
  }
  return out;
}

std::string upperFirst(std::string s) {
  if (!s.empty() && s[0] >= 'a' && s[0] <= 'z') s[0] = static_cast<char>(s[0] - 'a' + 'A');
  return s;
}

// Decodes the UTF-8 character at s[i]; returns its length in bytes.
size_t decodeAt(const std::string& s, const size_t i, uint32_t* cp) {
  const unsigned char c0 = static_cast<unsigned char>(s[i]);
  size_t len = 1;
  uint32_t v = c0;
  if ((c0 & 0xE0) == 0xC0) {
    len = 2;
    v = c0 & 0x1Fu;
  } else if ((c0 & 0xF0) == 0xE0) {
    len = 3;
    v = c0 & 0x0Fu;
  } else if ((c0 & 0xF8) == 0xF0) {
    len = 4;
    v = c0 & 0x07u;
  }
  if (i + len > s.size()) {
    *cp = c0;
    return 1;
  }
  for (size_t k = 1; k < len; ++k) v = (v << 6) | (static_cast<unsigned char>(s[i + k]) & 0x3Fu);
  *cp = v;
  return len;
}

void appendUtf8(std::string& out, const uint32_t cp) {
  if (cp < 0x80) {
    out += static_cast<char>(cp);
  } else if (cp < 0x800) {
    out += static_cast<char>(0xC0 | (cp >> 6));
    out += static_cast<char>(0x80 | (cp & 0x3F));
  } else if (cp < 0x10000) {
    out += static_cast<char>(0xE0 | (cp >> 12));
    out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
    out += static_cast<char>(0x80 | (cp & 0x3F));
  } else {
    out += static_cast<char>(0xF0 | (cp >> 18));
    out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
    out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
    out += static_cast<char>(0x80 | (cp & 0x3F));
  }
}

// "&amp;" / "&#39;" at s[i]. Returns the length of the entity, or 0 when it is not one.
size_t decodeEntityAt(const std::string& s, const size_t i, std::string* out) {
  const size_t semi = s.find(';', i + 1);
  if (semi == std::string::npos || semi - i > 10) return 0;
  const std::string name = s.substr(i + 1, semi - i - 1);
  if (name == "amp") {
    *out = "&";
  } else if (name == "lt") {
    *out = "<";
  } else if (name == "gt") {
    *out = ">";
  } else if (name == "quot") {
    *out = "\"";
  } else if (name == "apos") {
    *out = "'";
  } else if (name == "nbsp") {
    *out = "\xC2\xA0";
  } else if (name.size() > 1 && name[0] == '#') {
    uint32_t cp = 0;
    size_t k = 1;
    uint32_t base = 10;
    if (name[k] == 'x' || name[k] == 'X') {
      base = 16;
      ++k;
    }
    if (k >= name.size()) return 0;
    for (; k < name.size(); ++k) {
      const char ch = name[k];
      uint32_t d = 0;
      if (ch >= '0' && ch <= '9') {
        d = static_cast<uint32_t>(ch - '0');
      } else if (base == 16 && ch >= 'a' && ch <= 'f') {
        d = static_cast<uint32_t>(ch - 'a' + 10);
      } else if (base == 16 && ch >= 'A' && ch <= 'F') {
        d = static_cast<uint32_t>(ch - 'A' + 10);
      } else {
        return 0;
      }
      cp = cp * base + d;
    }
    if (cp == 0 || cp > 0x10FFFF) return 0;
    out->clear();
    appendUtf8(*out, cp);
  } else {
    return 0;
  }
  return semi - i + 1;
}

// The real parts of speech: a line that is just one of these is a heading.
bool isTruePos(const std::string& s) {
  static constexpr const char* POS[] = {"noun",        "verb",        "adjective", "adverb",
                                        "pronoun",     "preposition", "conjunction", "interjection",
                                        "determiner",  "article",     "numeral",   "particle"};
  const std::string low = lowerAscii(s);
  return std::any_of(std::begin(POS), std::end(POS), [&low](const char* p) { return low == p; });
}

// Cambridge sub-section names: "Verbs", "Adjectives", ... (lower case in)
bool isSubLabel(const std::string& low) {
  static constexpr const char* LABELS[] = {"verbs",    "adjectives", "adverbs",      "nouns",  "prepositions",
                                           "phrases",  "types",      "idioms",       "pronouns", "conjunctions",
                                           "phrasal verbs", "determiners"};
  return std::any_of(std::begin(LABELS), std::end(LABELS), [&low](const char* l) { return low == l; });
}

// "verb transitive", "noun countable": a line made only of part-of-speech words (lower case in).
bool isPosPhrase(const std::string& low) {
  static constexpr const char* WORDS[] = {"noun",     "verb",      "adjective", "adverb",    "pronoun",
                                          "preposition", "conjunction", "interjection", "determiner", "exclamation",
                                          "transitive", "intransitive", "countable", "uncountable", "plural",
                                          "singular", "phrasal",   "modal",     "auxiliary", "abbreviation",
                                          "prefix",   "suffix",    "idiom",     "symbol"};
  static constexpr const char* POS[] = {"noun", "verb", "adjective", "adverb", "pronoun", "preposition",
                                        "conjunction", "interjection", "determiner", "exclamation"};
  int words = 0;
  bool hasPos = false;
  size_t i = 0;
  while (i < low.size()) {
    while (i < low.size() && low[i] == ' ') ++i;
    if (i >= low.size()) break;
    const size_t start = i;
    while (i < low.size() && low[i] != ' ') ++i;
    const std::string w = low.substr(start, i - start);
    if (!std::any_of(std::begin(WORDS), std::end(WORDS), [&w](const char* k) { return w == k; })) return false;
    if (std::any_of(std::begin(POS), std::end(POS), [&w](const char* k) { return w == k; })) hasPos = true;
    if (++words > 4) return false;
  }
  return words > 0 && hasPos;
}

// Section names that open a block of related words ("See also: ...", "Type of: ...").
bool isSectionLabel(const std::string& lowHead) {
  static constexpr const char* LABELS[] = {
      "see also", "see", "type of", "types", "encyclopedia", "derived forms", "derived form", "derived",
      "part of", "parts", "has parts", "sounds like", "antonym", "antonyms", "synonym", "synonyms", "similar",
      "hypernym", "hyponym", "member of", "members", "has members", "substance of", "substances",
      "has substances", "has types", "category", "categories", "usage", "domain", "verb group", "entails",
      "cause", "causes", "pertains to", "related", "related words", "phrases", "phrase", "attributes",
      "wikipedia", "example", "examples", "collocations", "word family", "idioms", "idiom", "phrasal verbs", "phrasal verb", "proverb", "proverbs", "compare",
      "opposite", "origin", "etymology", "derivatives", "also"};
  return std::any_of(std::begin(LABELS), std::end(LABELS), [&lowHead](const char* l) { return lowHead == l; });
}

// True when the text holds a real letter or digit. Punctuation, bullets, squares and dashes do not count.
bool hasLetterOrDigit(const std::string& s) {
  for (size_t i = 0; i < s.size();) {
    uint32_t cp = 0;
    const size_t len = decodeAt(s, i, &cp);
    if (cp < 0x80) {
      if (std::isalnum(static_cast<int>(cp))) return true;
    } else if (cp == 0xAA || cp == 0xB5 || cp == 0xBA) {
      return true;
    } else if (cp >= 0xC0 && cp != 0xD7 && cp != 0xF7 && !(cp >= 0x2000 && cp <= 0x2BFF) &&
               !(cp >= 0x3000 && cp <= 0x303F) && !(cp >= 0xFE00 && cp <= 0xFE0F)) {
      return true;
    }
    i += len;
  }
  return false;
}

// "FLAVOUR": a short word written in capitals (Cambridge guidewords).
bool isAllCapsWord(const std::string& s) {
  if (s.size() < 3 || s.size() > 32) return false;
  int letters = 0;
  for (const char ch : s) {
    if (ch >= 'a' && ch <= 'z') return false;
    if (ch >= '0' && ch <= '9') return false;
    if (ch >= 'A' && ch <= 'Z') ++letters;
  }
  return letters >= 3;
}

bool isQuotedExample(const std::string& s) {
  if (s.size() < 4) return false;
  const bool opens = s[0] == '"' || s.compare(0, 3, "\xE2\x80\x9C") == 0;
  const bool closes = s.back() == '"' || s.compare(s.size() - 3, 3, "\xE2\x80\x9D") == 0;
  return opens && closes;
}

// Odd spaces become plain spaces and invisible characters are dropped, so a line made only of them is empty
// and a line starting with one has no gap before its text.
std::string normalizeSpaces(const std::string& s) {
  std::string out;
  out.reserve(s.size());
  for (size_t i = 0; i < s.size();) {
    uint32_t cp = 0;
    const size_t len = decodeAt(s, i, &cp);
    if (cp == 0xA0 || (cp >= 0x2000 && cp <= 0x200A) || cp == 0x202F || cp == 0x205F || cp == 0x3000) {
      out += ' ';
    } else if (cp == 0xAD || (cp >= 0x200B && cp <= 0x200F) || (cp >= 0x2028 && cp <= 0x202E) ||
               (cp >= 0x2060 && cp <= 0x206F) || cp == 0xFEFF || (cp >= 0xFE00 && cp <= 0xFE0F)) {
      // invisible: dropped
    } else {
      out.append(s, i, len);
    }
    i += len;
  }
  return out;
}

// "< A >" / "<E >" -> "<A>" / "<E>", and "[ C ]" -> "[C]".
std::string tidyCodes(std::string s) {
  for (size_t p = s.find('<'); p != std::string::npos; p = s.find('<', p + 1)) {
    size_t q = p + 1;
    while (q < s.size() && s[q] == ' ') ++q;
    const size_t codeStart = q;
    while (q < s.size() && q - codeStart < 4 && ((s[q] >= 'A' && s[q] <= 'Z') || (s[q] >= '0' && s[q] <= '9'))) ++q;
    if (q == codeStart) continue;
    size_t r = q;
    while (r < s.size() && s[r] == ' ') ++r;
    if (r < s.size() && s[r] == '>') s.replace(p, r - p + 1, "<" + s.substr(codeStart, q - codeStart) + ">");
  }
  for (size_t p = s.find('['); p != std::string::npos; p = s.find('[', p + 1)) {
    const size_t close = s.find(']', p + 1);
    if (close == std::string::npos || close - p > 60) continue;
    const std::string inner = trimmed(s.substr(p + 1, close - p - 1));
    if (inner.find('[') != std::string::npos) continue;
    s.replace(p + 1, close - p - 1, inner);
  }
  return s;
}

// True when the text holds phonetic (IPA) symbols or stress marks.
bool containsIpa(const std::string& s) {
  for (size_t i = 0; i < s.size();) {
    uint32_t cp = 0;
    const size_t len = decodeAt(s, i, &cp);
    if ((cp >= 0x250 && cp <= 0x2AF) || cp == 0x2C8 || cp == 0x2CC || cp == 0x2D0 || cp == 0x2D1 ||
        (cp >= 0x1D00 && cp <= 0x1D7F)) {
      return true;
    }
    i += len;
  }
  return false;
}

// A pronunciation line: /.../, phonetic symbols, or Cambridge's "UK [...] US [...]".
bool isPronunciationLine(const std::string& t) {
  if (t.size() >= 2 && t.front() == '/' && t.back() == '/') return true;
  if (t.size() > 160) return false;
  if (containsIpa(t)) return true;
  static constexpr const char* MARKS[] = {"UK [", "US [", "UK /", "US /", "UK[", "US["};
  return std::any_of(std::begin(MARKS), std::end(MARKS), [&t](const char* m) { return t.find(m) != std::string::npos; });
}

bool isExampleBullet(const uint32_t cp) {
  return cp == 0xB7 || cp == 0x2022 || cp == 0x2023 || cp == 0x2027 || cp == 0x2043 || cp == 0x2219 ||
         cp == 0x25CF || cp == 0x25CB || cp == 0x25E6 || cp == 0x25AA;
}
bool isMarkerShape(const uint32_t cp) { return cp >= 0x25A0 && cp <= 0x25FF; }
bool isDashCodepoint(const uint32_t cp) {
  return cp == '-' || cp == 0x2010 || cp == 0x2011 || cp == 0x2013 || cp == 0x2014 || cp == 0x2212;
}

// Removes one leading bullet, marker, dash or number.
// *kind: 0 nothing, 1 round bullet (an example in Cambridge), 2 marker or dash, 3 number.
std::string stripOneMarker(const std::string& t, int* kind) {
  *kind = 0;
  if (t.empty()) return t;
  uint32_t cp = 0;
  const size_t len = decodeAt(t, 0, &cp);
  if (isExampleBullet(cp)) {
    *kind = 1;
    return trimmed(t.substr(len));
  }
  if (isMarkerShape(cp)) {  // Oxford's grey square before a sub-sense
    *kind = 2;
    return trimmed(t.substr(len));
  }
  if (isDashCodepoint(cp) && t.size() > len && t[len] == ' ') {
    *kind = 2;
    return trimmed(t.substr(len));
  }
  size_t i = 0;
  while (i < t.size() && i < 3 && t[i] >= '0' && t[i] <= '9') i++;
  if (i > 0 && i < t.size() && (t[i] == '.' || t[i] == ')') && (i + 1 == t.size() || t[i + 1] == ' ')) {
    *kind = 3;
    return trimmed(t.substr(i + 1));
  }
  return t;
}

// Several markers can stack ("1) - text"): all of them are removed. *kind is 1 when any was a round bullet.
std::string stripLeadingMarker(const std::string& in, int* kind) {
  *kind = 0;
  std::string t = in;
  for (int pass = 0; pass < 4 && !t.empty(); ++pass) {
    int k = 0;
    const std::string next = stripOneMarker(t, &k);
    if (k == 0) break;
    if (k == 1) {
      *kind = 1;
    } else if (*kind == 0) {
      *kind = k;
    }
    t = next;
  }
  return t;
}

// A dash left hanging at the end of a line ("in the year of grace -") is dropped.
std::string stripTrailingDash(std::string t) {
  static constexpr const char* DASHES[] = {" -", " \xE2\x80\x93", " \xE2\x80\x94"};
  for (bool again = true; again;) {
    again = false;
    for (const char* d : DASHES) {
      const size_t dl = strlen(d);
      if (t.size() > dl && t.compare(t.size() - dl, dl, d) == 0) {
        t = trimmed(t.substr(0, t.size() - dl));
        again = true;
      }
    }
  }
  return t;
}

// WordWeb puts its respelling in a gray span ("<span style="COLOR:GRAY" title="Key: ...">row'mans</span>").
// It is removed before layout, so it never gets glued to the definition.
std::string stripWordWebRespelling(const std::string& html) {
  static constexpr const char* OPEN = "<span style=\"COLOR:GRAY\"";
  std::string out;
  out.reserve(html.size());
  size_t i = 0;
  while (i < html.size()) {
    const size_t p = html.find(OPEN, i);
    if (p == std::string::npos) {
      out.append(html, i, std::string::npos);
      break;
    }
    const size_t gt = html.find('>', p);
    const size_t close = html.find("</span>", p);
    const bool respelling =
        gt != std::string::npos && close != std::string::npos && html.substr(p, gt - p).find("Key:") != std::string::npos;
    out.append(html, i, p - i);
    if (!respelling) {
      out.append(html, p, 1);
      i = p + 1;
      continue;
    }
    i = close + 7;
  }
  return out;
}

// ---- named HTML entities ----
// The XML parser only knows &amp; &lt; &gt; &quot; &apos; and numeric references. Any other name makes it reject the
// whole entry, which then falls back to raw text. Named entities are turned into the character itself first.
struct NamedEntity {
  const char* name;
  uint32_t cp;  // 0 = dropped
};

constexpr NamedEntity kEntities[] = {
    {"lsqb", 0x5B},   {"rsqb", 0x5D},   {"lbrack", 0x5B}, {"rbrack", 0x5D}, {"lpar", 0x28},   {"rpar", 0x29},
    {"lcub", 0x7B},   {"rcub", 0x7D},   {"lbrace", 0x7B}, {"rbrace", 0x7D}, {"num", 0x23},    {"dollar", 0x24},
    {"percnt", 0x25}, {"ast", 0x2A},    {"plus", 0x2B},   {"comma", 0x2C},  {"period", 0x2E}, {"colon", 0x3A},
    {"semi", 0x3B},   {"equals", 0x3D}, {"quest", 0x3F},  {"excl", 0x21},   {"commat", 0x40}, {"sol", 0x2F},
    {"bsol", 0x5C},   {"lowbar", 0x5F}, {"grave", 0x60},  {"verbar", 0x7C}, {"vert", 0x7C},   {"hyphen", 0x2D},
    {"dash", 0x2D},   {"nbsp", 0xA0},   {"ensp", 0x2002}, {"emsp", 0x2003}, {"thinsp", 0x2009}, {"hairsp", 0x200A},
    {"ndash", 0x2013}, {"mdash", 0x2014}, {"hellip", 0x2026}, {"lsquo", 0x2018}, {"rsquo", 0x2019},
    {"sbquo", 0x201A}, {"ldquo", 0x201C}, {"rdquo", 0x201D}, {"bdquo", 0x201E}, {"laquo", 0xAB}, {"raquo", 0xBB},
    {"bull", 0x2022}, {"middot", 0xB7}, {"copy", 0xA9},   {"reg", 0xAE},    {"trade", 0x2122}, {"deg", 0xB0},
    {"plusmn", 0xB1}, {"times", 0xD7},  {"divide", 0xF7}, {"frac12", 0xBD}, {"frac14", 0xBC}, {"frac34", 0xBE},
    {"pound", 0xA3},  {"euro", 0x20AC}, {"yen", 0xA5},    {"cent", 0xA2},   {"sect", 0xA7},  {"para", 0xB6},
    {"dagger", 0x2020}, {"Dagger", 0x2021}, {"prime", 0x2032}, {"Prime", 0x2033}, {"micro", 0xB5},
    {"sup1", 0xB9},   {"sup2", 0xB2},   {"sup3", 0xB3},   {"ordf", 0xAA},   {"ordm", 0xBA},  {"iexcl", 0xA1},
    {"iquest", 0xBF}, {"minus", 0x2212}, {"ne", 0x2260},  {"le", 0x2264},   {"ge", 0x2265},
    {"larr", 0x2190}, {"rarr", 0x2192}, {"uarr", 0x2191}, {"darr", 0x2193},
    {"shy", 0},       {"zwj", 0},       {"zwnj", 0},      {"lrm", 0},       {"rlm", 0},
    {"Agrave", 0xC0}, {"Aacute", 0xC1}, {"Acirc", 0xC2},  {"Atilde", 0xC3}, {"Auml", 0xC4},  {"Aring", 0xC5},
    {"AElig", 0xC6},  {"Ccedil", 0xC7}, {"Egrave", 0xC8}, {"Eacute", 0xC9}, {"Ecirc", 0xCA}, {"Euml", 0xCB},
    {"Igrave", 0xCC}, {"Iacute", 0xCD}, {"Icirc", 0xCE},  {"Iuml", 0xCF},   {"Ntilde", 0xD1}, {"Ograve", 0xD2},
    {"Oacute", 0xD3}, {"Ocirc", 0xD4},  {"Otilde", 0xD5}, {"Ouml", 0xD6},   {"Oslash", 0xD8}, {"Ugrave", 0xD9},
    {"Uacute", 0xDA}, {"Ucirc", 0xDB},  {"Uuml", 0xDC},   {"Yacute", 0xDD}, {"szlig", 0xDF},
    {"agrave", 0xE0}, {"aacute", 0xE1}, {"acirc", 0xE2},  {"atilde", 0xE3}, {"auml", 0xE4},  {"aring", 0xE5},
    {"aelig", 0xE6},  {"ccedil", 0xE7}, {"egrave", 0xE8}, {"eacute", 0xE9}, {"ecirc", 0xEA}, {"euml", 0xEB},
    {"igrave", 0xEC}, {"iacute", 0xED}, {"icirc", 0xEE},  {"iuml", 0xEF},   {"ntilde", 0xF1}, {"ograve", 0xF2},
    {"oacute", 0xF3}, {"ocirc", 0xF4},  {"otilde", 0xF5}, {"ouml", 0xF6},   {"oslash", 0xF8}, {"ugrave", 0xF9},
    {"uacute", 0xFA}, {"ucirc", 0xFB},  {"uuml", 0xFC},   {"yacute", 0xFD}, {"yuml", 0xFF},
};

// An entity at s[i] ('&'): the XML ones and numeric references are decoded; named HTML ones come from the table;
// any other well-formed name is dropped (it would show as raw "&name;" text). Returns the length used, or 0.
size_t decodeEntity(const std::string& s, const size_t i, std::string* out) {
  const size_t used = decodeEntityAt(s, i, out);
  if (used > 0) return used;
  size_t j = i + 1;
  while (j < s.size() && j - i <= 32 && std::isalnum(static_cast<unsigned char>(s[j]))) ++j;
  if (j >= s.size() || s[j] != ';' || j == i + 1) return 0;
  const std::string name = s.substr(i + 1, j - i - 1);
  out->clear();
  for (const auto& e : kEntities) {
    if (name == e.name) {
      if (e.cp != 0) appendUtf8(*out, e.cp);
      break;
    }
  }
  return j - i + 1;
}

// Rewrites every named entity before the text reaches the XML parser. The five XML entities and numeric
// references stay as they are, because the parser knows them.
std::string normalizeEntities(const std::string& s) {
  std::string out;
  out.reserve(s.size());
  for (size_t i = 0; i < s.size();) {
    if (s[i] != '&') {
      out += s[i++];
      continue;
    }
    const size_t semi = s.find(';', i + 1);
    if (semi != std::string::npos && semi - i <= 12) {
      const std::string name = s.substr(i + 1, semi - i - 1);
      if (name == "amp" || name == "lt" || name == "gt" || name == "quot" || name == "apos" ||
          (!name.empty() && name[0] == '#')) {
        out.append(s, i, semi - i + 1);
        i = semi + 1;
        continue;
      }
    }
    std::string decoded;
    const size_t used = decodeEntity(s, i, &decoded);
    if (used > 0) {
      out += decoded;
      i += used;
    } else {
      out += s[i++];
    }
  }
  return out;
}

// ---- characters the UI font cannot draw ----
bool isUnsupportedCodepoint(const uint32_t cp) {
  return (cp >= 0x250 && cp <= 0x2FF) ||    // phonetic letters and modifiers
         (cp >= 0x370 && cp <= 0x10FF) ||   // Greek, Cyrillic, Armenian, Hebrew, Arabic, Indic, Thai, Georgian
         (cp >= 0x1200 && cp <= 0x1AFF) ||  // Ethiopic, Cherokee, Runic, Khmer, ...
         (cp >= 0x1C80 && cp <= 0x1DBF) ||  // more Cyrillic, phonetic extensions
         (cp >= 0x1F00 && cp <= 0x1FFF) ||  // Greek extended
         (cp >= 0x2070 && cp <= 0x209F) ||  // super- and subscripts
         (cp >= 0x2100 && cp <= 0x23FF) ||  // letterlike symbols, arrows, maths, technical
         (cp >= 0x2460 && cp <= 0x27BF) ||  // enclosed characters, boxes, shapes, dingbats
         (cp >= 0x2900 && cp <= 0x2BFF) ||  // more arrows and symbols
         (cp >= 0x2DE0 && cp <= 0x2DFF) || (cp >= 0xA640 && cp <= 0xA69F) ||  // Cyrillic extended
         (cp >= 0xE000 && cp <= 0xF8FF) ||  // private use
         (cp >= 0xFFF0 && cp <= 0xFFFF) ||  // specials
         (cp >= 0x10000 && cp <= 0x1FFFF);  // emoji, maths alphanumerics, historic scripts
}

void replaceAll(std::string& s, const char* from, const char* to) {
  const size_t fl = strlen(from);
  const size_t tl = strlen(to);
  for (size_t p = s.find(from); p != std::string::npos; p = s.find(from, p + tl)) s.replace(p, fl, to);
}

// Removes characters the UI font has no glyph for, then tidies the gap (doubled spaces, empty brackets).
std::string stripUnsupportedGlyphs(const std::string& s) {
  if (!kStripUnsupportedGlyphs) return s;
  std::string out;
  out.reserve(s.size());
  bool changed = false;
  for (size_t i = 0; i < s.size();) {
    uint32_t cp = 0;
    const size_t len = decodeAt(s, i, &cp);
    if (isUnsupportedCodepoint(cp)) {
      changed = true;
    } else {
      out.append(s, i, len);
    }
    i += len;
  }
  if (!changed) return s;
  for (int pass = 0; pass < 4; ++pass) {
    const std::string before = out;
    replaceAll(out, "  ", " ");
    replaceAll(out, "( ", "(");
    replaceAll(out, " )", ")");
    replaceAll(out, "()", "");
    replaceAll(out, "[]", "");
    replaceAll(out, "\xE2\x80\x9C\xE2\x80\x9D", "");
    replaceAll(out, ": )", ")");
    replaceAll(out, ", )", ")");
    replaceAll(out, "; )", ")");
    replaceAll(out, " ,", ",");
    replaceAll(out, ",,", ",");
    if (out == before) break;
  }
  return trimmed(out);
}

// ---- headings ----
bool isKnownHeading(const std::string& low) {
  static constexpr const char* NAMES[] = {
      "noun", "proper noun", "proper name", "verb", "adjective", "adverb", "pronoun", "preposition",
      "conjunction", "interjection", "determiner", "article", "numeral", "particle", "prefix", "suffix",
      "infix", "circumfix", "affix", "phrase", "proverb", "idiom", "initialism", "abbreviation", "acronym",
      "contraction", "symbol", "letter", "character", "number", "classifier", "postposition", "participle",
      "root", "combining form", "verb phrase", "noun phrase", "prepositional phrase", "alternative forms",
      "alternative form", "alternative spellings", "derived terms", "related terms", "synonyms", "antonyms",
      "hypernyms", "hyponyms", "etymology", "pronunciation", "usage notes", "usage note", "translations",
      "descendants", "anagrams", "references", "further reading", "see also", "collocations", "statistics"};
  return std::any_of(std::begin(NAMES), std::end(NAMES), [&low](const char* n) { return low == n; });
}

// A paragraph that is bold from start to end is a heading only when it reads like one: a known section name, or a
// short Title Case phrase of 2 to 4 words ("Proper Noun"). A single word or a sentence-case phrase
// ("A place name") is a definition.
bool looksLikeHeadingText(const std::string& raw) {
  std::string t = trimmed(raw);
  while (!t.empty() && t.back() == ':') t.pop_back();
  t = trimmed(t);
  if (t.empty() || t.size() > 40) return false;
  const std::string low = lowerAscii(t);
  if (isKnownHeading(low) || isSectionLabel(low) || isPosPhrase(low)) return true;
  int words = 0;
  size_t i = 0;
  while (i < t.size()) {
    while (i < t.size() && t[i] == ' ') ++i;
    if (i >= t.size()) break;
    if (!(t[i] >= 'A' && t[i] <= 'Z')) return false;
    while (i < t.size() && t[i] != ' ') {
      const unsigned char ch = static_cast<unsigned char>(t[i]);
      if (!(std::isalpha(ch) || t[i] == '-')) return false;
      ++i;
    }
    ++words;
  }
  return words >= 2 && words <= 4;
}

// ---- WordWeb entries ----
// Fold a "Usage: UK" label in front of a definition into a "[UK]" tag on that definition.
constexpr bool kFoldUsageTag = true;

struct Para {
  std::string text;
  bool heading;
  bool italic;
};

// Lower case, without a trailing colon: "Derived forms:" -> "derived forms"
std::string labelOf(const std::string& text) {
  std::string t = trimmed(text);
  while (!t.empty() && t.back() == ':') t.pop_back();
  return lowerAscii(trimmed(t));
}

// Would this paragraph still show something after addParagraph() has cleaned it?
bool paragraphHasContent(const std::string& raw) {
  std::string t = trimmed(tidyCodes(normalizeSpaces(raw)));
  if (t.empty() || !hasLetterOrDigit(t) || isPronunciationLine(t)) return false;
  t = stripUnsupportedGlyphs(t);
  if (t.empty() || !hasLetterOrDigit(t)) return false;
  int kind = 0;
  t = stripTrailingDash(stripLeadingMarker(t, &kind));
  return !t.empty() && hasLetterOrDigit(t);
}

// "Verb: have (had, having, has)": *pos is "Verb" and *base the word itself ("have"). Word forms in the brackets go
// to *forms; any other bracketed text is kept in *extra.
bool splitPosLine(const std::string& text, std::string* pos, std::string* base, std::vector<std::string>* forms,
                  std::string* extra) {
  const size_t colon = text.find(':');
  if (colon == std::string::npos || colon > 20) return false;
  const std::string head = trimmed(text.substr(0, colon));
  if (!isTruePos(head)) return false;
  *pos = capitalizeWords(lowerAscii(head));
  forms->clear();
  extra->clear();
  const std::string rest = trimmed(text.substr(colon + 1));
  const size_t open = rest.find('(');
  *base = lowerAscii(trimmed(open == std::string::npos ? rest : rest.substr(0, open)));
  const size_t close = rest.rfind(')');
  if (open == std::string::npos || close == std::string::npos || close < open) return true;
  const std::string inner = rest.substr(open + 1, close - open - 1);
  std::vector<std::string> items;
  bool wordsOnly = true;
  size_t start = 0;
  while (start <= inner.size()) {
    size_t end = inner.find_first_of(",;", start);
    if (end == std::string::npos) end = inner.size();
    const std::string item = trimmed(inner.substr(start, end - start));
    if (!item.empty()) {
      int words = 1;
      for (const char ch : item) {
        const unsigned char u = static_cast<unsigned char>(ch);
        if (ch == ' ') {
          ++words;
        } else if (!(std::isalpha(u) || u >= 0x80 || ch == '-' || ch == '\'')) {
          wordsOnly = false;
        }
      }
      if (words > 3 || item.size() > 30) wordsOnly = false;
      items.push_back(item);
    }
    if (end >= inner.size()) break;
    start = end + 1;
  }
  if (!items.empty() && wordsOnly) {
    *forms = items;
  } else if (!inner.empty()) {
    *extra = "(" + inner + ")";
  }
  return true;
}

std::string joinList(const std::vector<std::string>& items, const char* sep) {
  std::string out;
  for (size_t i = 0; i < items.size(); ++i) {
    if (i > 0) out += sep;
    out += items[i];
  }
  return out;
}

bool isDerivedLabel(const std::string& l) { return l == "derived forms" || l == "derived form"; }

void splitItems(const std::string& text, std::vector<std::string>* items) {
  size_t start = 0;
  while (start <= text.size()) {
    size_t end = text.find_first_of(",;", start);
    if (end == std::string::npos) end = text.size();
    std::string item = trimmed(text.substr(start, end - start));
    while (!item.empty() && item.back() == '.') item.pop_back();
    if (!item.empty()) items->push_back(item);
    if (end >= text.size()) break;
    start = end + 1;
  }
}

bool containsLower(const std::vector<std::string>& v, const std::string& item) {
  const std::string low = lowerAscii(trimmed(item));
  return std::any_of(v.begin(), v.end(), [&low](const std::string& s) { return lowerAscii(trimmed(s)) == low; });
}

// Is `form` a regular inflection of `base` for this part of speech? (plural, -s, -ing, -ed, -er, -est)
bool isRegularForm(const std::string& form, const std::string& baseRaw, const std::string& pos) {
  const std::string f = lowerAscii(trimmed(form));
  const std::string b = lowerAscii(trimmed(baseRaw));
  if (b.size() < 2 || f.empty()) return false;
  const char last = b.back();
  const bool endsE = last == 'e';
  const bool endsY = last == 'y' && std::string("aeiou").find(b[b.size() - 2]) == std::string::npos;
  const std::string stem = b.substr(0, b.size() - 1);
  std::vector<std::string> v;
  if (pos == "noun" || pos == "verb") {
    v.push_back(b + "s");
    v.push_back(b + "es");
    if (endsY) v.push_back(stem + "ies");
  }
  if (pos == "verb") {
    v.push_back(b + "ing");
    v.push_back(b + "ed");
    v.push_back(b + last + "ing");
    v.push_back(b + last + "ed");
    if (endsE) {
      v.push_back(stem + "ing");
      v.push_back(b + "d");
    }
    if (endsY) v.push_back(stem + "ied");
  }
  if (pos == "adjective" || pos == "adverb") {
    v.push_back(b + "er");
    v.push_back(b + "est");
    v.push_back(b + last + "er");
    v.push_back(b + last + "est");
    if (endsE) {
      v.push_back(b + "r");
      v.push_back(b + "st");
    }
    if (endsY) {
      v.push_back(stem + "ier");
      v.push_back(stem + "iest");
    }
  }
  return std::find(v.begin(), v.end(), f) != v.end();
}

// A short row under "Usage" that is a tag ("UK", "informal"), not a definition.
bool isUsageTag(const std::string& raw, const bool first) {
  const std::string t = trimmed(raw);
  if (t.empty() || t.size() > 24 || t.find('.') != std::string::npos || !paragraphHasContent(t)) return false;
  int words = 1;
  for (const char ch : t) {
    if (ch == ' ') ++words;
  }
  if (words > 2) return false;
  if (first) return true;
  bool caps = true;
  for (const char ch : t) {
    if (ch >= 'a' && ch <= 'z') caps = false;
  }
  return caps || (t[0] >= 'a' && t[0] <= 'z');
}

// WordWeb entries are restructured in four steps:
//  1. a label that arrives as plain text ("Usage: UK", "Encyclopedia:") becomes a heading with its text beneath it;
//  2. "Usage" + "UK" becomes a "[UK]" tag on the definition after it (or before it, or alone);
//  3. a part-of-speech line ("Verb: have (had, having, has)") becomes a bold heading, and its word forms go into
//     that part of speech's own "Derived forms" section, right after its definitions, without repeats. A form
//     belongs to the part of speech that lists it in brackets, else to the one whose word it inflects;
//  4. sections with nothing in them are dropped.
void restructureWordWeb(std::vector<Para>& paras) {
  const auto isPosLine = [](const std::string& t) {
    std::string pos;
    std::string base;
    std::string extra;
    std::vector<std::string> forms;
    return splitPosLine(t, &pos, &base, &forms, &extra);
  };
  // 1. plain-text labels become headings
  std::vector<Para> norm;
  for (const auto& p : paras) {
    if (!p.heading && !p.italic) {
      const size_t colon = p.text.find(':');
      if (colon != std::string::npos && colon > 0 && colon <= 24) {
        const std::string head = trimmed(p.text.substr(0, colon));
        const std::string rest = trimmed(p.text.substr(colon + 1));
        if (!isTruePos(head) && isSectionLabel(lowerAscii(head))) {
          norm.push_back({upperFirst(head), true, false});
          if (!rest.empty()) norm.push_back({rest, false, false});
          continue;
        }
      }
    }
    norm.push_back(p);
  }

  // 2. usage tags
  std::vector<Para> folded;
  for (size_t i = 0; i < norm.size(); ++i) {
    if (!(kFoldUsageTag && norm[i].heading && labelOf(norm[i].text) == "usage")) {
      folded.push_back(norm[i]);
      continue;
    }
    std::vector<std::string> tags;
    size_t j = i + 1;
    // The value of the label is the first row after it, whatever its shape ("UK", "Brit, Cdn (=neighborhood)");
    // a few more short tag rows may follow. Everything after that belongs to the part of speech, not to "Usage".
    if (j < norm.size() && !norm[j].heading && !norm[j].italic && !isPosLine(norm[j].text) &&
        paragraphHasContent(norm[j].text) && trimmed(norm[j].text).size() <= 60) {
      tags.push_back(trimmed(norm[j].text));
      ++j;
      while (j < norm.size() && !norm[j].heading && !norm[j].italic && tags.size() < 3 &&
             isUsageTag(norm[j].text, false)) {
        tags.push_back(trimmed(norm[j].text));
        ++j;
      }
    }
    if (tags.empty()) continue;  // "Usage" with no value: the label goes, the rows below stay where they are
    const std::string tag = "[" + joinList(tags, ", ") + "]";
    if (j < norm.size() && !norm[j].heading && !norm[j].italic && !isPosLine(norm[j].text)) {
      folded.push_back({tag + " " + norm[j].text, false, false});
      i = j;
    } else {
      bool attached = false;
      for (size_t k = folded.size(); k-- > 0;) {
        if (folded[k].heading) break;
        if (folded[k].italic) continue;
        if (isPosLine(folded[k].text)) break;
        folded[k].text = tag + " " + folded[k].text;
        attached = true;
        break;
      }
      if (!attached) folded.push_back({tag, false, false});
      i = j - 1;
    }
  }

  // 3. part-of-speech lines
  struct Block {
    size_t start;
    std::string pos;
    std::string base;
    std::vector<std::string> forms;
  };
  std::vector<Para> out;
  std::vector<Block> blocks;
  for (const auto& p : folded) {
    std::string pos;
    std::string base;
    std::string extra;
    std::vector<std::string> forms;
    if (splitPosLine(p.text, &pos, &base, &forms, &extra)) {
      out.push_back({pos, true, false});
      blocks.push_back({out.size() - 1, lowerAscii(pos), base, forms});
      if (!extra.empty()) out.push_back({extra, false, false});
      continue;
    }
    out.push_back(p);
  }

  const size_t nb = blocks.size();
  const auto ownerOf = [&](const std::string& item, const size_t b) -> size_t {
    if (containsLower(blocks[b].forms, item)) return b;
    for (size_t k = 0; k < nb; ++k) {
      if (k != b && containsLower(blocks[k].forms, item)) return k;
    }
    if (isRegularForm(item, blocks[b].base, blocks[b].pos)) return b;
    for (size_t k = 0; k < nb; ++k) {
      if (k != b && isRegularForm(item, blocks[k].base, blocks[k].pos)) return k;
    }
    return b;
  };

  std::vector<std::vector<std::string>> kept(nb), moved(nb);
  for (size_t b = 0; b < nb; ++b) {
    const size_t start = blocks[b].start;
    const size_t end = b + 1 < nb ? blocks[b + 1].start : out.size();
    for (size_t k = start + 1; k < end; ++k) {
      if (!(out[k].heading && isDerivedLabel(labelOf(out[k].text)))) continue;
      for (size_t e = k + 1; e < end && !out[e].heading; ++e) {
        std::vector<std::string> found;
        splitItems(out[e].text, &found);
        for (const auto& item : found) {
          const size_t owner = ownerOf(item, b);
          (owner == b ? kept[b] : moved[owner]).push_back(item);
        }
      }
    }
  }
  std::vector<std::vector<std::string>> items(nb);
  for (size_t b = 0; b < nb; ++b) {
    const auto add = [&](const std::string& item) {
      if (!containsLower(items[b], item)) items[b].push_back(item);
    };
    for (const auto& it : blocks[b].forms) add(it);
    for (const auto& it : kept[b]) add(it);
    for (const auto& it : moved[b]) add(it);
  }

  // last block first, so the positions of the earlier blocks stay valid
  for (size_t b = nb; b-- > 0;) {
    const size_t start = blocks[b].start;
    size_t end = b + 1 < nb ? blocks[b + 1].start : out.size();
    // the old "Derived forms" sections leave; one new section goes right after the definitions
    for (size_t k = start + 1; k < end;) {
      if (out[k].heading && isDerivedLabel(labelOf(out[k].text))) {
        size_t e = k + 1;
        while (e < end && !out[e].heading) ++e;
        out.erase(out.begin() + static_cast<std::ptrdiff_t>(k), out.begin() + static_cast<std::ptrdiff_t>(e));
        end -= e - k;
      } else {
        ++k;
      }
    }
    bool hasContent = false;
    for (size_t k = start + 1; k < end && !hasContent; ++k) {
      hasContent = !out[k].heading && paragraphHasContent(out[k].text);
    }
    if (!hasContent) {  // a part of speech with nothing in it: only its heading goes
      out.erase(out.begin() + static_cast<std::ptrdiff_t>(start));
      continue;
    }
    if (items[b].empty()) continue;
    size_t at = end;
    for (size_t k = start + 1; k < end; ++k) {
      if (out[k].heading) {
        at = k;
        break;
      }
    }
    const Para head{"Derived forms", true, false};
    const Para line{joinList(items[b], ", "), false, false};
    out.insert(out.begin() + static_cast<std::ptrdiff_t>(at), line);
    out.insert(out.begin() + static_cast<std::ptrdiff_t>(at), head);
  }

  // 4. headings with nothing under them
  for (bool again = true; again;) {
    again = false;
    for (size_t i = 0; i < out.size(); ++i) {
      if (!out[i].heading) continue;
      const bool isPos = isTruePos(labelOf(out[i].text));
      bool hasContent = false;
      for (size_t k = i + 1; k < out.size(); ++k) {
        if (out[k].heading) {
          if (!isPos || isTruePos(labelOf(out[k].text))) break;
          continue;
        }
        if (paragraphHasContent(out[k].text)) {
          hasContent = true;
          break;
        }
      }
      if (!hasContent) {
        out.erase(out.begin() + static_cast<std::ptrdiff_t>(i));
        again = true;
        break;
      }
    }
  }
  paras = std::move(out);
}

// WordWeb: only a leading "[UK]"-style tag is drawn bold.
std::string leadingBracketMask(const std::string& text) {
  if (text.empty() || text[0] != '[') return std::string();
  const size_t q = text.find(']');
  if (q == std::string::npos || q > 40) return std::string();
  std::string mask(text.size(), '0');
  for (size_t k = 0; k <= q; ++k) mask[k] = '1';
  return mask;
}

// ---- Oxford section numbers, bracket spacing, runs of part-of-speech tags, heading levels ----
// "I", "II." or "III)" -> "I", "II", "III": the big section numbers of an entry. Empty when it is not one.
std::string romanLabel(const std::string& raw) {
  std::string t = trimmed(raw);
  if (!t.empty() && (t.back() == '.' || t.back() == ')')) t.pop_back();
  static constexpr const char* NUMERALS[] = {"I",  "II",  "III", "IV",  "V",   "VI",  "VII", "VIII",
                                             "IX", "X",   "XI",  "XII", "XIII", "XIV", "XV"};
  for (const char* num : NUMERALS) {
    if (t == num) return t;
  }
  return std::string();
}

bool isRomanHeading(const std::string& s) {
  return s.size() >= 2 && s.back() == ')' && !romanLabel(s.substr(0, s.size() - 1)).empty();
}

// Text made only of separators between two part-of-speech tags: ",", "&", "and", "or".
bool isSeparatorOnly(const std::string& s) {
  std::string t = s;
  for (char& ch : t) {
    if (ch == ',' || ch == ';' || ch == '&' || ch == '/') ch = ' ';
  }
  t = lowerAscii(trimmed(t));
  return t.empty() || t == "and" || t == "or";
}

std::string joinPosSeparator(const std::string& raw) {
  if (raw.find('&') != std::string::npos) return " & ";
  const std::string low = lowerAscii(raw);
  if (low.find("and") != std::string::npos) return " and ";
  if (low.find("or") != std::string::npos) return " or ";
  return ", ";
}

// "( pl. opportunities)" -> "(pl. opportunities)": no space just inside a bracket.
std::string tidyParens(std::string s) {
  for (bool again = true; again;) {
    again = false;
    for (size_t p = s.find("( "); p != std::string::npos; p = s.find("( ", p)) {
      s.erase(p + 1, 1);
      again = true;
    }
    for (size_t p = s.find(" )"); p != std::string::npos; p = s.find(" )", p)) {
      s.erase(p, 1);
      again = true;
    }
  }
  return s;
}

// 0 = a big section number ("I)"), 1 = a part of speech, 2 = any other section heading.
int headingLevel(const std::string& text) {
  if (isRomanHeading(text)) return 0;
  std::string cleaned = lowerAscii(trimmed(text));
  for (char& ch : cleaned) {
    if (ch == ',' || ch == '&' || ch == '/') ch = ' ';
  }
  std::string words;
  size_t i = 0;
  while (i < cleaned.size()) {
    while (i < cleaned.size() && cleaned[i] == ' ') ++i;
    const size_t start = i;
    while (i < cleaned.size() && cleaned[i] != ' ') ++i;
    if (i > start) {
      const std::string w = cleaned.substr(start, i - start);
      if (w != "and" && w != "or") words += (words.empty() ? "" : " ") + w;
    }
  }
  return isPosPhrase(words) ? 1 : 2;
}

// ---- Cambridge's first line: "taste UK US [teɪst] noun [tastes]" ----
std::string collapseSpaces(const std::string& s) {
  std::string o;
  bool space = false;
  for (const char ch : s) {
    if (isBlankByte(ch)) {
      space = !o.empty();
      continue;
    }
    if (space) o += ' ';
    space = false;
    o += ch;
  }
  return o;
}

bool looksLikeCambridgeHeader(const std::string& text) {
  if (text.size() > 220) return false;
  const std::string t = collapseSpaces(text);
  static constexpr const char* MARKS[] = {"UK [", "US [", "UK /", "US /", "UK[", "US["};
  return std::any_of(std::begin(MARKS), std::end(MARKS), [&t](const char* m) { return t.find(m) != std::string::npos; });
}

// The part of speech named in that line ("Noun", "Adjective", "Modal verb"), outside the brackets; empty if none.
// "adjective before noun" and "phrasal verb" are notes, not the part of speech.
std::string leadingPartOfSpeech(const std::string& text, const std::string& headword) {
  static constexpr const char* POS[] = {"noun",        "verb",       "adjective", "adverb",   "pronoun",
                                        "preposition", "conjunction", "determiner", "exclamation", "interjection",
                                        "number",      "prefix",     "suffix",    "abbreviation", "symbol"};
  std::string t = collapseSpaces(text);
  const std::string hw = lowerAscii(collapseSpaces(headword));
  if (!hw.empty() && lowerAscii(t).compare(0, hw.size(), hw) == 0) t = t.substr(hw.size());
  std::string prev;
  int depth = 0;
  size_t i = 0;
  while (i < t.size()) {
    const char ch = t[i];
    if (ch == '[' || ch == '(') ++depth;
    if (ch == ']' || ch == ')') depth = std::max(0, depth - 1);
    if (depth == 0 && std::isalpha(static_cast<unsigned char>(ch))) {
      size_t j = i;
      while (j < t.size() && std::isalpha(static_cast<unsigned char>(t[j]))) ++j;
      const std::string w = lowerAscii(t.substr(i, j - i));
      i = j;
      bool isPos = false;
      for (const char* p : POS) {
        if (w == p) isPos = true;
      }
      if (isPos && prev != "before" && prev != "after" && prev != "phrasal") {
        if (w == "verb" && (prev == "modal" || prev == "auxiliary")) return upperFirst(prev) + " verb";
        return upperFirst(w);
      }
      prev = w;
      continue;
    }
    ++i;
  }
  return std::string();
}

// ---- Cambridge sections ----
// Blocks of word patterns: bold phrases inside them are entries, not senses.
bool isCambridgeBlockLabel(const std::string& low) {
  return low == "collocations" || low == "idioms" || low == "idiom" || low == "phrasal verbs" || low == "phrasal verb" ||
         low == "proverb" || low == "proverbs" || low == "common errors" ||
         low == "phrases" || low == "phrase" || low == "word family" || low == "language note";
}

// Headings that are not senses: they are never numbered.
bool isCambridgeLabel(const std::string& low) {
  return isCambridgeBlockLabel(low) || low == "thesaurus+" || low == "thesaurus" || low == "see also" ||
         isSectionLabel(low) || isSubLabel(low) || isKnownHeading(low);
}

// Cambridge part-of-speech lines: "verb transitive" -> "Transitive verb", "verb intransitive" -> "Intransitive verb";
// anything else ("modal verb", "exclamation") is just capitalised.
std::string normalizePosName(const std::string& raw) {
  const std::string low = lowerAscii(collapseSpaces(raw));
  if (low.compare(0, 5, "verb ") == 0) {
    const std::string rest = low.substr(5);
    bool onlyTransitivity = !rest.empty();
    size_t i = 0;
    while (i < rest.size() && onlyTransitivity) {
      while (i < rest.size() && (rest[i] == ' ' || rest[i] == ',' || rest[i] == '/')) ++i;
      const size_t start = i;
      while (i < rest.size() && rest[i] != ' ' && rest[i] != ',' && rest[i] != '/') ++i;
      if (i == start) break;
      const std::string w = rest.substr(start, i - start);
      if (w != "transitive" && w != "intransitive" && w != "and" && w != "or") onlyTransitivity = false;
    }
    if (onlyTransitivity) return upperFirst(rest) + " verb";
  }
  return upperFirst(low);
}

// Marks every "[...]" in the text as bold. Empty when there is none.
std::string bracketMask(const std::string& text) {
  std::string mask(text.size(), '0');
  bool any = false;
  for (size_t p = 0; p < text.size(); ++p) {
    if (text[p] != '[') continue;
    const size_t q = text.find(']', p + 1);
    if (q == std::string::npos || q - p > 60) continue;
    for (size_t k = p; k <= q; ++k) mask[k] = '1';
    any = true;
    p = q;
  }
  return any ? mask : std::string();
}

}  // namespace

DictionaryDefinitionActivity::DictionaryDefinitionActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                                           std::string headword, std::string definition,
                                                           const bool htmlDefinition)
    : UiListActivity("DictionaryDefinition", renderer, mappedInput),
      headword(std::move(headword)),
      title_(capitalizeWords(this->headword)),
      definition(std::move(definition)),
      htmlDefinition(htmlDefinition) {}

void DictionaryDefinitionActivity::onEnter() {
  // Normalize StarDict multi-type separators so every parser below sees the whole definition.
  std::replace(definition.begin(), definition.end(), '\0', '\n');
  buildRows();
  UiListActivity::onEnter();
  requestUpdate();
}

int DictionaryDefinitionActivity::measure(const std::string& text, const bool bold, const bool italic) const {
  int style = EpdFontFamily::REGULAR;
  if (bold) style |= EpdFontFamily::BOLD;
  if (italic) style |= EpdFontFamily::ITALIC;
  return renderer.getTextAdvanceX(UI_10_FONT_ID, text.c_str(), static_cast<EpdFontFamily::Style>(style));
}

// The width a list row gives to its label: the row inset and side padding on both sides. The scroll bar is
// hidden, so it takes no room.
int DictionaryDefinitionActivity::labelWidthPx() const {
  const auto& m = UITheme::getInstance().getMetrics();
  return renderer.getScreenWidth() - 2 * (m.listInset + m.listSidePadding);
}

// Rows that fit one screen, worked out from the same margins buildScreen() gives the list.
int DictionaryDefinitionActivity::pageRowsEstimate() const {
  const auto& m = UITheme::getInstance().getMetrics();
  const int top = m.topPadding + m.headerHeight - 3 + (m.verticalSpacing > 6 ? m.verticalSpacing - 6 : 0);
  const int body = renderer.getScreenHeight() - top - m.buttonHintsHeight;
  return std::max(1, body / std::max(1, m.listRowHeight));
}

void DictionaryDefinitionActivity::buildRows() {
  rows_.clear();
  items_.clear();
  pendingNote_.clear();
  guideNumber_ = 0;
  inCollocations_ = false;
  pendingSense_ = 0;

  // Cambridge also uses <dtrn>; its colour markup and UK/US tags tell the two apart.
  const bool cambridge = !htmlDefinition && (definition.find("c=\"darkcyan\"") != std::string::npos ||
                                             definition.find("<abr>UK</abr>") != std::string::npos ||
                                             definition.find("c=\"darkmagenta\"") != std::string::npos);
  oxfordStyle_ = !htmlDefinition && !cambridge && definition.find("<dtrn>") != std::string::npos;
  cambridgeStyle_ = cambridge;
  wordWebStyle_ = htmlDefinition && (definition.find("bword://") != std::string::npos ||
                                     definition.find("COLOR:GRAY") != std::string::npos);

  const bool xdxf = !htmlDefinition && (definition.find("<k>") != std::string::npos ||
                                        definition.find("<dtrn>") != std::string::npos ||
                                        definition.find("<ex>") != std::string::npos ||
                                        definition.find("<blockquote>") != std::string::npos);
  if (xdxf) {
    rowsFromXdxf(definition);
  } else {
    const std::string cleaned = htmlDefinition ? normalizeEntities(stripWordWebRespelling(definition)) : definition;
    bool styled = false;
    if (htmlDefinition && definition.size() <= kMaxStyledHtmlBytes) {
      std::vector<std::unique_ptr<Page>> pages;
      if (buildDictionaryHtmlPages(renderer, UI_10_FONT_ID, cleaned, kLayoutWidth, kLayoutHeight, pages)) {
        rowsFromPages(pages, UI_10_FONT_ID);
        styled = !rows_.empty();
      }
    }
    if (!styled) {
      rows_.clear();
      pendingNote_.clear();
      rowsFromPlainText(htmlToPlainText(cleaned));
    }
  }
  if (rows_.empty() && xdxf) {
    pendingNote_.clear();
    rowsFromPlainText(htmlToPlainText(normalizeEntities(definition)));
  }


  pruneEmptySections();
  numberSenseHeadings();

  // Blank rows pad the end to a whole number of screens, so paging moves exactly one screen at a time and the
  // last screen simply ends early, with no row repeated.
  if (!rows_.empty()) {
    const int cap = pageRowsEstimate();
    while (static_cast<int>(rows_.size()) % cap != 0) rows_.push_back({std::string(), std::string(), false, false, false, 0, false, false});
  }


  items_.reserve(rows_.size());
  for (size_t i = 0; i < rows_.size(); ++i) {
    fui::ListItem item;
    item.label = rows_[i].text.c_str();
    item.bold = rows_[i].bold;
    item.italic = rows_[i].italic;
    item.justify = rows_[i].justify;
    item.boldMask = rows_[i].boldMask.empty() ? nullptr : rows_[i].boldMask.c_str();
    if (rows_[i].ruleAbove) {  // "Thesaurus+": a 6px line right in front of the word
      item.labelXOffset = kThesaurusLead;
      item.leaderLine = true;
    }
    item.actionValue = static_cast<int16_t>(i);
    if (rows_[i].indent > 0) {
      item.labelXOffset = rows_[i].indent;
      item.leaderLine = rows_[i].leader;
    }
    items_.push_back(item);
  }
}

// Reads the laid-out lines back as paragraphs. A word that follows its neighbour without a gap (punctuation,
// styled fragments) is joined without a space. A paragraph whose words are all bold is a heading, one whose
// words are all italic is an example, and a bold run that ends in ":" ("See also:") is a heading of its own.
void DictionaryDefinitionActivity::rowsFromPages(const std::vector<std::unique_ptr<Page>>& pages, const int fontId) {
  const int space = renderer.getSpaceWidth(fontId, EpdFontFamily::REGULAR);
  std::vector<Para> paras;
  for (const auto& page : pages) {
    for (const auto& element : page->elements) {
      if (element->getTag() != TAG_PageLine) continue;
      const auto& block = static_cast<const PageLine*>(element.get())->getBlock();
      if (!block || block->wordCount() == 0) continue;

      std::string text;
      bool allBold = true;
      bool allItalic = true;
      bool leading = true;
      bool restItalic = true;
      size_t leadChars = 0;
      for (uint16_t w = 0; w < block->wordCount(); ++w) {
        const bool bold = (block->wordStyle(w) & EpdFontFamily::BOLD) != 0;
        const bool italic = (block->wordStyle(w) & EpdFontFamily::ITALIC) != 0;
        allBold = allBold && bold;
        allItalic = allItalic && italic;
        if (w > 0) {
          const int previousEnd =
              block->wordXpos(w - 1) +
              renderer.getTextAdvanceX(fontId, block->wordText(w - 1), block->wordStyle(w - 1));
          if (block->wordXpos(w) - previousEnd >= space / 2) text += ' ';
        }
        text += block->wordText(w);
        if (leading && bold) {
          leadChars = text.size();
        } else {
          leading = false;
          restItalic = restItalic && italic;
        }
      }
      // Bold + italic is the internal marker of a section heading: shown as plain bold.
      const bool marker = allBold && allItalic;  // the internal marker of a real section heading
      if (marker) allItalic = false;
      if (!allBold && leadChars > 0 && leadChars < text.size() && text[leadChars - 1] == ':') {
        paras.push_back({text.substr(0, leadChars), true, false});
        paras.push_back({text.substr(leadChars), false, wordWebStyle_ && restItalic});
        continue;
      }
      // Only WordWeb marks its examples in italic; elsewhere italic text is never taken for an example.
      paras.push_back({text, allBold && (marker || looksLikeHeadingText(text)), wordWebStyle_ && allItalic});
    }
  }
  if (wordWebStyle_) restructureWordWeb(paras);
  for (const auto& p : paras) addParagraph(p.text, p.heading, p.italic);
}

void DictionaryDefinitionActivity::rowsFromPlainText(const std::string& text) {
  size_t start = 0;
  while (start <= text.size()) {
    size_t end = text.find('\n', start);
    if (end == std::string::npos) end = text.size();
    addParagraph(text.substr(start, end - start), false, false);
    if (end == text.size()) break;
    start = end + 1;
  }
}

// XDXF-style markup (Oxford, Cambridge): <k> headword, <tr> pronunciation, <abr> part of speech, <ex> example,
// <b>/<i> styling, <blockquote> and newlines separating paragraphs. Every other tag is dropped, its text kept.
void DictionaryDefinitionActivity::rowsFromXdxf(const std::string& src) {
  struct Seg {
    std::string text;
    bool bold;
    bool italic;
    bool abr;
  };
  std::string cur;
  std::vector<Seg> segs;
  bool curBold = true;
  bool curItalic = true;
  bool curText = false;
  bool curHasEx = false;
  bool curHasKref = false;      // the paragraph holds a linked phrase
  bool curOutsideKref = false;  // ... and some text outside the link
  int krefDepth = 0;
  bool inPhrases = false;     // the "Phrases" heading was already written for this run of lines
  int boldDepth = 0;
  int italicDepth = 0;
  int skipDepth = 0;
  bool inEx = false;
  bool inAbr = false;
  bool derivMode = false;     // inside "Derivatives:": each derived word becomes one row
  bool posRunActive = false;  // "preposition, conjunction & adverb": a run of part-of-speech tags is one heading
  std::string posRunText;
  std::string posRunSep;
  std::string abrText;

  const auto resetParagraph = [&]() {
    cur.clear();
    segs.clear();
    curBold = true;
    curItalic = true;
    curText = false;
    curHasEx = false;
    curHasKref = false;
    curOutsideKref = false;
  };
  const auto flushPosRun = [&]() {
    if (!posRunActive) return;
    posRunActive = false;
    const std::string heading = posRunText;
    posRunText.clear();
    posRunSep.clear();
    addParagraph(heading, true, false);
  };
  // One row per derived word: "hatable (also hateable) (adjective)", with its example in italic beneath.
  const auto emitDerivatives = [&]() -> bool {
    struct Entry {
      std::string word;
      std::string paren;
      std::string pos;
      std::string notes;
      std::string example;
    };
    const auto collapse = [](const std::string& x) {
      std::string o;
      bool space = false;
      for (const char ch : x) {
        if (isBlankByte(ch)) {
          space = !o.empty();
          continue;
        }
        if (space) o += ' ';
        space = false;
        o += ch;
      }
      return o;
    };
    std::vector<Entry> entries;
    int depth = 0;
    std::string capture;
    for (const auto& seg : segs) {
      if (seg.abr) {
        const std::string t = trimmed(seg.text);
        if (t.empty()) continue;
        if (depth > 0) {
          capture += " " + t + " ";
        } else if (!entries.empty()) {
          if (isTruePos(t)) {
            entries.back().pos = lowerAscii(t);
          } else {
            entries.back().notes += std::string(entries.back().notes.empty() ? "" : " ") + "[" + t + "]";
          }
        }
        continue;
      }
      if (depth == 0 && seg.bold && !seg.italic) {
        const std::string w = collapse(seg.text);
        if (!w.empty()) entries.push_back({w, std::string(), std::string(), std::string(), std::string()});
        continue;
      }
      if (depth == 0 && seg.italic) {
        const std::string t = collapse(seg.text);
        if (t.empty() || entries.empty()) continue;
        std::string& target = t[0] == '[' ? entries.back().notes : entries.back().example;
        target += std::string(target.empty() ? "" : " ") + t;
        continue;
      }
      for (const char ch : seg.text) {
        if (ch == '(') {
          if (depth == 0) capture.clear();
          ++depth;
        } else if (ch == ')') {
          if (depth > 0 && --depth == 0 && !entries.empty()) {
            const std::string p = collapse(capture);
            if (!p.empty()) {
              entries.back().paren += std::string(entries.back().paren.empty() ? "" : " ") + "(" + p + ")";
            }
            capture.clear();
          }
        } else if (depth > 0) {
          capture += ch;
        }
      }
    }
    if (entries.empty()) return false;
    for (const auto& e : entries) {
      std::string row = e.word;
      if (!e.paren.empty()) row += " " + e.paren;
      if (!e.pos.empty()) row += " (" + e.pos + ")";
      if (!e.notes.empty()) row += " " + e.notes;
      addParagraph(row, false, false);
      if (!e.example.empty()) addParagraph(e.example, false, true);
    }
    return true;
  };
  const auto emit = [&]() {
    flushPosRun();
    if (curText) {
      const std::string t = trimmed(cur);
      const bool isLabel = curBold && !t.empty() && t.back() == ':' && t.size() <= 40;
      if (isLabel) derivMode = lowerAscii(t) == "derivatives:";
      // A run of lines made only of linked phrases gets one "Phrases" heading (Oxford).
      const bool phraseLine = oxfordStyle_ && curHasKref && !curOutsideKref && !inEx;
      if (phraseLine && !inPhrases) addParagraph("Phrases", true, false);
      inPhrases = phraseLine;
      if (inEx) {
        // several examples can share one <ex>, separated by " | "
        size_t start = 0;
        while (true) {
          const size_t bar = t.find(" | ", start);
          addParagraph(t.substr(start, bar == std::string::npos ? std::string::npos : bar - start), false, true);
          if (bar == std::string::npos) break;
          start = bar + 3;
        }
      } else if (derivMode && !isLabel) {
        if (!emitDerivatives()) addParagraph(t, false, false);
      } else {
        addParagraph(t, curBold && t.size() <= 40, curHasEx);
      }
    }
    resetParagraph();
  };
  const auto appendText = [&](const std::string& s) {
    if (skipDepth > 0) return;
    if (inAbr) {
      abrText += s;
      return;
    }
    if (posRunActive) {
      if (isSeparatorOnly(s)) {
        posRunSep += s;
        return;
      }
      flushPosRun();
    }
    // a bold Roman numeral ("I.", "II") opens a big section of its own (Oxford)
    if (oxfordStyle_ && boldDepth > 0 && !curText && !romanLabel(s).empty()) {
      addParagraph(trimmed(s), true, false);
      return;
    }
    for (const char ch : s) {
      if (isBlankByte(ch)) continue;
      curText = true;
      if (krefDepth == 0 && ch != '-') curOutsideKref = true;
      if (boldDepth == 0) curBold = false;
      if (italicDepth == 0 && !inEx) curItalic = false;
    }
    segs.push_back({s, boldDepth > 0, italicDepth > 0 || inEx, false});
    cur += s;
  };

  const size_t n = src.size();
  size_t i = 0;
  while (i < n) {
    const char c = src[i];
    if (c == '<') {
      const size_t gt = src.find('>', i + 1);
      if (gt == std::string::npos) {
        appendText("<");
        ++i;
        continue;
      }
      const std::string tag = src.substr(i + 1, gt - i - 1);
      const bool closing = !tag.empty() && tag[0] == '/';
      const size_t ns = closing ? 1 : 0;
      size_t ne = ns;
      while (ne < tag.size() && std::isalnum(static_cast<unsigned char>(tag[ne]))) ++ne;
      const std::string name = lowerAscii(tag.substr(ns, ne - ns));
      const bool selfClose = !tag.empty() && tag.back() == '/';
      if (name.empty()) {  // "x < y": a literal '<'
        appendText("<");
        ++i;
        continue;
      }
      // grammar codes such as <E> or <A> are text, not markup
      if (!closing && name.size() == 1 && tag[ns] >= 'A' && tag[ns] <= 'Z' &&
          tag.find_first_not_of(' ', ne) == std::string::npos) {
        appendText(std::string("<") + tag[ns] + ">");
        i = gt + 1;
        continue;
      }
      i = gt + 1;
      if (name == "k" || name == "tr") {  // headword and pronunciation: not shown
        if (!selfClose) skipDepth = std::max(0, skipDepth + (closing ? -1 : 1));
        continue;
      }
      if (skipDepth > 0) continue;
      if (name == "blockquote" || name == "br" || name == "p" || name == "div" || name == "li" || name == "ol" ||
          name == "ul") {
        emit();
        continue;
      }
      if (name == "ex") {
        if (oxfordStyle_) {
          emit();
          inEx = !closing;
        } else if (!closing) {
          curHasEx = true;  // Cambridge: an example stays in its paragraph, with the "(= ...)" notes around it
        }
        continue;
      }
      if (name == "b") {
        boldDepth = std::max(0, boldDepth + (closing ? -1 : 1));
        continue;
      }
      if (name == "i") {
        italicDepth = std::max(0, italicDepth + (closing ? -1 : 1));
        continue;
      }
      if (name == "abr") {
        if (!closing) {
          inAbr = true;
          abrText.clear();
        } else {
          inAbr = false;
          const std::string t = trimmed(abrText);
          if (derivMode) {
            if (!t.empty()) {
              segs.push_back({t, false, false, true});
              curText = true;
              cur += " " + t + " ";
            }
          } else if (isTruePos(t) && (posRunActive || !curText)) {
            if (posRunActive) {
              posRunText += joinPosSeparator(posRunSep) + capitalizeWords(lowerAscii(t));
              posRunSep.clear();
            } else {
              emit();
              posRunActive = true;
              posRunText = capitalizeWords(lowerAscii(t));
              posRunSep.clear();
            }
          } else if (!t.empty()) {
            flushPosRun();
            const char last = cur.empty() ? ' ' : cur.back();
            const bool noSpace = isBlankByte(last) || last == '(' || last == '[';
            appendText(std::string(noSpace ? "" : " ") + t + " ");
          }
        }
        continue;
      }
      if (name == "kref") {
        krefDepth = std::max(0, krefDepth + (closing ? -1 : 1));
        if (!closing) curHasKref = true;
        if (closing && krefDepth == 0 && cambridgeStyle_ && curText) {
          // A list of links ("Idioms", "Phrasal Verbs", "Thesaurus+") becomes one row per link: a separator is left
          // after each link that is followed by another link or by the end of the paragraph.
          size_t k = i;
          for (int hop = 0; hop < 4; ++hop) {
            while (k < n && (src[k] == ' ' || src[k] == '\t')) ++k;
            if (k < n && src[k] == '<' && src.compare(k, 5, "<kref") != 0 && src.compare(k, 12, "</blockquote") != 0) {
              const size_t g = src.find('>', k);
              if (g == std::string::npos) break;
              k = g + 1;
              continue;
            }
            break;
          }
          if (k >= n || src[k] == '\n' || src.compare(k, 5, "<kref") == 0 || src.compare(k, 12, "</blockquote") == 0) {
            cur += '\x1F';
          }
        }
        continue;
      }
      continue;  // every other tag is ignored; its text stays
    }
    if (c == '&') {
      std::string decoded;
      const size_t used = decodeEntity(src, i, &decoded);
      if (used > 0) {
        appendText(decoded);
        i += used;
      } else {
        appendText("&");
        ++i;
      }
      continue;
    }
    if (c == '\n') {
      emit();
      ++i;
      continue;
    }
    size_t j = i;
    while (j < n && src[j] != '<' && src[j] != '&' && src[j] != '\n') ++j;
    appendText(src.substr(i, j - i));
    i = j;
  }
  emit();
}

// One paragraph of the definition, cleaned the same way for every dictionary.
void DictionaryDefinitionActivity::addParagraph(const std::string& raw, bool heading, bool italic) {
  // Items of a list of links arrive joined by a separator: each one is a paragraph of its own.
  if (raw.find('\x1F') != std::string::npos) {
    size_t start = 0;
    while (start <= raw.size()) {
      const size_t p = raw.find('\x1F', start);
      addParagraph(raw.substr(start, p == std::string::npos ? std::string::npos : p - start), heading, italic);
      if (p == std::string::npos) break;
      start = p + 1;
    }
    return;
  }
  std::string text = tidyParens(trimmed(tidyCodes(normalizeSpaces(raw))));
  if (text.empty() || !hasLetterOrDigit(text)) return;  // also drops an orphan "." or bullet
  // Oxford's big section numbers: "I", "II." -> "I)", "II)"
  if (oxfordStyle_) {
    const std::string roman = romanLabel(text);
    if (!roman.empty()) {
      pendingNote_.clear();
      wrapInto(roman + ")", std::string(), true, false, 0);
      return;
    }
  }
  // Cambridge's first line holds the pronunciation and the part of speech: keep the part of speech as a heading.
  if (cambridgeStyle_ && looksLikeCambridgeHeader(text)) {
    const std::string pos = leadingPartOfSpeech(text, headword);
    if (!pos.empty()) addParagraph(pos, true, false);
    return;
  }
  // Items glued together by a separator ("↑a ↑b", "x ▪ y ▪ z") become paragraphs of their own. This has to happen
  // before the characters the font cannot draw are removed, or the separator would vanish and the items would run
  // together into one row.
  static constexpr const char* SEPARATORS[] = {"\xE2\x86\x91", "\xE2\x96\xAA"};
  for (const char* sep : SEPARATORS) {
    if (text.find(sep) == std::string::npos) continue;
    const size_t sl = strlen(sep);
    size_t start = 0;
    while (start <= text.size()) {
      const size_t p = text.find(sep, start);
      addParagraph(text.substr(start, p == std::string::npos ? std::string::npos : p - start), heading, italic);
      if (p == std::string::npos) break;
      start = p + sl;
    }
    return;
  }
  if (isPronunciationLine(text)) return;
  text = stripUnsupportedGlyphs(text);  // scripts the UI font cannot draw would only fill the log with errors
  if (text.empty() || !hasLetterOrDigit(text)) return;

  int kind = 0;
  text = stripLeadingMarker(text, &kind);
  text = stripTrailingDash(text);
  const bool hadNumber = kind == 3 || pendingSense_ > 0;  // the dictionary gave this paragraph a sense number
  if (cambridgeStyle_ && kind == 3 && (text.empty() || !hasLetterOrDigit(text))) {
    pendingSense_ = 1;  // a lone "2.": the next heading is a numbered sense of its own
    return;
  }
  pendingSense_ = 0;
  if (cambridgeStyle_ && kind == 3 && !heading) inCollocations_ = false;  // a new numbered sense ends a Collocations block
  if (text.empty() || !hasLetterOrDigit(text)) return;  // a lone "1." or "-"
  if (kind == 1 && cambridgeStyle_) italic = true;  // a round bullet marks an example (Cambridge only)
  if (lowerAscii(text) == lowerAscii(headword)) return;  // the word is already in the header

  // A label followed by text: "Thesaurus+: ...", "Noun: romance", "See also: ..."
  const size_t colon = text.find(':');
  if (colon != std::string::npos && colon > 0 && colon <= 24) {
    const std::string head = trimmed(text.substr(0, colon));
    const std::string rest = trimmed(text.substr(colon + 1));
    const std::string lowHead = lowerAscii(head);
    if (lowHead == "thesaurus+" || lowHead == "thesaurus") {
      addParagraph("Thesaurus+", true, false);
      if (!rest.empty()) addParagraph(rest, false, false);
      return;
    }
    if (isTruePos(head) && (heading || rest.empty() || rest.find(' ') == std::string::npos ||
                            lowerAscii(rest) == lowerAscii(headword))) {
      text = capitalizeWords(lowerAscii(head));
      heading = true;
    } else if (!heading && isSectionLabel(lowHead)) {
      addParagraph(upperFirst(head), true, false);
      if (!rest.empty()) addParagraph(rest, false, false);
      return;
    }
  }
  // Cambridge: a line that is only a part of speech ("verb transitive", "modal verb", "exclamation") is a section
  // heading at the margin, like "Adverb". Only the "+ Verbs" groups under Collocations are indented.
  if (cambridgeStyle_ && !heading && isPosPhrase(lowerAscii(collapseSpaces(text)))) {
    text = normalizePosName(text);
    heading = true;
  }
  if (!heading && isTruePos(text)) {  // a line that is only a part of speech
    text = capitalizeWords(lowerAscii(text));
    heading = true;
  }
  // A known label such as "Origin:" is a heading too. Any other line that ends in a colon ("A place in Wales:")
  // is an ordinary definition that introduces the lines below it.
  if (!heading && text.size() > 2 && text.back() == ':' &&
      isSectionLabel(lowerAscii(trimmed(text.substr(0, text.size() - 1))))) {
    heading = true;
  }
  if (!heading && cambridgeStyle_ && isAllCapsWord(text)) heading = true;  // guidewords such as "FLAVOUR"
  if (isDictionaryHeading(text.c_str())) heading = true;
  if (heading) {
    while (!text.empty() && text.back() == ':') text.pop_back();
    if (text.empty()) return;
    int numKind = 0;  // Cambridge: 1 = numbered sense heading, 2 = lettered under the previous one
    if (cambridgeStyle_) {
      const std::string low = labelOf(text);
      if (isAllCapsWord(text)) {  // a guideword ("FLAVOUR"): a numbered section of its part of speech
        text = capitalizeWords(lowerAscii(text));
        numKind = 1;
        inCollocations_ = false;
      } else if (isTruePos(low) || isPosPhrase(low)) {
        inCollocations_ = false;
      } else if (isCambridgeBlockLabel(low)) {
        inCollocations_ = true;
      } else if (!isCambridgeLabel(low) && !inCollocations_) {
        numKind = hadNumber ? 1 : 2;  // a phrase that opens a sense: numbered like a guideword, or lettered under it
      }
    }
    if (!rows_.empty() && rows_.back().bold && rows_.back().indent == 0 && !rows_.back().cont &&
        lowerAscii(rows_.back().text) == lowerAscii(text)) {
      return;  // the same heading twice in a row
    }
    pendingNote_.clear();
    wrapInto(text, std::string(), true, false, 0, true, numKind);
    return;
  }

  // Cambridge sub-labels ("Verbs", "Adjectives", "verb transitive"): bold, but indented like any other entry.
  {
    const std::string low = lowerAscii(text);
    if (cambridgeStyle_ && isPosPhrase(low)) {  // "verb transitive": a part-of-speech note, bold and one step in
      pendingNote_.clear();
      guideNumber_ = 0;
      inCollocations_ = false;
      wrapInto(upperFirst(text), std::string(), true, false, kSubIndent, true);
      return;
    }
    if (cambridgeStyle_ && isSubLabel(low)) {  // collocation groups: "+ Verbs", "+ Nouns", with no leader line
      pendingNote_.clear();
      wrapInto("+ " + upperFirst(text), std::string(), true, false, kSubIndent, false);
      return;
    }
  }

  // "[with obj]" on its own: kept to open the next definition
  if (text.front() == '[' && text.back() == ']' && text.find(']') == text.size() - 1 && text.size() <= 40) {
    pendingNote_ = text;
    return;
  }
  if (!italic && wordWebStyle_ && isQuotedExample(text)) italic = true;
  if (!italic && !pendingNote_.empty()) {
    text = pendingNote_ + " " + text;
    pendingNote_.clear();
  }

  // Oxford: every "[...]" note is drawn bold (bold italic inside an example). Other dictionaries keep them plain.
  // A definition that ends in a colon only introduces the lines below it: it ends in a full stop instead.
  if (!oxfordStyle_ && !cambridgeStyle_ && !italic && !text.empty() && text.back() == ':') text.back() = '.';

  const std::string mask =
      oxfordStyle_ ? bracketMask(text) : (wordWebStyle_ ? leadingBracketMask(text) : std::string());
  wrapInto(text, mask, false, italic, kIndent);
}

// Greedy word wrap at an exact pixel width. The first line of an entry is narrower (it is indented); every row
// of a paragraph except the last is justified. The bold mask is cut together with the text.
void DictionaryDefinitionActivity::wrapInto(const std::string& text, const std::string& mask, const bool bold,
                                            const bool italic, const int indentPx, const bool leader, const int numKind) {
  const int fullWidth = std::max(80, labelWidthPx() - kWrapSafety);
  const int firstWidth = indentPx > 0 ? std::max(80, fullWidth - indentPx) : fullWidth;

  std::vector<std::string> lines;
  std::vector<std::string> masks;
  std::string line;
  std::string lineMask;
  const auto widthNow = [&]() { return lines.empty() ? firstWidth : fullWidth; };
  const auto flush = [&] {
    if (!line.empty()) {
      lines.push_back(line);
      masks.push_back(lineMask);
    }
    line.clear();
    lineMask.clear();
  };

  size_t i = 0;
  const size_t n = text.size();
  while (i < n) {
    while (i < n && isBlankByte(text[i])) i++;
    if (i >= n) break;
    const size_t start = i;
    while (i < n && !isBlankByte(text[i])) i++;
    std::string token = text.substr(start, i - start);
    std::string tokMask = mask.empty() ? std::string(token.size(), '0') : mask.substr(start, i - start);

    // A word wider than a whole line is split at the widest fitting UTF-8 boundary.
    while (token.size() > 1 && measure(token, bold, italic) > (line.empty() ? widthNow() : fullWidth)) {
      flush();
      const int w = widthNow();
      size_t cut = 0;
      for (size_t f = 1; f <= token.size(); ++f) {
        if (f == token.size() || (static_cast<unsigned char>(token[f]) & 0xC0) != 0x80) {
          if (measure(token.substr(0, f), bold, italic) > w) break;
          cut = f;
        }
      }
      if (cut == 0) {
        cut = 1;
        while (cut < token.size() && (static_cast<unsigned char>(token[cut]) & 0xC0) == 0x80) cut++;
      }
      lines.push_back(token.substr(0, cut));
      masks.push_back(tokMask.substr(0, cut));
      token.erase(0, cut);
      tokMask.erase(0, cut);
    }
    if (token.empty()) continue;

    const std::string candidate = line.empty() ? token : line + ' ' + token;
    if (line.empty() || measure(candidate, bold, italic) <= widthNow()) {
      if (!line.empty()) lineMask += '0';
      line = candidate;
      lineMask += tokMask;
    } else {
      flush();
      line = token;
      lineMask = tokMask;
    }
  }
  flush();

  for (size_t k = 0; k < lines.size(); ++k) {
    const bool last = k + 1 == lines.size();
    rows_.push_back({lines[k], masks[k].find('1') != std::string::npos ? masks[k] : std::string(), bold, italic,
                     !bold && (kJustifyLastLine || !last), k == 0 ? indentPx : 0, k > 0, leader,
                     static_cast<uint8_t>(k == 0 ? numKind : 0), k == 0 && bold && lines[k] == "Thesaurus+"});
  }
}

// A heading with nothing under it (an empty "Encyclopedia", a "Noun" with no definitions) is removed, whatever
// the reason it is empty. A part of speech may hold section headings; any other heading may not.
void DictionaryDefinitionActivity::pruneEmptySections() {
  const auto isHead = [this](const size_t k) { return rows_[k].bold && rows_[k].indent == 0 && !rows_[k].cont; };
  for (bool again = true; again;) {
    again = false;
    for (size_t k = 0; k < rows_.size(); ++k) {
      if (!isHead(k)) continue;
      size_t next = k + 1;
      while (next < rows_.size() && rows_[next].cont) ++next;
      const bool empty = next >= rows_.size() ||
                         (isHead(next) && headingLevel(rows_[next].text) <= headingLevel(rows_[k].text));
      if (empty) {
        rows_.erase(rows_.begin() + static_cast<std::ptrdiff_t>(k), rows_.begin() + static_cast<std::ptrdiff_t>(next));
        again = true;
        break;
      }
    }
  }
}

// Cambridge: sense headings are numbered once the empty sections are gone, so the numbers never skip. Every
// section heading (a guideword, or a phrase that opens a sense) takes the next number; a new part of speech
// starts again at 1).
void DictionaryDefinitionActivity::numberSenseHeadings() {
  int number = 0;
  for (auto& r : rows_) {
    if (r.cont || !r.bold) continue;
    if (r.numKind == 0) {
      const std::string low = labelOf(r.text);
      if (isTruePos(low) || isPosPhrase(low)) number = 0;
      continue;
    }
    r.text = std::to_string(++number) + ") " + r.text;
  }
}

void DictionaryDefinitionActivity::buildScreen(UiScreen& screen) {
  uiTarget.setFont(fui::GfxRendererTarget::FONT_SMALL, UI_10_FONT_ID);
  uiTarget.setFont(fui::GfxRendererTarget::FONT_BODY, UI_10_FONT_ID);
  refreshSharedUiThemeTokens(uiTarget);

  const auto& metrics = UITheme::getInstance().getMetrics();
  screen.setContentMargin(fui::Insets{static_cast<int16_t>(metrics.topPadding + metrics.headerHeight - 3), 0,
                                      static_cast<int16_t>(metrics.buttonHintsHeight), 0});
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing > 6 ? metrics.verticalSpacing - 6 : 0));

  fui::ListProps props;
  props.items = items_.data();
  props.count = static_cast<uint16_t>(items_.size());
  props.action = ACTION_ROW;
  props.valueInset = 3;
  props.rowHeight = static_cast<int16_t>(metrics.listRowHeight);
  props.rowGap = 0;
  props.scrollIndicator = false;  // the Prev/Next hints already show when there is more
  props.labelText = screen.theme().smallText;
  props.labelText.maxLines = 1;
  props.labelText.lineGap = 6;
  syncListViewport(screen, props);
  props.selectedIndex = -1;  // view-only: no highlight
  screen.list(props);
}

void DictionaryDefinitionActivity::activateIndex(const int) { app.clearTapFlash(); }

bool DictionaryDefinitionActivity::canScrollUp() { return activeNav().top > 0; }

bool DictionaryDefinitionActivity::canScrollDown() {
  return static_cast<int>(activeNav().top) + pageRowsEstimate() < listCount();
}

// One whole screen at a time: the rows flow on exactly where the last screen ended, so nothing repeats.
void DictionaryDefinitionActivity::scrollPages(const int direction) {
  auto& n = activeNav();
  const int cap = pageRowsEstimate();
  const int count = listCount();
  const int newTop = static_cast<int>(n.top) + direction * cap;
  if (newTop < 0 || newTop >= count) return;
  {
    RenderLock lock(*this);
    n.top = newTop;
    n.selected = std::clamp(newTop, 0, std::max(0, count - 1));
  }
  requestUpdate();
}

void DictionaryDefinitionActivity::navigateButtons() {
  navigator_.onNext([this] { scrollPages(1); });
  navigator_.onPrevious([this] { scrollPages(-1); });
}

void DictionaryDefinitionActivity::drawFooter() {
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", canScrollUp() ? "Prev" : "", canScrollDown() ? "Next" : "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}
