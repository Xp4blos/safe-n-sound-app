# Safe'n'Sound - technical overview

Safe'n'Sound turns the sounds around a deaf or hard-of-hearing person into things they can see and feel: a live
chart, an on-screen card, vibration and system notifications. Everything runs on the phone; audio is analysed in
memory and never stored or sent anywhere.

This document explains how every function works, what it is built with, what it stands on and which physical
phenomena it relies on. It is written to be the base of a technical presentation.

## 1. Technology stack

| Layer | Technology |
|---|---|
| Platform | HarmonyOS (OpenHarmony), API 20, phone |
| UI | ArkTS (strict mode) with ArkUI, declarative components, Canvas for the chart, `SymbolGlyph` icons |
| Signal processing | C++17 engine, compiled with CMake into `libsafensound.so`, called from ArkTS through N-API (Node-API for OpenHarmony) |
| Build | `hvigor` (DevEco Studio toolchain), CMake + Ninja from the DevEco SDK for the native part |
| Storage | `@kit.ArkData` Preferences (JSON): learned sounds, settings, keyword rules |
| Tests | C++ host tests (MSVC on the PC), ArkTS unit tests (Hypium), tests on a real phone driven with `hdc` |

### Platform services used (HarmonyOS kits)

| Kit | Used for |
|---|---|
| AudioKit `AudioCapturer` | microphone capture, 16 kHz mono 16-bit PCM |
| SensorServiceKit `vibrator` | vibration patterns |
| NotificationKit | local system notifications |
| BackgroundTasksKit + AbilityKit `wantAgent` | keeping the microphone alive when the app is not on screen (continuous task `AUDIO_RECORDING`) |
| CoreSpeechKit (`speechRecognizer`, an HMS service) | speech to text, on the device |
| ArkData `preferences` | persistence |
| AbilityKit `abilityAccessCtrl` | microphone permission |

Permissions requested: `MICROPHONE` (usage scene "always", needed for background listening), `VIBRATE`,
`KEEP_BACKGROUND_RUNNING`. Notification permission is requested at run time.

### External libraries

There are **no third-party libraries**. The sound engine (FFT, detectors, template matching, trainer, keyword
engine) is the team's own C++ code, kept in `entry/src/main/cpp/ambient/` (it comes from the team's sound-engine
repositories and was extended during the merge). The FFT is a small radix-2 implementation written for the project.
The only external dependencies are the platform kits listed above.

## 2. Architecture and data flow

```
microphone ──AudioCapturer──▶ AudioService ──PCM chunks──▶ NativeEngine (N-API) ──▶ libsafensound.so
 (16 kHz, 16-bit, mono)            │                                   │
                                   │                                   ├─ ring buffer (10 s, memory only)
                                   │                                   ├─ 16-band live spectrum for the chart
                                   │                                   └─ ambient engine: events
                                   │                                          alarm, chirp, siren, scream, cry,
                                   │                                          knock, loud_sound, custom (learned)
                                   ▼
                              SoundPipeline (ArkTS, pure logic, unit tested)
                                   │  decides: unknown sound? learned sound? class? alert? prompt? duplicate?
                                   ├──▶ SoundCatalog ──▶ SoundStore (Preferences)      history, My sounds
                                   ├──▶ AlertService ──▶ vibrator, notifications       alerts
                                   └──▶ UI listener  ──▶ Listen / History / My sounds   cards, chart marks
PCM chunks ──▶ SystemRecognizer (CoreSpeechKit) ──text──▶ native keyword pipeline ──▶ CaptionBoard ──▶ Captions tab
                                                                                       └─▶ alerts (vibration, notification)
```

Design rules that keep it testable:

* The pipeline, catalog, settings and caption board are plain ArkTS classes with narrow "ports" (interfaces) for the
  engine, alerts and storage. Tests replace the ports with fakes, so the logic is verified without a phone.
* The native module is thin: argument checking and conversion only. All DSP lives in the C++ libraries.
* Every failure of an alert or of storage is logged and swallowed: a problem with a notification must never stop the
  microphone.

## 3. The physics and the signal processing

### 3.1 From air pressure to numbers

Sound is a pressure wave. The phone's MEMS microphone converts the pressure into a voltage, the audio chip samples
it and the app receives **16 000 samples per second, 16 bits each**. By the Nyquist theorem this represents
frequencies up to 8 kHz, which covers alarms, beeps, sirens and the voice.

The engine works on short **frames** of 1024 samples (64 ms) that overlap by half (a new frame every 32 ms). A frame is
multiplied by a **Hann window** (to avoid spectral leakage at the frame edges) and transformed with an **FFT** into a
spectrum. One FFT bin is 16 000 / 1024 = 15.6 Hz wide. From the spectrum the engine takes:

* the **level** in dBFS (decibels relative to full scale; 0 dB is the loudest possible digital sample),
* the **dominant frequency** and how much of the power sits in a narrow band around it (**tonality**),
* a **band spectrogram**: the energy in 24 logarithmically spaced bands (log spacing follows how pitch is perceived).

A running estimate of the **background level** (exponentially smoothed) lets the engine talk about "how far above the
room noise" a sound is, so detection adapts to a quiet bedroom or a noisy street.

