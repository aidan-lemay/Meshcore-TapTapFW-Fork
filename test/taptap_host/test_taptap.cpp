// Host-side tests for the TapTap logic (no hardware needed).  Run: test/taptap_host/run.sh
#include "TapTap.h"
#include "TapPlayer.h"
#include "Button.h"
#include <stdio.h>
#include <assert.h>
#include <string>

unsigned long g_ms = 0; int g_freq = 0; int g_led = 0; int g_btn = 0;

static void test_taptap_and_player() {
  char buf[TAPTAP_CW_BUF_LEN];
  taptap::buildCwRtttl("sos hi", buf, sizeof buf); printf("CW: %s\n", buf);
  assert(taptap::buildCwRtttl("!!!", buf, sizeof buf)==0);
  char tiny[40]; int n=taptap::buildCwRtttl("hello world", tiny, sizeof tiny); printf("CW tiny(%d): %s\n", n, tiny); assert(n<40);
  TapTapSettings st; st.setDefaults(); assert(st.isValid());
  assert(st.buz_mode == TAP_BUZ_CW && st.repeat_alert == 1 && (st.led_flags & TAP_LED_NO_STANDBY) && st.slot_beep_ms == 200 && st.slot_flash_ms == 500);
  assert(taptap::parseCannedList(" hi | yes||no ", st)==3);
  printf("canned: [%s][%s][%s] count=%d\n", st.canned[0], st.canned[1], st.canned[2], st.canned_count);
  assert(taptap::parseCannedList(" | ", st)==0 && st.canned_count==3);
  assert(taptap::matchCanned("  HI ", st)==0); assert(taptap::matchCanned("no", st)==2); assert(taptap::matchCanned("hix", st)==-1);
  printf("strip: '%s'\n", taptap::stripSenderPrefix("Bob: hi"));
  char r[64]; bool tr;
  assert(taptap::normaliseRtttl("d=8,o=6,b=180:c,e,g", r, sizeof r, &tr)); printf("norm: %s\n", r);
  assert(taptap::normaliseRtttl("x:d=4,o=5,b=100:c", r, sizeof r, &tr) && !tr);
  assert(!taptap::normaliseRtttl("hello", r, sizeof r, &tr)); assert(!taptap::normaliseRtttl("a:b:c", r, sizeof r, &tr));
  char sm[24]; assert(taptap::normaliseRtttl("d=8,o=6,b=180:c,e,g,a,b,c,d", sm, sizeof sm, &tr) && tr); printf("trunc: %s\n", sm);
  for(int i=0;i<3;i++){ taptap::buildSlotBeeps(i, r, sizeof r); printf("beeps%d: %s\n", i, r);}
  for(int i=0;i<3;i++){ taptap::buildSlotFlashes(i, r, sizeof r); printf("flash%d: %s\n", i, r);}
  // player timing: CW "e" = one dit at b=480,d=8 -> 62ms note
  TapPlayer p; p.begin(25,37,24,true);
  taptap::buildCwRtttl("e", buf, sizeof buf); p.play(buf);
  printf("play e: playing=%d freq=%d led=%d\n", p.isPlaying(), g_freq, g_led); assert(p.isPlaying() && g_freq==1760 && g_led==1);
  g_ms=63; p.loop(); printf("after 63ms: playing=%d freq=%d led=%d\n", p.isPlaying(), g_freq, g_led); assert(!p.isPlaying() && g_freq==0 && g_led==0);
  p.setQuiet(true); g_ms=100; p.play("t:d=4,o=5,b=120:c,p,4a.");
  printf("quiet: freq=%d led=%d\n", g_freq, g_led); assert(g_freq==0 && g_led==1);
  g_ms=600; p.loop(); assert(g_led==0); g_ms=1100; p.loop(); assert(g_led==1); // pause 500ms then dotted a
  g_ms=1100+749; p.loop(); assert(p.isPlaying()); g_ms=1100+750; p.loop(); assert(!p.isPlaying());
  p.setQuiet(false); g_ms=2000; p.play("x:d=8,o=5,b=120:c#6"); printf("c#6=%d\n", g_freq); assert(g_freq==1108);
  { taptap::buildCwRtttl("sos hi", buf, sizeof buf); assert(strcmp(buf,"CW:d=8,o=6,b=475:a,p,a,p,a,4p.,4a.,p,4a.,p,4a.,4p.,a,p,a,p,a,2p.,p,a,p,a,p,a,p,a,4p.,a,p,a")==0);
    g_ms=5000; p.play(buf); int steps=0; while(p.isPlaying()){ g_ms+=1; p.loop(); steps++; } printf("sos hi plays %d ms\n", steps); }
  
  // slot 101, beeps 500ms, flashes 500ms: buzzer and LED run on separate timelines
  //  buzzer: 3 equal 500ms beeps (high, low, high) + 2*250 gaps = 2000ms, sounding 1500ms
  //  LED:    dash 500, gap 250, dot 250, gap 250, dash 500  = 1750ms, lit 1250ms
  {
    TapPlayer q; q.begin(25, 37, 24, true);
    char beeps[80], flashes[80];
    taptap::buildSlotBeeps(5, beeps, sizeof beeps, 500);
    taptap::buildSlotFlashes(5, flashes, sizeof flashes);
    g_ms = 20000; q.play(beeps, flashes);
    int lit = 0, sounding = 0, total = 0, led_off_at = -1, first = g_freq, low = 0;
    while (q.isPlaying()) {
      if (g_led) lit++;
      if (g_freq) sounding++;
      if (g_freq && g_freq < 1000) low++;
      if (!g_led && led_off_at < 0 && total > 1500) led_off_at = total;
      g_ms++; q.loop(); total++;
    }
    printf("slot split: total %d, sounding %d (low %d), lit %d, led done at %d\n", total, sounding, low, lit, led_off_at);
    assert(total == 2000 && sounding == 1500 && low == 500 && lit == 1250 && led_off_at == 1750 && first == 1760);
  }
  // independent settings: beeps 200 -> 3*200 + 2*100 = 800ms; flashes 200 on 000 -> 3*100 + 2*100 = 500ms
  {
    TapPlayer q; q.begin(25, 37, 24, true);
    char beeps[80], flashes[80];
    taptap::buildSlotBeeps(0, beeps, sizeof beeps, 200);
    taptap::buildSlotFlashes(0, flashes, sizeof flashes, 1000);
    g_ms = 25000; q.play(beeps, flashes);   // LED 000 at 1000: 3*500 + 2*500 = 2500ms
    int t = 0, lit = 0; while (q.isPlaying()) { if (g_led) lit++; g_ms++; q.loop(); t++; }
    printf("slot beeps 200 / flashes 1000: %dms, lit %d\n", t, lit); assert(t == 2500 && lit == 1500);
    taptap::buildSlotBeeps(0, beeps, sizeof beeps, 9999); assert(strstr(beeps, "b=300:"));   // out of range -> 200ms default
    taptap::buildSlotFlashes(0, flashes, sizeof flashes, 9999); assert(strstr(flashes, "b=120:"));   // -> 500ms default
    taptap::buildSlotFlashes(0, flashes, sizeof flashes, 1000);
    // muted: buzzer silent, LED still follows its own track
    q.setQuiet(true); g_ms = 30000; q.play(beeps, flashes);
    int heard = 0; lit = 0; while (q.isPlaying()) { if (g_freq) heard++; if (g_led) lit++; g_ms++; q.loop(); }
    assert(heard == 0 && lit == 1500); q.setQuiet(false);
  }
  // CW speed: 12 wpm -> 100ms dit; 2 wpm -> 600ms dit
  {
    TapPlayer q; q.begin(25, 37, 24, true);
    taptap::buildCwRtttl("e", buf, sizeof buf, 12); g_ms = 30000; q.play(buf);
    int t = 0; while (q.isPlaying()) { g_ms++; q.loop(); t++; } printf("12wpm dit %dms\n", t); assert(t == 100);
    taptap::buildCwRtttl("e", buf, sizeof buf, 2); g_ms = 40000; q.play(buf);
    t = 0; while (q.isPlaying()) { g_ms++; q.loop(); t++; } printf("2wpm dit %dms\n", t); assert(t == 600);
    assert(taptap::buildCwRtttl("e", buf, sizeof buf, 0) > 0 && strstr(buf, "b=475"));  // out of range -> default
  }
  // older (shorter) settings: fields past the end keep defaults; sanitise fixes junk
  {
    TapTapSettings s2; s2.setDefaults(); s2.cw_wpm = 0; s2.slot_beep_ms = 7; s2.slot_flash_ms = 0; s2.sanitise();
    assert(s2.cw_wpm == TAPTAP_CW_WPM_DEFAULT && s2.slot_beep_ms == TAPTAP_SLOT_BEEP_MS_DEFAULT && s2.slot_flash_ms == TAPTAP_SLOT_MS_DEFAULT);
  }

}

