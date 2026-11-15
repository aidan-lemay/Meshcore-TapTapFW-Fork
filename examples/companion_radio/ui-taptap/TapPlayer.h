#pragma once

#include <Arduino.h>
#include "../TapTap.h"

// Non-blocking RTTTL player that drives the piezo AND mirrors every note on an LED,
// so alerts can be followed visually when the buzzer is muted.
class TapPlayer {
public:
  void begin(int buzzer_pin, int buzzer_en_pin, int led_pin, bool led_active_high);
  void play(const char* rtttl);   // copies the melody; interrupts anything already playing
  void stop();
  void sideTone(bool on);         // continuous tone (+LED) while a Morse key is held
  void loop();
  bool isPlaying() const { return _playing; }

  void setQuiet(bool quiet);      // quiet = LED only
  bool isQuiet() const { return _quiet; }
  void setLedMirror(bool on) { _led_mirror = on; }

private:
  bool nextNote();                // parse + start the next note, false at end of melody
  void outputs(uint16_t freq);

  char _buf[TAPTAP_CW_BUF_LEN];
  const char* _p = nullptr;
  bool _playing = false;
  bool _quiet = false;
  bool _led_mirror = true;
  int _buzzer_pin = -1, _en_pin = -1, _led_pin = -1;
  bool _led_high = true;
  uint8_t _def_dur = 4, _def_oct = 6;
  uint16_t _bpm = 63;
  unsigned long _note_end = 0;
};