### 3.2 What each detector listens for

| Class | Physical phenomenon | Detection rule (defaults of the engine) |
|---|---|---|
| **Alarm** (smoke alarm, appliance beep, door buzzer) | A resonating buzzer or piezo element produces an almost pure tone: all the energy is concentrated in one narrow frequency band. | At least 50 % of the frame power within +-3 bins of the peak, peak between 800 and 4500 Hz, lasting at least 0.4 s (a few non-tonal frames are tolerated). |
| **Chirp** (low-battery chirp, short appliance beep) | The same narrow-band tone, but short. | 50-350 ms, pitch drift below 10 %; a repeat at the same pitch 5-120 s later raises the confidence. |
| **Siren** | A horn driven with a sweeping frequency: the pitch rises and falls. | Tonal sound between 400 and 2200 Hz lasting at least 2 s whose pitch sweeps by at least 20 % and turns around (wail, yelp, hi-lo all qualify). A steady tone stays an alarm. |
| **Baby crying** | A voice is a *harmonic* sound: vocal folds vibrate at a fundamental f0 and the vocal tract shapes integer multiples of it. A baby's f0 is high (300-700 Hz) and moves during each wail. | Harmonic analysis finds f0 in 300-700 Hz with at least 3 harmonics carrying at least 35 % of the power (a sub-harmonic check rejects adult voices); pulses of at least 0.35 s with moving pitch; two pulses within 2 s make one episode. |
| **Scream** | A loud, rough, high-pitched voice: harmonic but with noisy, irregular excitation. | f0 700-1800 Hz, at least -35 dBFS and 15 dB above the background at the onset, harmonic share 35-85 % (a pure whistle or siren is more tonal and is excluded), at least 0.4 s, pitch variation at least 4 %. This is the least reliable class (heuristic, tuned on synthetic audio). |
| **Knock** | An impact excites the structure impulsively: a broadband burst that decays within a few tens of milliseconds. | Level rises at least 15 dB within one hop (impulsive), at least 12 dB over the background, decays 10 dB within 0.25 s. |
| **Loud sound** | Sustained broadband energy (a crash, a shout, machinery). | Sustained onset above the background and above -35 dBFS, 1.5 s refractory time; voiced speech is recognised by its harmonics (f0 80-320 Hz) and ignored. |

Knock and loud sound are level-only detectors, so they are the ones that can be fooled by the phone itself: see the
self-noise guard in section 4.

### 3.3 Learning and recognising a sound (fingerprint matching)

Alarms are not all the same, and a user also needs the doorbell, the washing machine or a microwave. The app therefore
learns sounds.

1. **Fingerprint.** Every frame is reduced to a vector of 24 band energies in dB. A sound is a matrix of such vectors
   (time x frequency): a small spectrogram.
2. **Level invariance.** A window is normalised: values below (peak - 30 dB) are clamped (treated as silence), the mean
   is subtracted and the vector is scaled to unit length. After this, the same sound heard louder or quieter produces the
   same numbers, and only its *shape* in time and frequency remains.
