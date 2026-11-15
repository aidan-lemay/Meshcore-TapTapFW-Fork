#include "UITask.h"
#include <Arduino.h>
#include <stdarg.h>
#include <helpers/TxtDataHelpers.h>
#include "../MyMesh.h"

#define AUTO_OFF_MILLIS         15000
#define CANNED_TIMEOUT_MILLIS   15000   // leave canned mode after this long without a click
#define CLI_RESCUE_WINDOW       8000    // long press this soon after boot -> CLI rescue
#define ALERT_DEDUPE_MILLIS     30000
#define MORSE_DAH_MILLIS        250     // key held >= this = dah, shorter = dit
#define MORSE_LETTER_GAP_MILLIS 700     // pause that ends a letter
#define MORSE_WORD_GAP_MILLIS   2000    // pause that inserts a space
#define MORSE_TIMEOUT_MILLIS    10000   // leave Morse mode after this long with no keying

#ifndef PIN_BUZZER
  #define PIN_BUZZER -1
#endif
#ifndef PIN_BUZZER_EN
  #define PIN_BUZZER_EN -1
#endif
#ifndef PIN_STATUS_LED
  #define PIN_STATUS_LED -1
#endif
#ifndef LED_STATE_ON
  #define LED_STATE_ON HIGH
#endif
#ifndef USER_BTN_PRESSED
  #define USER_BTN_PRESSED LOW
#endif

// tones (kept identical to the TapTap firmware)
static const char TONE_MSG_DM[]       = "MsgRcv3:d=4,o=6,b=200:32e,32g,32b,16c7";
static const char TONE_MSG_CHANNEL[]  = "kerplop:d=16,o=6,b=120:32g#,32c#";
static const char TONE_ACK[]          = "ack:d=32,o=8,b=120:c";
static const char TONE_STARTUP[]      = "Startup:d=4,o=5,b=160:16c6,16e6,8g6";
static const char TONE_POWERDOWN[]    = "PowerDown:d=16,o=5,b=140:8g5,8e5,8c5";
static const char TONE_ADVERT[]       = "AdvertLow:d=8,o=4,b=180:8c4";
static const char TONE_ERR[]          = "CannedErr:d=32,o=5,b=160:8c5,16p,8c5";
static const char TONE_SEND[]         = "CannedSend:d=16,o=6,b=200:8g5,8b5,8d6";
static const char TONE_ENTER[]        = "CannedEnter:d=32,o=6,b=220:16c6,16e6,16g6";
static const char TONE_EXIT[]         = "CannedExit:d=32,o=5,b=180:16g5,16e5,16c5";
static const char TONE_GPS_ON[]       = "GpsOn:d=32,o=6,b=200:16c6,16e6,16g6";
static const char TONE_GPS_OFF[]      = "GpsOff:d=32,o=5,b=200:16g5,16e5,16c5";
static const char TONE_UNMUTE[]       = "Unmute:d=16,o=6,b=200:c,g";
static const char TONE_MORSE_IN[]     = "MorseIn:d=32,o=6,b=220:16g6,16p,16g6";
static const char TONE_MORSE_DEL[]    = "MorseDel:d=32,o=5,b=200:16c5";
static const char TONE_MUTE[]         = "Mute:d=16,o=6,b=200:g,c";   // LED only (played after muting)

