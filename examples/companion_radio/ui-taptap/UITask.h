#pragma once

// TapTap headless UI: one button + piezo + LED. See TapTap.h for the feature overview.

#include <MeshCore.h>
#include <helpers/ui/DisplayDriver.h>
#include <helpers/SensorManager.h>
#include <stddef.h>

#include "../AbstractUITask.h"
#include "../NodePrefs.h"
#include "../TapTap.h"

#include "Button.h"
#include "TapPlayer.h"

class UITask : public AbstractUITask {
  DisplayDriver* _display;
  SensorManager* _sensors;
  NodePrefs* _node_prefs;
  TapPlayer _player;

  unsigned long _auto_off;
  unsigned long ui_started_at;
  char _status[80];     // last status line (shown on a display, if any; also logged)
  int _msgcount;

  // canned message mode
  bool _canned_mode = false;
  int8_t _canned_idx = -1;
  unsigned long _canned_last = 0;

  // Morse input mode
  bool _morse_mode = false;
  char _morse_text[TAPTAP_CANNED_LEN];
  char _morse_sym[10];
  unsigned long _morse_last = 0;      // last key activity (press or release)
  bool _key_down = false;

  // incoming-message alert (+ repeat-until-ack)
  char _alert_melody[TAPTAP_CW_BUF_LEN];
  char _alert_led[96];             // separate LED track (slot pattern), if _alert_split
  bool _alert_split = false;

  // last received message, for replay (3 clicks)
  char _last_msg[MAX_TEXT_LEN + 1] = {0};
  bool _last_is_channel = false;
  char _replay_melody[TAPTAP_CW_BUF_LEN];
  char _replay_led[96];
  bool _alert_queued = false;      // play _alert_melody as soon as the player is free
  bool _alert_pending = false;     // repeat until acknowledged
  bool _playing_alert = false;     // current playback is the alert (vs. a UI tone)
  bool _gap_started = false;
  unsigned long _alert_next = 0;

  // de-dupe: same text heard again shortly (resends / multiple routes) doesn't re-alert
  uint32_t _recent_hash[4] = {0};
  unsigned long _recent_at[4] = {0};
  uint8_t _recent_pos = 0;

#ifdef PIN_USER_BTN
  Button* _userButton = nullptr;
#endif

  void playTone(const char* rtttl);                  // UI feedback tone (doesn't cancel a pending alert)
  void alertIncoming(const char* body, bool is_channel);
  // builds the alert for 'body'; content_only = always slot pattern / CW (ignore ringtone + buzzer mode).
  // Returns true if 'led' holds a separate LED track.
  bool buildAlert(const char* body, bool is_channel, bool content_only, char* mel, size_t mel_len, char* led, size_t led_len);
  void playAlert();
  void ackAlert();
  bool recentlyAlerted(uint32_t h);
  void exitCannedMode(bool with_tone);
  void enterMorseMode();
  void exitMorseMode(bool with_tone);
  void morseKeyDown();
  void morseKeyUp(uint32_t held_ms);
  void morseFlushSymbol();
  void morseSend();
  void setStatus(const char* fmt, ...);
  void userLedHandler();
  void renderCurrScreen();

  void handleButtonAnyPress();
  void handleButtonShortPress();
  void handleButtonDoublePress();
  void handleButtonTriplePress();
  void handleButtonQuadruplePress();
  void handleButtonLongPress();
  void handleButtonVeryLongPress();

public:
  UITask(mesh::MainBoard* board, MultiSerialInterface* serial) : AbstractUITask(board, serial), _display(NULL), _sensors(NULL) {
    ui_started_at = 0;
    _status[0] = 0;
    _msgcount = 0;
  }
  void begin(DisplayDriver* display, SensorManager* sensors, NodePrefs* node_prefs);
  bool hasDisplay() const { return _display != NULL; }

  // MyMesh::Listener
  void onMessageRecv(mesh::Packet *pkt, const ContactInfo &from, uint8_t txt_type, uint32_t sender_timestamp, const char* text) override;
  void onChannelMessageRecv(mesh::Packet *pkt, ChannelDetails& channel_details, const char* text) override;
  void onQueueSizeChanged(int offline_queue_size) override;
  bool onPlayMelody(const char* rtttl) override;

  // from AbstractUITask
  void notify(UIEventType t = UIEventType::none) override;
  void loop() override;

  void shutdown(bool restart = false);
};
