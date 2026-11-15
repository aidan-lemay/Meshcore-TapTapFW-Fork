#include "TapPlayer.h"
#include <ctype.h>
#include <string.h>

// octave 4 frequencies, c..b
static const uint16_t OCT4[12] = { 262, 277, 294, 311, 330, 349, 370, 392, 415, 440, 466, 494 };

void TapPlayer::begin(int buzzer_pin, int buzzer_en_pin, int led_pin, bool led_active_high) {
  _buzzer_pin = buzzer_pin;
  _en_pin = buzzer_en_pin;
  _led_pin = led_pin;
  _led_high = led_active_high;
  if (_en_pin >= 0) pinMode(_en_pin, OUTPUT);
  if (_buzzer_pin >= 0) {
    pinMode(_buzzer_pin, OUTPUT);
    digitalWrite(_buzzer_pin, LOW);   // keep low, avoids excess current draw
  }
  if (_led_pin >= 0) pinMode(_led_pin, OUTPUT);
  setQuiet(_quiet);
  setBuzzer(0);
  setLed(false);
}

void TapPlayer::setQuiet(bool quiet) {
  _quiet = quiet;
  if (_quiet && _buzzer_pin >= 0) noTone(_buzzer_pin);
  if (_en_pin >= 0) digitalWrite(_en_pin, _quiet ? LOW : HIGH);
}

void TapPlayer::setBuzzer(uint16_t freq) {
  if (_buzzer_pin < 0) return;
  if (freq && !_quiet) tone(_buzzer_pin, freq);
  else noTone(_buzzer_pin);
}

void TapPlayer::setLed(bool on) {
  if (_led_pin >= 0) digitalWrite(_led_pin, on == _led_high ? HIGH : LOW);
}

static int readNum(const char*& p) {
  int n = 0;
  while (isdigit((unsigned char)*p)) n = n * 10 + (*p++ - '0');
  return n;
}

bool TapPlayer::Track::start(const char* rtttl) {
  playing = false;
  p = nullptr;
  if (rtttl == nullptr) return false;
  const char* s = strchr(rtttl, ':');   // skip name
  if (!s) return false;
  s++;
  def_dur = 4; def_oct = 6; bpm = 63;
  while (*s && *s != ':') {             // defaults section
    char key = tolower((unsigned char)*s);
    if (s[1] == '=') {
      s += 2;
      int v = readNum(s);
      if (key == 'd' && v > 0) def_dur = v;
      else if (key == 'o' && v >= 3 && v <= 8) def_oct = v;
      else if (key == 'b' && v > 0) bpm = v;
    } else {
      s++;
    }
    if (*s == ',') s++;
  }
  if (*s != ':') return false;
  p = s + 1;
  playing = true;
  return true;
}

bool TapPlayer::Track::next(uint16_t& freq, unsigned long now) {
  const char* s = p;
  while (*s == ',' || *s == ' ') s++;
  if (*s == 0) return false;

  int dur = readNum(s);
  if (dur == 0) dur = def_dur;
  unsigned long ms = (60000UL * 4) / bpm / dur;

  int semis = -1;   // -1 = pause
  switch (tolower((unsigned char)*s)) {
    case 'c': semis = 0; break;
    case 'd': semis = 2; break;
    case 'e': semis = 4; break;
    case 'f': semis = 5; break;
    case 'g': semis = 7; break;
    case 'a': semis = 9; break;
    case 'b': case 'h': semis = 11; break;
    case 'p': semis = -1; break;
    default:  return false;   // malformed: stop rather than guess
  }
  s++;
  if (*s == '#') { if (semis >= 0) semis++; s++; }
  bool dotted = false;
  if (*s == '.') { dotted = true; s++; }
  int oct = isdigit((unsigned char)*s) ? readNum(s) : def_oct;
  if (*s == '.') { dotted = true; s++; }
  if (dotted) ms += ms / 2;
  while (*s && *s != ',') s++;   // skip anything unexpected up to the separator
  p = s;

  freq = 0;
  if (semis >= 0) {
    if (semis == 12) { semis = 0; oct++; }
    uint32_t f = OCT4[semis];
    if (oct >= 4) f <<= (oct - 4); else f >>= (4 - oct);
    freq = (uint16_t)f;
  }
  note_end = now + ms;
  return true;
}

void TapPlayer::advance(Track& t, bool is_led_track) {
  uint16_t freq = 0;
  if (t.next(freq, millis())) {
    if (is_led_track) {
      setLed(freq != 0);
    } else {
      setBuzzer(freq);
      if (_mirror) setLed(freq != 0);
    }
  } else {
    t.playing = false;
    if (is_led_track) {
      setLed(false);
    } else {
      setBuzzer(0);
      if (_mirror) setLed(false);
    }
  }
}

void TapPlayer::play(const char* rtttl, const char* led_rtttl) {
  stop();
  if (rtttl && *rtttl) {
    strncpy(_buf, rtttl, sizeof(_buf) - 1);
    _buf[sizeof(_buf) - 1] = 0;
    if (_buz.start(_buf)) advance(_buz, false);
  }
  _mirror = (led_rtttl == nullptr);
  if (!_mirror && *led_rtttl) {
    strncpy(_led_buf, led_rtttl, sizeof(_led_buf) - 1);
    _led_buf[sizeof(_led_buf) - 1] = 0;
    if (_led.start(_led_buf)) advance(_led, true);
  }
}

void TapPlayer::loop() {
  unsigned long now = millis();
  if (_buz.playing && (long)(now - _buz.note_end) >= 0) advance(_buz, false);
  if (_led.playing && (long)(now - _led.note_end) >= 0) advance(_led, true);
}

void TapPlayer::sideTone(bool on) {
  _buz.playing = _led.playing = false;
  _mirror = true;
  setBuzzer(on ? 1760 : 0);
  setLed(on);
}

void TapPlayer::stop() {
  _buz.playing = _led.playing = false;
  _mirror = true;
  setBuzzer(0);
  setLed(false);
}