void UITask::begin(DisplayDriver* display, SensorManager* sensors, NodePrefs* node_prefs) {
  _display = display;
  _sensors = sensors;
  _node_prefs = node_prefs;
  _auto_off = millis() + AUTO_OFF_MILLIS;
  if (_display != NULL) _display->turnOn();

  _player.begin(PIN_BUZZER, PIN_BUZZER_EN, PIN_STATUS_LED, LED_STATE_ON == HIGH);
  _player.setQuiet(_node_prefs->buzzer_quiet);
  playTone(TONE_STARTUP);

#ifdef PIN_USER_BTN
  _userButton = new Button(PIN_USER_BTN, USER_BTN_PRESSED);
  _userButton->begin();
  _userButton->onShortPress([this]() { handleButtonShortPress(); });
  _userButton->onDoublePress([this]() { handleButtonDoublePress(); });
  _userButton->onTriplePress([this]() { handleButtonTriplePress(); });
  _userButton->onQuadruplePress([this]() { handleButtonQuadruplePress(); });
  _userButton->onLongPress([this]() { handleButtonLongPress(); });
  _userButton->onVeryLongPress([this]() { handleButtonVeryLongPress(); });
  _userButton->onAnyPress([this]() { handleButtonAnyPress(); });
  _userButton->onRawDown([this]() { morseKeyDown(); });
  _userButton->onRawUp([this](uint32_t ms) { morseKeyUp(ms); });
#endif
  ui_started_at = millis();
  setStatus("MyMesh ready (%u)", (unsigned)the_mesh.getTapTap().canned_count);
}

void UITask::setStatus(const char* fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(_status, sizeof(_status), fmt, ap);
  va_end(ap);
  MESH_DEBUG_PRINTLN("TapTap: %s", _status);
  if (_display != NULL) {
    _display->turnOn();
    _auto_off = millis() + AUTO_OFF_MILLIS;
  }
}

/* ---------------------------------- sound / alerts ---------------------------------- */

void UITask::playTone(const char* rtttl) {
  _playing_alert = false;
  _player.play(rtttl);
}

bool UITask::onPlayMelody(const char* rtttl) {
  playTone(rtttl);
  return true;   // even when muted the LED shows it
}

void UITask::notify(UIEventType t) {
  switch (t) {
    case UIEventType::contactMessage: playTone(TONE_MSG_DM); break;
    case UIEventType::channelMessage: playTone(TONE_MSG_CHANNEL); break;
    case UIEventType::ack:            playTone(TONE_ACK); break;
    default: break;
  }
}

bool UITask::recentlyAlerted(uint32_t h) {
  unsigned long now = millis();
  for (int i = 0; i < 4; i++) {
    if (_recent_hash[i] == h && _recent_at[i] && now - _recent_at[i] < ALERT_DEDUPE_MILLIS) return true;
  }
  _recent_hash[_recent_pos] = h;
  _recent_at[_recent_pos] = now;
  _recent_pos = (_recent_pos + 1) % 4;
  return false;
}

void UITask::alertIncoming(const char* body, bool is_channel) {
  TapTapSettings& st = the_mesh.getTapTap();
  if (st.buz_mode == TAP_BUZ_OFF) return;
  if (recentlyAlerted(taptap::fnv1a(body))) return;

  _alert_melody[0] = 0;
  if (st.buz_mode == TAP_BUZ_CW) {
    int slot = taptap::matchCanned(body, st);
    if (slot >= 0) {
      // a known canned message: play its slot pattern, same as the tapper selection tone
      taptap::buildIndexTone(slot, _alert_melody, sizeof(_alert_melody));
    } else {
      taptap::buildCwRtttl(body, _alert_melody, sizeof(_alert_melody), st.cw_wpm);
    }
  } else if (st.ringtone[0]) {
    StrHelper::strncpy(_alert_melody, st.ringtone, sizeof(_alert_melody));
  }
  if (_alert_melody[0] == 0) {   // nothing encodable / no custom ringtone
    StrHelper::strncpy(_alert_melody, is_channel ? TONE_MSG_CHANNEL : TONE_MSG_DM, sizeof(_alert_melody));
  }

  if (_morse_mode || (_player.isPlaying() && _playing_alert)) {
    _alert_queued = true;        // don't talk over the key / current alert: play the newest after
  } else {
    _player.play(_alert_melody);
    _playing_alert = true;
  }
  _gap_started = false;
  _alert_pending = st.repeat_alert != 0;
}

void UITask::ackAlert() {
  _alert_pending = false;
  _alert_queued = false;
  _gap_started = false;
  if (_playing_alert) _player.stop();
  _playing_alert = false;
  setStatus("Alert acknowledged");
}

