#pragma once

// TapTap: headless tracker features (canned messages, tap target, buzzer modes,
// CW/RTTTL alerts, in-app slash commands). Re-implementation of the behaviour of
// the closed TapTap firmware on top of upstream companion_radio.
// Everything here is only compiled when TAPTAP_FEATURES is defined.

#include <Arduino.h>
#include <stdint.h>
#include <stddef.h>

#define TAPTAP_MAX_CANNED     8
#define TAPTAP_CANNED_LEN     64     // incl. null terminator
#define TAPTAP_RINGTONE_LEN   192    // incl. null terminator
#define TAPTAP_CW_BUF_LEN     640    // max length of a generated CW RTTTL string
#define TAPTAP_SETTINGS_FILE  "/taptap"

enum TapBuzMode : uint8_t {
  TAP_BUZ_RTTTL = 0,
  TAP_BUZ_CW    = 1,
  TAP_BUZ_OFF   = 2,
};

enum TapTargetType : uint8_t {
  TAP_TARGET_CHANNEL = 0,   // default: channel_idx (0 == Public)
  TAP_TARGET_CONTACT = 1,
};

struct TapTarget {
  uint8_t type;           // TapTargetType
  uint8_t channel_idx;    // last known index (channels can be re-ordered by the app)
  uint8_t key[32];        // channel secret, or contact public key. all-zero = 'not pinned'
};

struct TapTapSettings {
  uint32_t magic;
  uint8_t  version;
  uint8_t  buz_mode;      // TapBuzMode
  uint8_t  canned_count;
  uint8_t  _reserved;
  char     canned[TAPTAP_MAX_CANNED][TAPTAP_CANNED_LEN];
  char     ringtone[TAPTAP_RINGTONE_LEN];   // global message ringtone, "" = firmware default
  TapTarget target;
  uint8_t  repeat_alert;      // 1 = repeat incoming-message alerts until acknowledged (single click)
  uint8_t  repeat_gap_secs;   // pause between repeats

  static const uint32_t MAGIC = 0x50415454;  // 'TTAP'
  static const uint8_t  VERSION = 1;

  void setDefaults();
  bool isValid() const;
};

namespace taptap {
  const char* buzModeName(uint8_t mode);
  bool parseBuzMode(const char* s, uint8_t& mode);

  // "Sender: text" -> "text" (channel messages carry the sender name in the text)
  const char* stripSenderPrefix(const char* text);

  // FNV-1a, used to de-dupe alerts for the same text heard via different packets
  uint32_t fnv1a(const char* s);

  // Morse pattern (e.g. ".-") -> character ('A'), or 0 if unknown.
  char decodeMorse(const char* pattern);

  // Builds an RTTTL melody that plays 'text' as Morse code. Returns length written.
  int buildCwRtttl(const char* text, char* out, size_t out_len);

  // Parses "item1|item2|..." into settings.canned. Returns number of entries (0 = none valid).
  int parseCannedList(const char* s, TapTapSettings& st);

  // Accepts a full RTTTL ("name:d=4,o=5,b=120:notes") or a header-less one ("d=4,o=5,b=120:notes")
  // and normalises it into 'out'. Returns false if it doesn't look like RTTTL.
  bool normaliseRtttl(const char* in, char* out, size_t out_len, bool* truncated);

  // Index of the canned slot whose text equals 'text' (case-insensitive, trimmed), or -1.
  int matchCanned(const char* text, const TapTapSettings& st);

  // Melody that plays 3-bit binary of 'idx' (high tone = 1, low tone = 0). 'out' >= 64 bytes.
  void buildIndexTone(int idx, char* out, size_t out_len);
}
