#ifdef TAPTAP_FEATURES
#include "TapTap.h"
#include <string.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>

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
  buz_mode = TAP_BUZ_CWMATCH;
  led_flags = TAP_LED_NO_STANDBY;   // LED only lights for tones/alerts
  canned_count = TAPTAP_MAX_CANNED;
  for (int i = 0; i < TAPTAP_MAX_CANNED; i++) {
    strncpy(canned[i], DEFAULT_CANNED[i], TAPTAP_CANNED_LEN - 1);
  }
  target.type = TAP_TARGET_CHANNEL;
  target.channel_idx = 0;   // Public
  repeat_alert = 1;
  repeat_gap_secs = 5;
  cw_wpm = TAPTAP_CW_WPM_DEFAULT;
  slot_beep_ms = TAPTAP_SLOT_BEEP_MS_DEFAULT;
  slot_flash_ms = TAPTAP_SLOT_MS_DEFAULT;
}

void TapTapSettings::sanitise() {
  if (cw_wpm < TAPTAP_CW_WPM_MIN || cw_wpm > TAPTAP_CW_WPM_MAX) cw_wpm = TAPTAP_CW_WPM_DEFAULT;
  if (slot_beep_ms < TAPTAP_SLOT_MS_MIN || slot_beep_ms > TAPTAP_SLOT_MS_MAX) slot_beep_ms = TAPTAP_SLOT_BEEP_MS_DEFAULT;
  if (slot_flash_ms < TAPTAP_SLOT_MS_MIN || slot_flash_ms > TAPTAP_SLOT_MS_MAX) slot_flash_ms = TAPTAP_SLOT_MS_DEFAULT;
  if (repeat_gap_secs == 0) repeat_gap_secs = 5;
  for (int i = 0; i < TAPTAP_MAX_CANNED; i++) canned[i][TAPTAP_CANNED_LEN - 1] = 0;
  ringtone[TAPTAP_RINGTONE_LEN - 1] = 0;
}

void TapTapSettings::resetKeepMessages() {
  char keep_canned[TAPTAP_MAX_CANNED][TAPTAP_CANNED_LEN];
  uint8_t keep_count = canned_count;
  TapTarget keep_target = target;
  memcpy(keep_canned, canned, sizeof(canned));
  setDefaults();
  memcpy(canned, keep_canned, sizeof(canned));
  canned_count = keep_count;
  target = keep_target;
}

bool TapTapSettings::isValid() const {
  return magic == MAGIC && version == VERSION && buz_mode <= TAP_BUZ_RTTTLMATCH &&
         canned_count <= TAPTAP_MAX_CANNED && target.type <= TAP_TARGET_CONTACT;
}