void UITask::onMessageRecv(mesh::Packet *pkt, const ContactInfo &from, uint8_t txt_type, uint32_t sender_timestamp, const char* text) {
  if (!(txt_type == TXT_TYPE_PLAIN || txt_type == TXT_TYPE_SIGNED_PLAIN)) return;
  setStatus("%s: %s", from.name, text);
  alertIncoming(text, false);
}

void UITask::onChannelMessageRecv(mesh::Packet *pkt, ChannelDetails& channel_details, const char* text) {
  setStatus("%s", text);
  alertIncoming(taptap::stripSenderPrefix(text), true);
}

void UITask::onQueueSizeChanged(int msgcount) {
  _msgcount = msgcount;
  if (msgcount == 0 && _alert_pending) ackAlert();   // read in the app: stop repeating
}

/* ---------------------------------- buttons ---------------------------------- */

void UITask::handleButtonAnyPress() {
  if (_display != NULL) {
    if (!_display->isOn()) _display->turnOn();
    _auto_off = millis() + AUTO_OFF_MILLIS;
  }
  if (_canned_mode) _canned_last = millis();
}

// 1 click: acknowledge alert > next canned message > send advert
void UITask::handleButtonShortPress() {
  if (_alert_pending || (_playing_alert && _player.isPlaying())) {
    ackAlert();
    playTone(TONE_ACK);
    return;
  }
  TapTapSettings& st = the_mesh.getTapTap();
  if (_canned_mode) {
    if (st.canned_count == 0) {
      playTone(TONE_ERR);
      setStatus("/msg error: no entries");
      return;
    }
    _canned_idx = (_canned_idx + 1) % st.canned_count;
    char tone[80];
    taptap::buildIndexTone(_canned_idx, tone, sizeof(tone));
    playTone(tone);
    setStatus("[%d] %s", _canned_idx + 1, st.canned[_canned_idx]);
    return;
  }
  if (the_mesh.advert()) {
    playTone(TONE_ADVERT);
    setStatus("Advert sent");
  } else {
    playTone(TONE_ERR);
    setStatus("Advert failed");
  }
}

// 2 clicks: enter / leave canned message mode
void UITask::handleButtonDoublePress() {
  if (_canned_mode) {          // second double click: canned -> Morse input
    exitCannedMode(false);
    enterMorseMode();
    return;
  }
  _canned_mode = true;
  _canned_idx = -1;
  _canned_last = millis();
  playTone(TONE_ENTER);
  char target[48];
  the_mesh.describeTapTarget(target, sizeof(target));
  setStatus("Canned mode -> %s", target);
}

void UITask::exitCannedMode(bool with_tone) {
  _canned_mode = false;
  _canned_idx = -1;
  if (with_tone) playTone(TONE_EXIT);
}

// 3 clicks: GPS on/off
void UITask::handleButtonTriplePress() {
  if (_sensors == NULL) return;
  int num = _sensors->getNumSettings();
  for (int i = 0; i < num; i++) {
    if (strcmp(_sensors->getSettingName(i), "gps") == 0) {
      bool on = strcmp(_sensors->getSettingValue(i), "1") != 0;
      _sensors->setSettingValue("gps", on ? "1" : "0");
      _node_prefs->gps_enabled = on ? 1 : 0;
      the_mesh.savePrefs();
      playTone(on ? TONE_GPS_ON : TONE_GPS_OFF);
      setStatus("GPS: %s", on ? "Enabled" : "Disabled");
      return;
    }
  }
}

// 4 clicks: mute / unmute buzzer (LED keeps mirroring everything)
void UITask::handleButtonQuadruplePress() {
  bool quiet = !_player.isQuiet();
  _player.setQuiet(quiet);
  _node_prefs->buzzer_quiet = quiet ? 1 : 0;
  the_mesh.savePrefs();
  playTone(quiet ? TONE_MUTE : TONE_UNMUTE);
  setStatus("Buzzer: %s", quiet ? "muted" : "on");
}

