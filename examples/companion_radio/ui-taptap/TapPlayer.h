#pragma once

#include <Arduino.h>
#include "../TapTap.h"

// Non-blocking RTTTL player with two tracks: one drives the piezo, one drives the LED.
// play(melody) mirrors every note on the LED, so alerts can be followed when muted.
// play(buzzer_melody, led_melody) runs the LED on its own timing (e.g. the slot pattern,
// where the LED shows dots/dashes but the buzzer uses equal-length high/low beeps).
class TapPlayer {
public:
  void begin(int buzzer_pin, int buzzer_en_pin, int led_pin, bool led_active_high);
  void play(const char* rtttl, const char* led_rtttl = nullptr);   // copies; interrupts anything playing
  void stop();
  void sideTone(bool on);         // continuous tone (+LED) while a Morse key is held
  void loop();
  bool isPlaying() const { return _buz.playing || _led.playing; }

  void setQuiet(bool quiet);      // quiet = LED only
  bool isQuiet() const { return _quiet; }

private:
  struct Track {
    const char* p = nullptr;
    bool playing = false;
    uint8_t def_dur = 4, def_oct = 6;
    uint16_t bpm = 63;
    unsigned long note_end = 0;
    bool start(const char* rtttl);           // parse header; false if malformed
    bool next(uint16_t& freq, unsigned long now);   // start next note; false at end
  };

  void setBuzzer(uint16_t freq);
  void setLed(bool on);
  void advance(Track& t, bool is_led_track);

  char _buf[TAPTAP_CW_BUF_LEN];
  char _led_buf[96];
  Track _buz, _led;
  bool _mirror = true;            // LED follows the buzzer track
  bool _quiet = false;
  int _buzzer_pin = -1, _en_pin = -1, _led_pin = -1;
  bool _led_high = true;
};