namespace taptap {

const char* buzModeName(uint8_t mode) {
  switch (mode) {
    case TAP_BUZ_RTTTL:   return "rtttl";
    case TAP_BUZ_CWMATCH: return "cwmatch";
    case TAP_BUZ_CW:      return "cw";
    case TAP_BUZ_RTTTLMATCH: return "rtttlmatch";
    case TAP_BUZ_OFF:     return "off";
    default:            return "unknown";
  }
}

bool parseBuzMode(const char* s, uint8_t& mode) {
  while (*s == ' ') s++;
  if (strcasecmp(s, "rtttl") == 0 || strcasecmp(s, "tune") == 0) { mode = TAP_BUZ_RTTTL; return true; }
  if (strcasecmp(s, "rtttlmatch") == 0 || strcasecmp(s, "tunematch") == 0) { mode = TAP_BUZ_RTTTLMATCH; return true; }
  if (strcasecmp(s, "cwmatch") == 0 || strcasecmp(s, "match") == 0) { mode = TAP_BUZ_CWMATCH; return true; }
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

static void buildSlot(int idx, char* out, size_t out_len, int ms, int def_ms, const char* one, const char* zero) {
  // quarter note = ms (b = 60000 / ms), eighth = ms / 2
  if (ms < TAPTAP_SLOT_MS_MIN || ms > TAPTAP_SLOT_MS_MAX) ms = def_ms;
  int pos = snprintf(out, out_len, "TapSel:d=4,o=5,b=%d:", 60000 / ms);
  for (int bit = 2; bit >= 0 && pos < (int)out_len; bit--) {
    pos += snprintf(&out[pos], out_len - pos, "%s%s", ((idx >> bit) & 1) ? one : zero, bit ? ",8p," : "");
  }
}

void buildSlotBeeps(int idx, char* out, size_t out_len, int beep_ms) {
  buildSlot(idx, out, out_len, beep_ms, TAPTAP_SLOT_BEEP_MS_DEFAULT, "4a6", "4f5");    // same length, high / low
}

void buildSlotFlashes(int idx, char* out, size_t out_len, int flash_ms) {
  buildSlot(idx, out, out_len, flash_ms, TAPTAP_SLOT_MS_DEFAULT, "4a6", "8a6");   // dash / dot (pitch unused: LED only)
}

void escapeValue(const char* in, char* out, size_t out_len) {
  size_t o = 0;
  for (; *in && o + 1 < out_len; in++) {
    if (*in == ';' || *in == '\\') {
      if (o + 2 >= out_len) break;
      out[o++] = '\\';
    }
    out[o++] = *in;
  }
  out[o] = 0;
}

// appends "k=v" to the current line, starting a new "/import " line if it won't fit
static void addPair(char lines[][TAPTAP_EXPORT_LINE], int max_lines, int& n, const char* pair) {
  static const char PREFIX[] = "/import ";
  size_t plen = strlen(pair);
  if (n > 0) {
    size_t cur = strlen(lines[n - 1]);
    if (cur + 1 + plen < TAPTAP_EXPORT_LINE) {
      lines[n - 1][cur] = ';';
      memcpy(&lines[n - 1][cur + 1], pair, plen + 1);
      return;
    }
  }
  if (n >= max_lines) return;
  snprintf(lines[n], TAPTAP_EXPORT_LINE, "%s%s", PREFIX, pair);
  n++;
}

// adds key=value, splitting long values into key=<part>;key+=<part>... (each part escaped separately)
static void addChunked(char lines[][TAPTAP_EXPORT_LINE], int max_lines, int& n, const char* key, const char* value) {
  const size_t CHUNK = 50;   // raw chars per part: escaped <= 100, so a part always fits a line
  char pair[TAPTAP_EXPORT_LINE * 2];
  char raw[CHUNK + 1];
  char esc[CHUNK * 2 + 1];
  bool first = true;
  do {
    size_t len = strlen(value);
    if (len > CHUNK) len = CHUNK;
    memcpy(raw, value, len);
    raw[len] = 0;
    escapeValue(raw, esc, sizeof(esc));
    snprintf(pair, sizeof(pair), "%s%s%s", key, first ? "=" : "+=", esc);
    addPair(lines, max_lines, n, pair);
    value += len;
    first = false;
  } while (*value);
}

int exportSettings(const TapTapSettings& st, const char* tap_spec, char lines[][TAPTAP_EXPORT_LINE], int max_lines) {
  int n = 0;
  char pair[TAPTAP_EXPORT_LINE];

  snprintf(pair, sizeof(pair), "buz=%s", buzModeName(st.buz_mode));                 addPair(lines, max_lines, n, pair);
  snprintf(pair, sizeof(pair), "rep=%s", st.repeat_alert ? "on" : "off");           addPair(lines, max_lines, n, pair);
  snprintf(pair, sizeof(pair), "gap=%u", st.repeat_gap_secs);                       addPair(lines, max_lines, n, pair);
  snprintf(pair, sizeof(pair), "cw=%u", st.cw_wpm);                                 addPair(lines, max_lines, n, pair);
  snprintf(pair, sizeof(pair), "beep=%u", st.slot_beep_ms);                         addPair(lines, max_lines, n, pair);
  snprintf(pair, sizeof(pair), "flash=%u", st.slot_flash_ms);                       addPair(lines, max_lines, n, pair);
  snprintf(pair, sizeof(pair), "led=%s", (st.led_flags & TAP_LED_NO_STANDBY) ? "off" : "on"); addPair(lines, max_lines, n, pair);
  if (tap_spec && *tap_spec) {   // never split: "ch:<31 chars>" escaped or "dev:<64 hex>" always fits a line
    char esc[80];
    char tp[96];
    escapeValue(tap_spec, esc, sizeof(esc));
    snprintf(tp, sizeof(tp), "tap=%s", esc);
    addPair(lines, max_lines, n, tp);
  }
  addChunked(lines, max_lines, n, "rt", st.ringtone);   // "rt=" alone = default ringtone

  snprintf(pair, sizeof(pair), "msgs=%u", st.canned_count);
  addPair(lines, max_lines, n, pair);
  for (int k = 0; k < st.canned_count; k++) {
    char key[8];
    snprintf(key, sizeof(key), "m%d", k + 1);
    addChunked(lines, max_lines, n, key, st.canned[k]);
  }
  return n;
}

static void setError(ImportResult& r, const char* key) {
  if (r.errors++ == 0) snprintf(r.first_error, sizeof(r.first_error), "%s", key);
}

static bool parseOnOff(const char* v, uint8_t& out) {
  if (strcasecmp(v, "on") == 0 || strcmp(v, "1") == 0) { out = 1; return true; }
  if (strcasecmp(v, "off") == 0 || strcmp(v, "0") == 0) { out = 0; return true; }
  return false;
}

static bool parseRange(const char* v, int lo, int hi, int& out) {
  if (*v == 0) return false;
  char* end;
  long x = strtol(v, &end, 10);
  if (*end != 0 || x < lo || x > hi) return false;
  out = (int)x;
  return true;
}

ImportResult importSettings(const char* text, TapTapSettings& st,
                            bool (*tap_handler)(const char* value, void* ctx), void* ctx) {
  ImportResult res;
  const char* p = text;
  while (*p) {
    while (*p == ' ' || *p == ';') p++;
    if (*p == 0) break;

    char key[12];
    size_t kl = 0;
    while (*p && *p != '=' && *p != ';' && kl < sizeof(key) - 1) key[kl++] = *p++;
    key[kl] = 0;
    bool append = kl > 0 && key[kl - 1] == '+';
    if (append) key[--kl] = 0;

    char val[TAPTAP_RINGTONE_LEN];
    size_t vl = 0;
    bool has_eq = (*p == '=');
    if (has_eq) {
      p++;
      while (*p && *p != ';') {
        if (*p == '\\' && p[1]) p++;
        if (vl < sizeof(val) - 1) val[vl++] = *p;
        p++;
      }
    } else {
      while (*p && *p != ';') p++;   // skip junk
    }
    val[vl] = 0;
    if (!has_eq || kl == 0) { setError(res, kl ? key : "?"); continue; }

    bool ok = true;
    int x;
    if (strcmp(key, "buz") == 0)        ok = parseBuzMode(val, st.buz_mode);
    else if (strcmp(key, "rep") == 0)   ok = parseOnOff(val, st.repeat_alert);
    else if (strcmp(key, "gap") == 0)   { ok = parseRange(val, 1, 255, x); if (ok) st.repeat_gap_secs = x; }
    else if (strcmp(key, "cw") == 0)    { ok = parseRange(val, TAPTAP_CW_WPM_MIN, TAPTAP_CW_WPM_MAX, x); if (ok) st.cw_wpm = x; }
    else if (strcmp(key, "beep") == 0)  { ok = parseRange(val, TAPTAP_SLOT_MS_MIN, TAPTAP_SLOT_MS_MAX, x); if (ok) st.slot_beep_ms = x; }
    else if (strcmp(key, "flash") == 0) { ok = parseRange(val, TAPTAP_SLOT_MS_MIN, TAPTAP_SLOT_MS_MAX, x); if (ok) st.slot_flash_ms = x; }
    else if (strcmp(key, "led") == 0) {
      uint8_t on;
      ok = parseOnOff(val, on);
      if (ok) { if (on) st.led_flags &= ~TAP_LED_NO_STANDBY; else st.led_flags |= TAP_LED_NO_STANDBY; }
    }
    else if (strcmp(key, "rt") == 0) {
      if (append) {
        size_t cur = strlen(st.ringtone);
        ok = cur + vl < sizeof(st.ringtone);
        if (ok) memcpy(&st.ringtone[cur], val, vl + 1);
      } else if (vl == 0) {
        st.ringtone[0] = 0;
      } else {
        ok = vl < sizeof(st.ringtone);
        if (ok) memcpy(st.ringtone, val, vl + 1);   // validated as RTTTL by the caller once complete
      }
    }
    else if (strcmp(key, "msgs") == 0) {
      ok = parseRange(val, 0, TAPTAP_MAX_CANNED, x);
      if (ok) {
        for (int k = x; k < TAPTAP_MAX_CANNED; k++) st.canned[k][0] = 0;
        st.canned_count = x;
      }
    }
    else if (key[0] == 'm' && key[1] >= '1' && key[1] <= '0' + TAPTAP_MAX_CANNED && key[2] == 0) {
      int slot = key[1] - '1';
      if (append) {
        size_t cur = slot < st.canned_count ? strlen(st.canned[slot]) : 0;
        ok = slot < st.canned_count && cur + vl < TAPTAP_CANNED_LEN;
        if (ok) memcpy(&st.canned[slot][cur], val, vl + 1);
      } else {
        ok = vl > 0 && vl < TAPTAP_CANNED_LEN && slot <= st.canned_count;   // fill slots in order
        if (ok) {
          memcpy(st.canned[slot], val, vl + 1);
          if (slot == st.canned_count) st.canned_count = slot + 1;
        }
      }
    }
    else if (strcmp(key, "tap") == 0)   ok = !append && tap_handler && tap_handler(val, ctx);
    else ok = false;

    if (ok) res.applied++;
    else setError(res, key);
  }
  return res;
}

}  // namespace taptap

#endif  // TAPTAP_FEATURES
