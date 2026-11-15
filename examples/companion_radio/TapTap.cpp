#ifdef TAPTAP_FEATURES
#include "TapTap.h"
#include <string.h>
#include <ctype.h>
#include <stdio.h>

static const char* const DEFAULT_CANNED[TAPTAP_MAX_CANNED] = {
  "All good",
  "Stop and wait",
  "Need assistance",
  "Emergency, send help",
  "Found water",
  "Returning to camp",
  "Running behind",
  "Found the point",
};

void TapTapSettings::setDefaults() {
  memset(this, 0, sizeof(*this));
  magic = MAGIC;
  version = VERSION;
  buz_mode = TAP_BUZ_RTTTL;
  canned_count = TAPTAP_MAX_CANNED;
  for (int i = 0; i < TAPTAP_MAX_CANNED; i++) {
    strncpy(canned[i], DEFAULT_CANNED[i], TAPTAP_CANNED_LEN - 1);
  }
  target.type = TAP_TARGET_CHANNEL;
  target.channel_idx = 0;   // Public
  repeat_alert = 0;
  repeat_gap_secs = 5;
  cw_wpm = TAPTAP_CW_WPM_DEFAULT;
  slot_beep_ms = TAPTAP_SLOT_MS_DEFAULT;
  slot_flash_ms = TAPTAP_SLOT_MS_DEFAULT;
}

void TapTapSettings::sanitise() {
  if (cw_wpm < TAPTAP_CW_WPM_MIN || cw_wpm > TAPTAP_CW_WPM_MAX) cw_wpm = TAPTAP_CW_WPM_DEFAULT;
  if (slot_beep_ms < TAPTAP_SLOT_MS_MIN || slot_beep_ms > TAPTAP_SLOT_MS_MAX) slot_beep_ms = TAPTAP_SLOT_MS_DEFAULT;
  if (slot_flash_ms < TAPTAP_SLOT_MS_MIN || slot_flash_ms > TAPTAP_SLOT_MS_MAX) slot_flash_ms = TAPTAP_SLOT_MS_DEFAULT;
  if (repeat_gap_secs == 0) repeat_gap_secs = 5;
  for (int i = 0; i < TAPTAP_MAX_CANNED; i++) canned[i][TAPTAP_CANNED_LEN - 1] = 0;
  ringtone[TAPTAP_RINGTONE_LEN - 1] = 0;
}

bool TapTapSettings::isValid() const {
  return magic == MAGIC && version == VERSION && buz_mode <= TAP_BUZ_OFF &&
         canned_count <= TAPTAP_MAX_CANNED && target.type <= TAP_TARGET_CONTACT;
}

