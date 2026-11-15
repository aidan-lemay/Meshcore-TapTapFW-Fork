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
#define TAPTAP_CW_WPM_DEFAULT 19
#define TAPTAP_CW_WPM_MIN     1
#define TAPTAP_CW_WPM_MAX     40
#define TAPTAP_SLOT_MS_DEFAULT 500        // LED dash length
#define TAPTAP_SLOT_BEEP_MS_DEFAULT 200   // buzzer beep length
#define TAPTAP_SLOT_MS_MIN     50
#define TAPTAP_SLOT_MS_MAX     2000

enum TapBuzMode : uint8_t {
  TAP_BUZ_RTTTL    = 0,   // ringtone
  TAP_BUZ_CWMATCH  = 1,   // canned match -> slot pattern, otherwise Morse (value kept from the old "cw")
  TAP_BUZ_OFF      = 2,
  TAP_BUZ_CW       = 3,   // always Morse, no table check
  TAP_BUZ_RTTTLMATCH = 4, // canned match -> slot pattern, otherwise ringtone
};

// led_flags bits (a set bit turns the feature OFF)
#define TAP_LED_NO_STANDBY  0x01   // no heartbeat / messages-waiting blink when idle

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
  uint8_t  led_flags;     // TAP_LED_* bits (default: TAP_LED_NO_STANDBY)
  char     canned[TAPTAP_MAX_CANNED][TAPTAP_CANNED_LEN];
  char     ringtone[TAPTAP_RINGTONE_LEN];   // global message ringtone, "" = firmware default
  TapTarget target;
  uint8_t  repeat_alert;      // 1 = repeat incoming-message alerts until acknowledged (single click)
  uint8_t  repeat_gap_secs;   // pause between repeats
  // --- fields below were added later; older (shorter) files load with these at defaults ---
  uint8_t  cw_wpm;            // CW alert speed (spelled-out text), words per minute
  uint16_t slot_beep_ms;      // slot pattern: buzzer beep length (all beeps equal, high = 1, low = 0)
  uint16_t slot_flash_ms;     // slot pattern: LED dash length (1 = dash, 0 = dot = half)

  static const uint32_t MAGIC = 0x50415454;  // 'TTAP'
  static const uint8_t  VERSION = 1;

  void setDefaults();
  void resetKeepMessages();   // defaults, but keep canned messages + tap target
  bool isValid() const;
  void sanitise();            // clamp fields that may be out of range (e.g. from older files)
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

  // Builds an RTTTL melody that plays 'text' as Morse code at 'wpm'. Returns length written.
  int buildCwRtttl(const char* text, char* out, size_t out_len, int wpm = TAPTAP_CW_WPM_DEFAULT);

  // Parses "item1|item2|..." into settings.canned. Returns number of entries (0 = none valid).
  int parseCannedList(const char* s, TapTapSettings& st);

  // Accepts a full RTTTL ("name:d=4,o=5,b=120:notes") or a header-less one ("d=4,o=5,b=120:notes")
  // and normalises it into 'out'. Returns false if it doesn't look like RTTTL.
  bool normaliseRtttl(const char* in, char* out, size_t out_len, bool* truncated);

  // Index of the canned slot whose text equals 'text' (case-insensitive, trimmed), or -1.
  int matchCanned(const char* text, const TapTapSettings& st);

  // Slot pattern (3-bit binary of 'idx', MSB first), as two tracks played together:
  //  buzzer: equal-length beeps, high tone = 1, low tone = 0, gaps beep_ms/2
  //  LED:    1 = dash (flash_ms), 0 = dot (flash_ms/2), gaps flash_ms/2
  // 'out' >= 64 bytes each.
  void buildSlotBeeps(int idx, char* out, size_t out_len, int beep_ms = TAPTAP_SLOT_BEEP_MS_DEFAULT);
  void buildSlotFlashes(int idx, char* out, size_t out_len, int flash_ms = TAPTAP_SLOT_MS_DEFAULT);

  // ---- export / import ----
  // Export writes up to 'max_lines' lines, each a complete "/import k=v;k=v" command that fits one
  // app message. 'tap_spec' is "ch:<name>" / "dev:<hex pubkey>" (or "" to skip). Returns line count.
  #define TAPTAP_EXPORT_LINE 121   // 120 chars: fits one app message even with a long node name
  int exportSettings(const TapTapSettings& st, const char* tap_spec, char lines[][TAPTAP_EXPORT_LINE], int max_lines);

  struct ImportResult {
    int applied = 0;
    int errors = 0;
    char first_error[32] = {0};
  };
  // Applies "k=v;k=v" pairs to 'st' ('text' is what follows "/import"). 'tap' values are handed to
  // tap_handler (which returns false if it can't resolve them). Values escape ';' and '\' with '\'.
  ImportResult importSettings(const char* text, TapTapSettings& st,
                              bool (*tap_handler)(const char* value, void* ctx), void* ctx);

  // escape ';' and '\' for export values
  void escapeValue(const char* in, char* out, size_t out_len);
}