static std::string ev;
static Button b(6, HIGH);
static void run(unsigned long ms) { for (unsigned long t = 0; t < ms; t++) { g_ms++; b.update(); } }
static void press(unsigned long hold, unsigned long gap) { g_btn = 1; run(hold); g_btn = 0; run(gap); }

static void test_button_and_morse() {
  b.begin();
  b.onShortPress([]{ev+="S";}); b.onDoublePress([]{ev+="D";}); b.onTriplePress([]{ev+="T";});
  b.onQuadruplePress([]{ev+="Q";}); b.onLongPress([]{ev+="L";}); b.onVeryLongPress([]{ev+="V";});
  b.onRawDown([]{ev+="v";}); b.onRawUp([](uint32_t ms){ ev+= ms>=250?"-":"."; });
  press(100,800); assert(ev=="S"); ev="";
  press(100,150); press(100,800); assert(ev=="D"); ev="";
  for(int i=0;i<3;i++) { press(100,150); }
  run(800); assert(ev=="T"); ev="";
  for(int i=0;i<4;i++) { press(100,150); }
  run(800); assert(ev=="Q"); ev="";
  // long fires while held at ~1.2s, before release
  g_btn=1; run(1300); assert(ev=="L"); g_btn=0; run(800); assert(ev=="L"); ev="";
  // hold 6s: L then V, release ignored
  g_btn=1; run(6000); assert(ev=="LV"); g_btn=0; run(800); assert(ev=="LV"); ev="";
  // raw mode: dit dah dit, then hold to send
  b.setRawMode(true);
  press(100,200); press(400,200); press(100,800);
  printf("raw: %s\n", ev.c_str()); assert(ev=="v.v-v."); ev="";
  g_btn=1; run(1300); g_btn=0; run(100); assert(ev=="vL"); ev="";
  b.setRawMode(false); press(100,800); assert(ev=="S");
  assert(taptap::decodeMorse(".-")=='A' && taptap::decodeMorse("...")=='S' && taptap::decodeMorse("-----")=='0');
  assert(taptap::decodeMorse(".-.-.-")=='.' && taptap::decodeMorse("..--..")=='?' && taptap::decodeMorse("......")==0);
  // round trip every char via the CW table
  for(const char* c="ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789"; *c; c++){ char buf[200]; char s[2]={*c,0}; taptap::buildCwRtttl(s,buf,sizeof buf);
    std::string pat; for(const char* p=strchr(strchr(buf,':')+1,':')+1; *p; ){ if(!strncmp(p,"4a.",3)){pat+='-';p+=3;} else if(*p=='a'){pat+='.';p++;} else p++; }
    assert(taptap::decodeMorse(pat.c_str())==*c); }
  
}

int main() {
  test_taptap_and_player();
  test_button_and_morse();
  puts("ALL TAPTAP HOST TESTS PASSED");
  return 0;
}
