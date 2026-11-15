# MeshCore TapTap Edition (unofficial)

Headless, one-button messaging firmware for the **Seeed Studio T1000-E**, built on
[MeshCore](https://github.com/meshcore-dev/MeshCore) companion radio **v1.18.0**.

The T1000-E has no screen, so everything is done with the single button, the buzzer and the
green LED: send canned messages, key in free text with Morse code, and get incoming
messages played back as tunes, Morse (CW) or slot patterns. The LED shows every sound, so the
device also works muted.

This is an independent, clean re-implementation of the TapTap feature set on current upstream
MeshCore, plus extra features (see [What's different from TapTap](#whats-different-from-taptap)).
It is not affiliated with the TapTap or MeshCore projects.

It still works as a normal MeshCore BLE companion. Pair it with the MeshCore app as usual
(default BLE PIN `123456`).

---

## Contents

- [Install](#install)
- [Button controls](#button-controls)
- [Sending: canned messages](#sending-canned-messages)
- [Sending: Morse input](#sending-morse-input)
- [Receiving: alerts](#receiving-alerts)
- [Commands](#commands)
- [Sounds and LED patterns](#sounds-and-led-patterns)
- [Building from source](#building-from-source)
- [How it's put together](#how-its-put-together)
- [What's different from TapTap](#whats-different-from-taptap)
- [Known limitations](#known-limitations)
- [License and credits](#license-and-credits)

---

## Install

1. Download `t1000e_taptap_ble-<version>.uf2` from [`release/`](release/) (or the Releases page).
2. Connect the T1000-E by USB and **quickly double-press the button** to enter the bootloader.
   A USB drive called `T1000-E` appears.
3. Copy the `.uf2` onto the drive. The device reboots into the new firmware and plays the startup tune.

Your contacts, channels and radio settings are kept when you move between this and stock MeshCore
companion firmware (they use the same layout).
Settings from the original TapTap firmware are **not** imported. Set them again with
the [commands](#commands).

To go back to stock firmware, flash the official MeshCore T1000-E companion `.uf2` the same way.

---

## Button controls

| Gesture | Normal | Canned mode | Morse mode |
|---|---|---|---|
| **1 click** | Send advert. If an alert is pending: **acknowledge** it | Next canned message | *(keying: see below)* |
| **2 clicks** | Enter **canned mode** | Switch to **Morse mode** | *(keying)* |
| **3 clicks** | **Replay** the last received message | Replay | *(keying)* |
| **4 clicks** | **Mute / unmute** buzzer | Mute / unmute | *(keying)* |
| **Hold 1.2 s** | (first 8 s after boot: USB CLI rescue) | **Send** selected message | **Send** composed message |
| **Hold 5 s** | Power off | Power off | Power off |

- GPS is turned on and off **from the app** (the device's GPS setting), not the button.
- The 1.2 s hold fires **as soon as 1.2 s is reached**. You don't need to let go, and letting go afterwards does nothing.
- Canned mode exits after **15 s** with no clicks. Morse mode exits after **10 s** with no keying.
- **Holding to power off while in canned mode also sends the selected message**, because the
  1.2 s hold fires first. Leave canned mode (2 clicks, or wait 15 s) before powering off.

---

## Sending: canned messages

There are up to **8** preset messages. The defaults are:

| Slot | Pattern | Buzzer | LED | Default text |
|---|---|---|---|---|
| 1 | `000` | low low low | · · · | All good |
| 2 | `001` | low low high | · · — | Stop and wait |
| 3 | `010` | low high low | · — · | Need assistance |
| 4 | `011` | low high high | · — — | Emergency, send help |
| 5 | `100` | high low low | — · · | Found water |
| 6 | `101` | high low high | — · — | Returning to camp |
| 7 | `110` | high high low | — — · | Running behind |
| 8 | `111` | high high high | — — — | Found the point |

1. **2 clicks** to enter canned mode (rising three-note chirp).
2. **1 click** steps to the next message. The device shows the slot number as 3 binary digits,
   with the buzzer and the LED each using their own timing:
   - **Buzzer:** equal-length beeps, **high = 1, low = 0** (0.5 s each, 0.25 s gaps by default)
   - **LED:** **1 = dash** (0.5 s), **0 = dot** (0.25 s), dot-length gaps, so it can be read with the buzzer muted

   So slot 6 (`101`) sounds high-low-high and flashes dash-dot-dash. The two start together, but each
   follows its own timing, so they don't stay in step. Change them separately with
   `/slot beep <ms>` and `/slot flash <ms>` (or both at once with `/slot <ms>`).
3. **Hold 1.2 s** to send it to the [tap target](#commands). A rising "sent" tune means it was sent.
   A double low buzz means it failed (for example, no tap target set).

Change the messages from the app with `/msg`.

---

## Sending: Morse input

Type any free-text message by keying Morse code on the button.

1. **2 clicks** (canned mode), then **2 clicks** again. Two short high beeps mean you're in Morse mode.
2. Key the message. A side-tone sounds and the LED lights while the button is held:
   - **short press (< 250 ms)** = dit `.`
   - **long press (250 ms – 1.2 s)** = dah `-`
   - **pause 0.7 s** = end of letter
   - **pause 2 s** = space between words
   - **8 or more dits** (`........`, the Morse "error" sign) = **delete the last letter**
   - an unrecognised letter gives a double low buzz and is dropped
3. **Hold 1.2 s** to send. Messages are sent in CAPITALS, up to 63 characters.

Supported characters: `A–Z`, `0–9`, `.` `,` `/` `?`.

---

## Receiving: alerts

When a direct message or channel message arrives, the device alerts in the current
**buzzer mode** (`/buz`):

| Mode | What you hear |
|---|---|
| `rtttl` *(default)* | Your custom ringtone (`/rtttl`), or the built-in message chirp |
| `cw` | The message text in **Morse code** (19 WPM by default, adjustable with `/cw`; the sender name is not played) |
| `off` | No alert |

In `rtttl` and `cw` modes, a message that **matches one of your canned messages** (ignoring case and
surrounding spaces) plays that slot's **3-bit pattern** instead, the same one you hear and see when picking
it with the button. So "All good" is low-low-low with three short flashes. The spelled-out Morse speed
(`/cw`) and the slot pattern timing (`/slot`) are separate settings.

- **Alerts play even while the app is connected.**
- **The LED flashes with every note** (for slot patterns, with its own dot/dash timing), so alerts are visible when muted (4 clicks).
- **3 clicks replays the last received message**, at any time. Replay always plays the *content*,
  whatever `/buz` is set to: the slot pattern if it matches a canned message, otherwise Morse.
  It doesn't start a repeat-until-ack cycle. If nothing has been received since power-on, you get the error buzz.
- **Repeat until acknowledged** (`/rep on`): the alert repeats every *N* seconds (default 5)
  until you **single-click**. Reading the message in the app doesn't stop it, because the app downloads
  messages as soon as they arrive.
- If the same text arrives again within 30 s (resends, or the same message on several routes), it only alerts once.
- If a new message arrives while one is still playing, the newest plays straight after. Incoming
  alerts are held while you're keying Morse and play when you leave Morse mode.

---

## Commands

Type these as a normal message **in the MeshCore app** (in any channel, or in a direct-message chat).
The device handles them itself and never sends them over the mesh. Replies show up in the same
chat from **"System"**. Anything starting with `/` that isn't listed here is sent as a normal message.

| Command | What it does |
|---|---|
| `/help` | List commands |
| `/msg` or `/msg ?` | List the canned messages |
| `/msg First\|Second\|Third` | Replace the canned messages (1–8 items separated by `\|`, max 63 chars each) |
| `/tap` | Show the current tap target (where button-sent messages go) |
| `/tap here` | Make **this channel**, or **this contact** if typed in a direct chat, the tap target |
| `/buz` | Show the buzzer mode |
| `/buz rtttl\|cw\|off` | Set the alert style ([Receiving](#receiving-alerts)) |
| `/rtttl` | Show the custom ringtone (`/ringtone` also works) |
| `/rtttl <rtttl>` | Set the ringtone, e.g. `/rtttl d=8,o=6,b=180:c,e,g` or a full `Name:d=4,o=5,b=120:…` |
| `/rtttl clear` | Go back to the built-in tones |
| `/rep` | Show repeat-until-ack status |
| `/rep on\|off` | Turn repeat-until-ack on or off |
| `/rep <secs>` | Turn it on with a gap of 1–255 s between repeats |
| `/play <rtttl>` | Play a melody on the device now (handy for trying ringtones) |
| `/cw` | Show the CW alert speed |
| `/cw <wpm>` | Set the CW alert speed, 1–40 words per minute (dit = 1200 / wpm ms: 19 → 63 ms, 12 → 100 ms, 2 → 600 ms) |
| `/slot` | Show the slot pattern timing |
| `/slot beep <ms>` | Buzzer beep length for the slot pattern, 50–2000 ms (all beeps equal; gaps are half). Default 500 |
| `/slot flash <ms>` | LED **dash** length for the slot pattern, 50–2000 ms (dots and gaps are half). Default 500 |
| `/slot <ms>` | Set both at once. Used both when picking a canned message and when an incoming message matches one |
| `/led` | Show LED settings |
| `/led standby on\|off` | Turn the idle heartbeat / messages-waiting blink on or off. When off, the LED **only** lights for tones, alerts and Morse keying, so it's never confused with a message |

The default tap target is the **Public** channel. A channel target follows the channel if you
re-order channels in the app. If the channel or contact is deleted, sending fails with the error buzz.

All of these settings are saved on the device and survive reboots.

---

## Sounds and LED patterns

| Event | Sound |
|---|---|
| Startup | rising C-E-G |
| Advert sent | single low beep |
| Enter canned mode | quick rising chirp |
| Leave canned / Morse mode | quick falling chirp |
| Enter Morse mode | two short high beeps |
| Slot selected | 3-bit pattern: buzzer high = 1 / low = 0; LED dash = 1 / dot = 0 (`/slot`) |
| Message sent | rising "sent" tune |
| Error / failed | double low buzz |
| Morse letter deleted | single low blip |
| Unmute / mute | up / down two-note (mute plays on the LED only) |
| Acknowledge alert | very short high tick |
| Power off | falling three notes |

The green LED lights for every note above (the slot pattern flashes on its own dot/dash timing). When nothing is playing it shows a heartbeat:
a short blink every 4 s, or a longer blink when messages are waiting or an alert is pending.
Turn the heartbeat off with `/led standby off` if it gets in the way of reading messages on the LED.

**Charging light:** the firmware never drives the LED for charging (the charge-detect pin is not
used). Any light you see while charging comes from the device's charging circuit, so it can't be
turned off from firmware.

---

## Building from source

Requirements: Python 3 and [PlatformIO Core](https://platformio.org/install/cli)
(`pip install platformio`).

```bash
./build-taptap.sh
# -> release/t1000e_taptap_ble-<version>.uf2   (drag-and-drop flashing)
# -> release/t1000e_taptap_ble-<version>-ota.zip (nRF DFU / OTA)
```

Or by hand:

```bash
pio run -e t1000e_taptap_ble
python3 bin/uf2conv/uf2conv.py .pio/build/t1000e_taptap_ble/firmware.hex -c -f 0xADA52840 -o firmware.uf2
```

The version string is set by `FIRMWARE_VERSION` in the `t1000e_taptap_ble` section of
[`variants/t1000-e/platformio.ini`](variants/t1000-e/platformio.ini).

### Tests

The core logic (Morse encode/decode, canned parsing and matching, RTTTL handling, the melody/LED
player and the button state machine) has host-side tests that run on a PC without hardware:

```bash
test/taptap_host/run.sh
```

---

## How it's put together

All changes are additive and switched on by the `TAPTAP_FEATURES` build flag. Every other
MeshCore build target compiles exactly as upstream.

| Path | Role |
|---|---|
| [`examples/companion_radio/TapTap.h/.cpp`](examples/companion_radio/TapTap.h) | Settings struct, Morse encoder/decoder, canned-list parsing/matching, RTTTL helpers |
| [`examples/companion_radio/ui-taptap/UITask.*`](examples/companion_radio/ui-taptap/UITask.cpp) | Button modes, canned + Morse input, alert logic, repeat-until-ack, LED heartbeat |
| [`examples/companion_radio/ui-taptap/TapPlayer.*`](examples/companion_radio/ui-taptap/TapPlayer.cpp) | Non-blocking RTTTL player that drives the buzzer **and** mirrors notes on the LED |
| [`examples/companion_radio/ui-taptap/Button.*`](examples/companion_radio/ui-taptap/Button.cpp) | Button driver: multi-click, 1.2 s / 5 s holds, raw keying mode for Morse |
| [`examples/companion_radio/MyMesh.*`](examples/companion_radio/MyMesh.cpp) | Catches `/commands` in outgoing app messages, "System" replies, sending to the tap target |
| [`examples/companion_radio/DataStore.*`](examples/companion_radio/DataStore.cpp) | Small `loadFile` / `saveFile` helpers |
| [`variants/t1000-e/platformio.ini`](variants/t1000-e/platformio.ini) | The `t1000e_taptap_ble` build environment |
| [`test/taptap_host/`](test/taptap_host/) | Host tests |

TapTap settings live in `/taptap` on the device's internal flash. If the file is missing or from
an incompatible version, it's replaced with the defaults.

To update to a newer MeshCore:

```bash
git remote add upstream https://github.com/meshcore-dev/MeshCore.git   # once
git fetch upstream
git merge upstream/main
```

---

## What's different from TapTap

New in this edition:

- **Canned-slot alerts**: incoming messages that match a canned message play the slot pattern instead of the ringtone or full text.
- **Replay the last message on 3 clicks** (GPS moved to the app).
- **Separate buzzer and LED timing** for the slot pattern (`/slot beep`, `/slot flash`).
- **Mute/unmute on 4 clicks**, saved across reboots.
- **LED mirrors all sounds**, so it works silently.
- **Repeat-until-acknowledged alerts** (`/rep`).
- **Alerts while the app is connected**.
- `/rep`, `/rtttl clear`, `/msg ?`, and channel targets that follow re-ordered channels.
- Built on MeshCore **v1.18.0**.

Behaviour kept from TapTap: the button map for advert, canned mode, Morse mode and power off; the
binary slot beeps; the tones; the commands; Morse alerts; the default canned messages.

Not included: the LED blinking the current time in Morse after an advert, and TapTap's Easter
egg (4 clicks is now mute).

---

## Known limitations

- Built for the T1000-E only. Other boards (e.g. WisMesh Tag) need their own build environment in their `variants/*/platformio.ini`.
- Messages sent with the button don't appear in the app's chat history.
- Morse alerts play letters, digits and `. , / ?`. Other characters are skipped. Very long
  messages are cut at about 640 characters of melody (around 60–90 letters).
- The charging light can't be turned off (see [Sounds and LED patterns](#sounds-and-led-patterns)).
- The original TapTap settings files aren't read. Set your messages and options again with the commands.

---

## License and credits

MIT, the same as MeshCore. See [`license.txt`](license.txt).

- [MeshCore](https://github.com/meshcore-dev/MeshCore) by Scott Powell / rippleradios.com and contributors.
  The original project README is kept as [`README.MeshCore.md`](README.MeshCore.md).
- The feature set is modelled on the TapTap firmware for MeshCore trackers. This is an independent
  implementation and is not endorsed by its authors.