3. **Similarity.** The similarity of a window to a stored template is the mean of two correlations: of the spectral shape
   and of the loudness envelope (how the volume rises and falls: the rhythm). It is 1 for identical shape. The app fires
   when the similarity reaches **0.60** (the engine's default of 0.8 missed quieter real repeats on a phone).
4. **Streaming.** The matcher slides over the live frames; a hit needs a loud enough frame in the window, and a
   refractory time of 2 s avoids repeated hits.
5. **Learning from one occurrence.** When the engine reports an unknown alarm, the app keeps listening for 3.5 s, then
   cuts the window from 2 s before to 3.5 s after the event out of a **10-second ring buffer held in memory** and trains
   a template from it. The audio is discarded; only the numbers are kept.
6. **Teach a sound on purpose.** The user records 2 or 3 takes of 6 seconds. Each take is first reduced to its clearest
   tonal stretch (frames that are tonal, in 600-6000 Hz and at least 8 dB above the background; everything else is
   silenced), so a noisy room does not change where the sound starts and ends. The trainer cuts each take by loudness,
   stretches them in time to the same length, and requires that they resemble each other (pairwise similarity at least
   0.55). With three takes one odd take is left out; two takes that disagree are rejected. The template is the average of
   the takes, and the trainer checks that it recognises its own recordings, lowering the threshold in small steps if
   needed.
7. **Storage.** A template is a few kilobytes of floats, serialised with a CRC32 and stored as base64 in Preferences.

How well this works was measured, not assumed: on 25 real recordings (five multi-note signals, five plays each, played
from a PC speaker in a noisy room and recorded by the phone), teaching from two takes succeeded for 46 of 50 pairs and
from three takes for 50 of 50; pairs of *different* sounds were accepted 0 times of 50; after teaching from three plays
each, 25 of 25 plays were recognised under the right name.

### 3.4 The sound profile (plain words)

Besides the template, each learned sound gets a profile computed from 32 ms frames: dominant frequency, duration, number
of beeps and their rate, whether it is single, repeated or continuous, whether the pitch is steady, pulsed or sweeping,
and an 8-point loudness envelope (drawn as the small bars in History). The profile is turned into a sentence such as
"High-pitched, 3 beeps per second, about 2.1 s long".

### 3.5 The live chart

The chart on the Listen tab is a rolling 8-second spectrogram: 16 log-spaced bands from 200 Hz to 7.6 kHz, drawn on a
Canvas with a colour ramp from dark blue (quiet) to bright (loud); detections are highlighted in the last columns.

### 3.6 Speech and keywords

Speech recognition uses the HarmonyOS **Core Speech Kit** (an HMS service) on the device: the same PCM stream is cut into
1280-byte blocks (40 ms) and written to the recogniser. On this platform the recogniser's language is Mandarin
(`zh-CN`); we queried the device and it reports no other language. In practice it returns Chinese characters for Chinese
and Latin text for many English words ("good morning", "watch out"), and that text is what the keyword engine sees.

The **keyword engine** (C++) normalises the text (lower case, punctuation turned into spaces), matches ASCII phrases as
whole words and non-ASCII phrases (Chinese) as substrings, alerts once per occurrence per utterance (interim results
repeat and extend a sentence), applies a cooldown (1 s for the built-in rules), and if several rules fire only the highest
priority one vibrates. Built-in rules: help, watch out, look out, danger and their Chinese forms. Each rule has a
vibration pattern (SOS, rapid burst, long buzz, double tap) and the user can add or remove words.

The recogniser sessions end after silence or a time limit, so a new session is started automatically (with back-off
after repeated failures). The recogniser only works while the app is on screen: it is stopped when the app goes to the
background, because it competes for the audio device there.

## 4. The decision logic (SoundPipeline)

* **Unknown alarm.** An alarm event is held for 3.7 s. If a learned sound or a built-in class explains it in that time,
  it is dropped; otherwise a new unknown sound is created and learned from the buffer. Alarms of one burst (within 6 s)
  create one sound.
* **Second hearing.** When an unnamed sound is heard again, the app asks "I've heard this sound before. Do you want to
  name it?" (at most once per 10 minutes per sound; "Don't ask again" is remembered). A prompt that would appear while
  the Teach dialog is open waits until it closes.
* **Named sound.** Vibration (three long pulses), notification "<name> detected", an on-screen card; alerts at most once
  per 15 s per sound, but every hearing is counted.
* **Built-in classes** (siren, scream, baby crying, beep, knock, loud sound) become history entries of their own,
  alert with their own vibration pattern and notification, and can be switched off one by one in Settings. A siren,
  scream or cry also "explains" the alarm tones heard in the seconds before it, so no unknown sound is created for them.
  A loud sound or a beep is held for 3 s, because the first seconds of a siren look like a loud sound, and the first beeps
  of a learned sound look like a chirp until the match is complete: if a recognised sound or a siren explains them they are
  dropped, otherwise they are reported after the hold.
* **Self-noise guard.** The phone's own noise must not be heard as a knock or a loud sound: while it vibrates (plus a
  1.5 s tail) and for 1.2 s after a tap on the screen, knocks and loud sounds are ignored.

## 5. Background listening

With "Keep listening in the background" on, starting to listen also requests a **continuous task** of type
`AUDIO_RECORDING` (needs `KEEP_BACKGROUND_RUNNING` and a `wantAgent` to bring the app back). The system then keeps the
process and the microphone alive when the app is not on screen or the screen is off, and shows its own notification
("A recording task is in progress") and a status-bar icon, so the user always sees that the microphone is in use.
Tapping stop (or turning the setting off) ends the task. Detections in the background produce vibration and
notifications; the Captions recogniser pauses until the app returns to the screen.

## 6. Privacy and safety

* Audio exists only in memory (a 10 s ring buffer and the current chunks) and is discarded; only feature numbers
  (templates, profiles) and settings are stored, on the phone.
* There is no network access and no cloud service. Speech recognition runs on the device.
* The microphone is shown as in use by the system whenever the app listens.
* Failures (denied permission, a dead microphone stream, denied notifications) are reported in the UI and the app keeps
  working with the remaining alert channels.

## 7. How it was verified

| What | How | Result |
|---|---|---|
| Engine and wrapper | C++ host tests (35) and the engine's own suites (core, custom, safety, sounds, speech) | all pass |
| App logic | ArkTS unit tests (72): catalog, pipeline, classes, settings, captions, self-noise guard | all pass |
| Real phone | scripted tests with a PC speaker next to the phone, screenshots and logs | detection of alarm, chirp, siren, loud sound, baby crying; naming, learning, teaching; alerts and notifications in the background and with the screen off; Settings switches; keyword alerts from simulated text and from real speech ("watch out") |
| Teaching | 25 real recordings, pairs and triples | see 3.3 |

Known limitations: scream and baby-crying detection are heuristics and depend on the sound source (a laptop speaker
cannot reproduce a baby's fundamental of 380-520 Hz, and a synthetic scream through it was not detected); knock
detection could not be exercised with a speaker; the speech recogniser is Mandarin-based and works only on screen;
some multi-note sounds may need a third take when taught in a noisy room.
