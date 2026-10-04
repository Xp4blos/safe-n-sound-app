# Ambient engine integration and functional spec (Design)

Date: 2026-10-03
Status: approved by the user's message of 2026-10-03 (switch engine, clickable mic, "Teach a sound" button, keep the functional spec F1-F9)
Supersedes the engine, fingerprint and matching parts of `2026-10-03-sound-detection-history-design.md`. The app shell, storage, alerts and tabs stay.

## Engine switch

The teammate's C++ engine (`Xp4blos/hack-yeah-2026`, commit 512e41d) replaces the first engine for detection and matching.
It is vendored in `entry/src/main/cpp/ambient/` without `speech.*` (speech is out of scope). It provides:

- **Detector:** alarm events (sustained narrow-band tone, 800-4500 Hz, one event per alarm) and, once custom sounds are registered, `Custom` events with the matched sound's label.
- **Trainer:** `train_custom_sound` builds a level-invariant 24-band spectrogram template from 1..N recordings.
- **Custom sound serialisation:** stored with a CRC.

Only `Alarm` and `Custom` events are surfaced. `Knock` and `LoudSound` are ignored: the phone's own vibration is classed as LoudSound, and room hum produces no event.

A thin C++ wrapper (`SoundEngine`) adds what the app needs:

| Wrapper feature | Purpose |
| --- | --- |
| Ring buffer of the last 10 s of PCM (memory only) | auto-learning from a detected sound |
| 16-band live spectrum per processed chunk | the equaliser view |
| `SoundProfile` | plain-words description: dominant Hz, duration, beep count and rate, single / repeated / continuous, steady / pulsed / sweeping, 8-point envelope |

The first engine's FFT and frame analyzer are kept for the live spectrum and the profile; its detector, fingerprint and matcher are removed.

## Remembering sounds (F3-F5)

1. An `Alarm` event that no custom sound explains creates an unnamed history entry ("Unknown sound", count 1) with a pending template.
2. About 3.7 s later, `learnSound` trains a template from the audio window [t-2 s, t+3.5 s] held in the ring buffer. The template (spectrogram numbers, not audio) and the profile are stored in the entry and registered in the engine. If training fails, the entry is removed.
3. Later occurrences arrive as `Custom` events for that entry: count and time are updated. Matching is level-invariant, so distance and volume changes still match.
4. When an unnamed sound is heard for the 2nd time, the user is asked "I've heard this sound before. Do you want to name it?" (system notification and in-app dialog with Name it / Not now / Don't ask again for this sound). No repeat prompt for the same sound within 10 minutes.
5. Raw audio is never stored or sent. Only templates and profile numbers persist (Preferences JSON).

## Teaching (F6)

"Teach a sound" on the Listen screen opens a dialog. The user records up to 3 takes (6 s each); every take shows captured / not usable. With 2 or more good takes the user types a name and saves; the sound is immediately named. If takes differ too much the dialog says so and asks to repeat. Teaching suspends detection so it creates no history entries.

## Alerts (F7)

- A named sound with alerts on: distinct long vibration (3 x 500 ms), notification "<Name> detected" with the time, and a card on the Listen screen. 15 s cooldown per sound, while the card keeps showing "still ongoing" as long as the sound repeats.
- Unknown sounds: one short vibration.
- Every state is visible on screen; nothing relies on audio.

## UI

- Tabs: Listen, History, My sounds.
- Listen: the mic circle is a button (start/stop, same as the Start/Stop button), a "Teach a sound" button, an 8 s live spectrum (frequency bars over time, detections highlighted), last-detected / alert card, error and permission states.
- History: chronological entries with name or "Unknown sound", time, count and a small envelope visual; tapping opens details with the plain-words summary, name field, alert switch, rename and delete.
- My sounds: named sounds with alert switch, rename and delete.
- First microphone request explains privacy; after a denial the Settings page is offered.

## Out of scope

Keyword detection, background listening, knock and loud-sound classes.

## Testing

- C++ (host): the teammate's own suite, plus wrapper tests: learn then recognise a repeated beep, hum and 200 Hz motor bursts give no events, teaching with consistent and inconsistent takes, profile words.
- ArkTS (local): catalog (occurrence, alert cooldown 15 s, prompt rules, naming, deletion, tolerant parsing, plain-words summary), pipeline with fakes, spectrum history.
- Device: acceptance steps 1-7 of the functional spec; each reported as verified or not.