// hold 1.2s: send selected canned / composed Morse message, or CLI rescue right after boot
void UITask::handleButtonLongPress() {
  if (_morse_mode) {
    morseSend();
    return;
  }
  if (_canned_mode) {
    TapTapSettings& st = the_mesh.getTapTap();
    if (_canned_idx < 0 || _canned_idx >= st.canned_count) {
      playTone(TONE_ERR);
      setStatus("No message selected");
      return;
    }
    if (the_mesh.sendTapMessage(st.canned[_canned_idx])) {
      playTone(TONE_SEND);
      setStatus("Sent: %s", st.canned[_canned_idx]);
    } else {
      playTone(TONE_ERR);
      setStatus("Send failed (tap target unavailable?)");
    }
    exitCannedMode(false);
    return;
  }
  if (millis() - ui_started_at < CLI_RESCUE_WINDOW) {
    the_mesh.enterCLIRescue();
  }
}

// hold 5s: power off
void UITask::handleButtonVeryLongPress() {
  shutdown();
}

void UITask::shutdown(bool restart) {
  _alert_pending = _alert_queued = false;
  playTone(TONE_POWERDOWN);
  uint32_t t0 = millis();
  while (_player.isPlaying() && millis() - t0 < 2500) _player.loop();
  _player.stop();
  if (restart) _board->reboot();
  else _board->powerOff();
}

/* ---------------------------------- Morse input ---------------------------------- */

void UITask::enterMorseMode() {
  _morse_mode = true;
  _morse_text[0] = 0;
  _morse_sym[0] = 0;
  _key_down = false;
  _morse_last = millis();
#ifdef PIN_USER_BTN
  if (_userButton) _userButton->setRawMode(true);
#endif
  playTone(TONE_MORSE_IN);
  setStatus("Morse input: key message, hold to send");
}

void UITask::exitMorseMode(bool with_tone) {
  _morse_mode = false;
  _key_down = false;
  _player.sideTone(false);
#ifdef PIN_USER_BTN
  if (_userButton) _userButton->setRawMode(false);
#endif
  if (with_tone) playTone(TONE_EXIT);
}

void UITask::morseKeyDown() {
  if (!_morse_mode) return;
  unsigned long now = millis();
  if (_morse_sym[0] == 0 && _morse_text[0] && now - _morse_last >= MORSE_WORD_GAP_MILLIS) {
    size_t n = strlen(_morse_text);
    if (_morse_text[n - 1] != ' ' && n < sizeof(_morse_text) - 1) {
      _morse_text[n] = ' ';
      _morse_text[n + 1] = 0;
    }
  }
  _key_down = true;
  _morse_last = now;
  _player.sideTone(true);
}

void UITask::morseKeyUp(uint32_t held_ms) {
  if (!_morse_mode) return;
  _key_down = false;
  _player.sideTone(false);
  _morse_last = millis();
  size_t n = strlen(_morse_sym);
  if (n < sizeof(_morse_sym) - 1) {
    _morse_sym[n] = held_ms >= MORSE_DAH_MILLIS ? '-' : '.';
    _morse_sym[n + 1] = 0;
  }
}

void UITask::morseFlushSymbol() {
  if (_morse_sym[0] == 0) return;
  size_t n = strlen(_morse_text);
  if (strncmp(_morse_sym, "........", 8) == 0) {   // 8+ dits: error prosign = delete last letter
    while (n > 0 && _morse_text[n - 1] == ' ') n--;
    if (n > 0) n--;
    _morse_text[n] = 0;
    playTone(TONE_MORSE_DEL);
    setStatus("Morse: %s", _morse_text);
  } else {
    char c = taptap::decodeMorse(_morse_sym);
    if (c == 0) {
      playTone(TONE_ERR);
      setStatus("Morse: ? (%s)", _morse_sym);
    } else if (n < sizeof(_morse_text) - 1) {
      _morse_text[n] = c;
      _morse_text[n + 1] = 0;
      setStatus("Morse: %s", _morse_text);
    }
  }
  _morse_sym[0] = 0;
}