namespace taptap {

const char* buzModeName(uint8_t mode) {
  switch (mode) {
    case TAP_BUZ_RTTTL: return "rtttl";
    case TAP_BUZ_CW:    return "cw";
    case TAP_BUZ_OFF:   return "off";
    default:            return "unknown";
  }
}

bool parseBuzMode(const char* s, uint8_t& mode) {
  if (strcasecmp(s, "rtttl") == 0 || strcasecmp(s, "tune") == 0) { mode = TAP_BUZ_RTTTL; return true; }
  if (strcasecmp(s, "cw") == 0 || strcasecmp(s, "morse") == 0)  { mode = TAP_BUZ_CW; return true; }
  if (strcasecmp(s, "off") == 0 || strcasecmp(s, "none") == 0)  { mode = TAP_BUZ_OFF; return true; }
  return false;
}

const char* stripSenderPrefix(const char* text) {
  const char* sep = strstr(text, ": ");
  if (sep && (sep - text) < 32) return sep + 2;
  return text;
}

uint32_t fnv1a(const char* s) {
  uint32_t h = 0x811c9dc5;
  while (*s) {
    h ^= (uint8_t)*s++;
    h *= 0x01000193;
  }
  return h;
}

// International Morse, '0'..'Z' (0x30..0x5A). nullptr = not encodable.
static const char* const MORSE[] = {
  "-----", ".----", "..---", "...--", "....-", ".....", "-....", "--...", "---..", "----.",   // 0-9
  nullptr, nullptr, nullptr, nullptr, nullptr, "..--..", nullptr,                             // : ; < = > ? @
  ".-", "-...", "-.-.", "-..", ".", "..-.", "--.", "....", "..", ".---", "-.-", ".-..", "--",  // A-M
  "-.", "---", ".--.", "--.-", ".-.", "...", "-", "..-", "...-", ".--", "-..-", "-.--", "--..", // N-Z
};

static const char* morseFor(char c) {
  c = toupper((unsigned char)c);
  if (c == '.') return ".-.-.-";
  if (c == ',') return "--..--";
  if (c == '/') return "-..-.";
  if (c < '0' || c > 'Z') return nullptr;
  return MORSE[c - '0'];
}

char decodeMorse(const char* pattern) {
  if (pattern == nullptr || *pattern == 0) return 0;
  static const char EXTRA[] = ".,/?";
  for (const char* x = EXTRA; *x; x++) {
    const char* code = morseFor(*x);
    if (code && strcmp(code, pattern) == 0) return *x;
  }
  for (char c = '0'; c <= 'Z'; c++) {
    const char* code = MORSE[c - '0'];
    if (code && strcmp(code, pattern) == 0) return c;
  }
  return 0;
}

static bool append(char* out, size_t out_len, int& pos, const char* s) {
  size_t n = strlen(s);
  if (pos + n + 1 > out_len) return false;
  memcpy(&out[pos], s, n);
  pos += n;
  out[pos] = 0;
  return true;
}

// Timing: one Morse 'unit' = 1200/wpm ms and is an 8th note, so b = 25 * wpm (19 WPM -> b=475, 63ms).
// dit = 1 unit, dah = 3 units ("4a." = dotted quarter), element gap = 1,
// letter gap = 3 ("4p."), word gap = 7 ("2p.,p").
int buildCwRtttl(const char* text, char* out, size_t out_len, int wpm) {
  int pos = 0;
  out[0] = 0;
  if (wpm < TAPTAP_CW_WPM_MIN || wpm > TAPTAP_CW_WPM_MAX) wpm = TAPTAP_CW_WPM_DEFAULT;
  char header[32];
  snprintf(header, sizeof(header), "CW:d=8,o=6,b=%d:", wpm * 25);
  if (!append(out, out_len, pos, header)) return 0;
  const size_t header_len = pos;

  bool pending_word_gap = false;
  bool first = true;
  for (const char* p = text; *p; p++) {
    if (*p == ' ') { pending_word_gap = !first; continue; }
    const char* code = morseFor(*p);
    if (code == nullptr) continue;

    int mark = pos;   // so we can roll back a partially written letter
    bool ok = true;
    if (!first) ok = append(out, out_len, pos, pending_word_gap ? ",2p.,p," : ",4p.,");
    for (const char* e = code; ok && *e; e++) {
      ok = append(out, out_len, pos, *e == '.' ? "a" : "4a.");
      if (ok && e[1]) ok = append(out, out_len, pos, ",p,");
    }
    if (!ok) { pos = mark; out[pos] = 0; break; }   // out of space: stop at a letter boundary
    first = false;
    pending_word_gap = false;
  }
  if ((size_t)pos == header_len) {   // nothing encodable
    out[0] = 0;
    return 0;
  }
  return pos;
}

static void trimCopy(char* dest, size_t dest_len, const char* start, const char* end) {
  while (start < end && isspace((unsigned char)*start)) start++;
  while (end > start && isspace((unsigned char)end[-1])) end--;
  size_t n = end - start;
  if (n > dest_len - 1) n = dest_len - 1;
  memcpy(dest, start, n);
  dest[n] = 0;
}

int parseCannedList(const char* s, TapTapSettings& st) {
  char tmp[TAPTAP_MAX_CANNED][TAPTAP_CANNED_LEN];
  int count = 0;
  const char* start = s;
  while (count < TAPTAP_MAX_CANNED) {
    const char* bar = strchr(start, '|');
    const char* end = bar ? bar : start + strlen(start);
    trimCopy(tmp[count], TAPTAP_CANNED_LEN, start, end);
    if (tmp[count][0]) count++;
    if (!bar) break;
    start = bar + 1;
  }
  if (count == 0) return 0;
  memset(st.canned, 0, sizeof(st.canned));
  memcpy(st.canned, tmp, sizeof(tmp[0]) * count);
  st.canned_count = count;
  return count;
}

bool normaliseRtttl(const char* in, char* out, size_t out_len, bool* truncated) {
  while (*in == ' ') in++;
  const char* c1 = strchr(in, ':');
  if (c1 == nullptr) return false;
  const char* c2 = strchr(c1 + 1, ':');
  const char* prefix = "";
  if (c2 == nullptr) {
    // header-less form "d=4,o=5,b=120:notes" -- needs a name section
    if (strchr(in, '=') == nullptr || strchr(in, '=') > c1) return false;
    prefix = "Ring:";
  } else if (strchr(c1, '=') == nullptr || strchr(c1, '=') > c2) {
    return false;   // second section isn't a d=/o=/b= header
  }
  size_t plen = strlen(prefix);
  size_t ilen = strlen(in);
  if (truncated) *truncated = (plen + ilen + 1 > out_len);
  snprintf(out, out_len, "%s%s", prefix, in);
  // never leave a dangling partial note: cut back to the last ','
  if (truncated && *truncated) {
    char* last = strrchr(out, ',');
    char* hdr_end = strrchr(out, ':');
    if (last && hdr_end && last > hdr_end) *last = 0;
  }
  return true;
}

int matchCanned(const char* text, const TapTapSettings& st) {
  const char* start = text;
  const char* end = text + strlen(text);
  while (start < end && isspace((unsigned char)*start)) start++;
  while (end > start && isspace((unsigned char)end[-1])) end--;
  size_t n = end - start;
  if (n == 0) return -1;
  for (int i = 0; i < st.canned_count; i++) {
    if (strlen(st.canned[i]) == n && strncasecmp(st.canned[i], start, n) == 0) return i;
  }
  return -1;
}

static void buildSlot(int idx, char* out, size_t out_len, int ms, const char* one, const char* zero) {
  // quarter note = ms (b = 60000 / ms), eighth = ms / 2
  if (ms < TAPTAP_SLOT_MS_MIN || ms > TAPTAP_SLOT_MS_MAX) ms = TAPTAP_SLOT_MS_DEFAULT;
  int pos = snprintf(out, out_len, "TapSel:d=4,o=5,b=%d:", 60000 / ms);
  for (int bit = 2; bit >= 0 && pos < (int)out_len; bit--) {
    pos += snprintf(&out[pos], out_len - pos, "%s%s", ((idx >> bit) & 1) ? one : zero, bit ? ",8p," : "");
  }
}

void buildSlotBeeps(int idx, char* out, size_t out_len, int beep_ms) {
  buildSlot(idx, out, out_len, beep_ms, "4a6", "4f5");    // same length, high / low
}

void buildSlotFlashes(int idx, char* out, size_t out_len, int flash_ms) {
  buildSlot(idx, out, out_len, flash_ms, "4a6", "8a6");   // dash / dot (pitch unused: LED only)
}

}  // namespace taptap

#endif  // TAPTAP_FEATURES
