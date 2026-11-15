// Minimal Arduino stand-in so the TapTap logic can be unit-tested on a PC.
#pragma once
#include <stdint.h>
#include <string.h>
#include <strings.h>
#define HIGH 1
#define LOW 0
#define OUTPUT 1
extern unsigned long g_ms;   // simulated clock
extern int g_freq;           // last tone() frequency, 0 = silent
extern int g_led;            // LED pin (24) level
extern int g_btn;            // button pin level
inline unsigned long millis() { return g_ms; }
inline void pinMode(int, int) {}
inline void digitalWrite(int p, int v) { if (p == 24) g_led = v; }
inline int digitalRead(int) { return g_btn; }
inline int analogRead(int) { return 1023; }
inline void tone(int, unsigned f) { g_freq = f; }
inline void noTone(int) { g_freq = 0; }