void UITask::morseSend() {
  _key_down = false;
  _player.sideTone(false);
  morseFlushSymbol();
  // trim trailing spaces
  size_t n = strlen(_morse_text);
  while (n > 0 && _morse_text[n - 1] == ' ') _morse_text[--n] = 0;
  exitMorseMode(false);
  if (n == 0) {
    playTone(TONE_ERR);
    setStatus("Morse: nothing to send");
  } else if (the_mesh.sendTapMessage(_morse_text)) {
    playTone(TONE_SEND);
    setStatus("Sent: %s", _morse_text);
  } else {
    playTone(TONE_ERR);
    setStatus("Send failed (tap target unavailable?)");
  }
}

/* ---------------------------------- loop ---------------------------------- */

void UITask::userLedHandler() {
#if PIN_STATUS_LED >= 0
  static int state = 0;
  static unsigned long next_change = 0;
  static int last_increment = 0;
  if (_player.isPlaying()) {    // the LED is mirroring the buzzer
    state = 0;
    next_change = millis() + 500;
    return;
  }
  unsigned long now = millis();
  if (the_mesh.getTapTap().led_flags & TAP_LED_NO_STANDBY) {   // LED only used for tones/alerts
    if (state) digitalWrite(PIN_STATUS_LED, !LED_STATE_ON);
    state = 0;
    next_change = now;
    return;
  }
  if (now > next_change) {
    if (state == 0) {
      state = 1;
      last_increment = (_msgcount > 0 || _alert_pending) ? 200 : 20;
      next_change = now + last_increment;
    } else {
      state = 0;
      next_change = now + 4000 - last_increment;
    }
    digitalWrite(PIN_STATUS_LED, state == 1 ? LED_STATE_ON : !LED_STATE_ON);
  }
#endif
}

void UITask::renderCurrScreen() {
  _display->setCursor(0, 0);
  _display->setTextSize(1);
  _display->setColor(UIColor::primary_txt);
  _display->print(_node_prefs->node_name);
  _display->setCursor(0, 14);
  _display->setColor(UIColor::secondary_txt);
  _display->print(_status);
}

void UITask::loop() {
#ifdef PIN_USER_BTN
  if (_userButton) _userButton->update();
#endif
  _player.loop();

  if (_morse_mode && !_key_down) {
    unsigned long idle = millis() - _morse_last;
    if (_morse_sym[0] && idle >= MORSE_LETTER_GAP_MILLIS) morseFlushSymbol();
    if (idle >= MORSE_TIMEOUT_MILLIS) {
      exitMorseMode(true);
      setStatus("Morse input timed out");
    }
  }

  if (!_player.isPlaying() && !_morse_mode) {
    if (_alert_queued) {
      _alert_queued = false;
      _player.play(_alert_melody);
      _playing_alert = true;
      _gap_started = false;
    } else if (_alert_pending) {
      if (!_gap_started) {
        _gap_started = true;
        _alert_next = millis() + (unsigned long)the_mesh.getTapTap().repeat_gap_secs * 1000;
      } else if ((long)(millis() - _alert_next) >= 0) {
        _gap_started = false;
        _player.play(_alert_melody);
        _playing_alert = true;
      }
    } else {
      _playing_alert = false;
    }
  }

  if (_canned_mode && millis() - _canned_last > CANNED_TIMEOUT_MILLIS) {
    exitCannedMode(true);
    setStatus("Canned mode timed out");
  }

  if (!_morse_mode) userLedHandler();   // in Morse mode the LED follows the key

  if (_display != NULL && _display->isOn()) {
    static unsigned long next_refresh = 0;
    if (millis() >= next_refresh) {
      _display->startFrame();
      renderCurrScreen();
      _display->endFrame();
      next_refresh = millis() + 1000;
    }
    if (millis() > _auto_off) _display->turnOff();
  }
}
