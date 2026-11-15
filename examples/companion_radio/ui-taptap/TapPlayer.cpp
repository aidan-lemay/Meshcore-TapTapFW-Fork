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
  outputs(0);
}

void TapPlayer::setQuiet(bool quiet) {
  _quiet = quiet;
  if (_quiet && _buzzer_pin >= 0) noTone(_buzzer_pin);
  if (_en_pin >= 0) digitalWrite(_en_pin, _quiet ? LOW : HIGH);
}

void TapPlayer::outputs(uint16_t freq) {
  if (_buzzer_pin >= 0) {
    if (freq && !_quiet) tone(_buzzer_pin, freq);
    else noTone(_buzzer_pin);
  }
  if (_led_pin >= 0 && _led_mirror) {
    bool on = freq != 0;
    digitalWrite(_led_pin, on == _led_high ? HIGH : LOW);
  }
}

static int readNum(const char*& p) {
  int n = 0;
  while (isdigit((unsigned char)*p)) n = n * 10 + (*p++ - '0');
  return n;
}

void TapPlayer::play(const char* rtttl) {
  stop();
  if (rtttl == nullptr || *rtttl == 0) return;
  strncpy(_buf, rtttl, sizeof(_buf) - 1);
  _buf[sizeof(_buf) - 1] = 0;

  // name section
  const char* p = strchr(_buf, ':');
  if (!p) return;
  p++;
  // defaults section
  _def_dur = 4; _def_oct = 6; _bpm = 63;
  while (*p && *p != ':') {
    char key = tolower((unsigned char)*p);
    if (p[1] == '=') {
      p += 2;
      int v = readNum(p);
      if (key == 'd' && v > 0) _def_dur = v;
      else if (key == 'o' && v >= 3 && v <= 8) _def_oct = v;
      else if (key == 'b' && v > 0) _bpm = v;
    } else {
      p++;
    }
    if (*p == ',') p++;
  }
  if (*p != ':') return;
  _p = p + 1;
  _playing = nextNote();
}

bool TapPlayer::nextNote() {
  const char* p = _p;
  while (*p == ',' || *p == ' ') p++;
  if (*p == 0) return false;

  int dur = readNum(p);
  if (dur == 0) dur = _def_dur;
  unsigned long ms = (60000UL * 4) / _bpm / dur;

  int semis = -1;   // -1 = pause
  switch (tolower((unsigned char)*p)) {
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
  p++;
  if (*p == '#') { if (semis >= 0) semis++; p++; }
  bool dotted = false;
  if (*p == '.') { dotted = true; p++; }
  int oct = isdigit((unsigned char)*p) ? readNum(p) : _def_oct;
  if (*p == '.') { dotted = true; p++; }
  if (dotted) ms += ms / 2;
  while (*p && *p != ',') p++;   // skip anything unexpected up to the separator
  _p = p;

  uint16_t freq = 0;
  if (semis >= 0) {
    if (semis == 12) { semis = 0; oct++; }
    uint32_t f = OCT4[semis];
    if (oct >= 4) f <<= (oct - 4); else f >>= (4 - oct);
    freq = (uint16_t)f;
  }
  outputs(freq);
  _note_end = millis() + ms;
  return true;
}

void TapPlayer::loop() {
  if (!_playing) return;
  if ((long)(millis() - _note_end) < 0) return;
  if (!nextNote()) stop();
}

void TapPlayer::sideTone(bool on) {
  _playing = false;
  _p = nullptr;
  outputs(on ? 1760 : 0);
}

void TapPlayer::stop() {
  _playing = false;
  _p = nullptr;
  outputs(0);
}
