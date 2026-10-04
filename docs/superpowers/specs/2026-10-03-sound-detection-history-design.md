# Safe'n'Sound: Sound Detection, History and Named Alerts (Design)

Date: 2026-10-03
Status: awaiting user review

## 1. Purpose

Safe'n'Sound helps people who are hard of hearing by turning everyday sounds into
something they can see or feel. This iteration delivers the first vertical slice:
the phone listens through the microphone, detects beeps, alarms, signals and door
rings, remembers distinct sounds, keeps a history, and lets the user name a sound
so they are notified when that sound is heard again.

All analysis is on-device. No cloud, no stored audio. Only compact fingerprints
are persisted.

Challenge theme (lead): Human-Centric Technology. Platform capabilities used:
microphone, vibration, notifications.

## 2. Confirmed decisions

| Topic | Decision |
| --- | --- |
| Sound classes now | Beeps, alarms, signals, door rings. Detected from amplitude, frequency and dynamics |
| Out of scope now | Speech recognition, knock and loud-sound classes, background listening |
| Core language | C++ DSP engine plus NAPI bridge, written from scratch (nothing pre-existing in this repo) |
| UI language | ArkTS/ArkUI |
| Listening mode | Foreground only (app open, screen on) |
| Memory of sounds | Spectral/temporal fingerprint matched by similarity |
| UI layout | Two tabs: Listen and History |
| Alerts | Vibrate on any detection. Notify only when a named sound with alerts enabled is matched again |
| Target | HarmonyOS phone, API 20 or later |

## 3. Architecture

Audio is captured in ArkTS and analysed in C++.

```
AudioCapturer (ArkTS) --PCM buffers--> NAPI --> C++ engine
                                                  |  level, events + fingerprints
ArkTS services <----------------------------------+
  SoundStore (Preferences/JSON), AlertService (vibrator, notification)
  UI: Listen tab, History tab
```

The C++ engine has no OS dependencies, so it builds and is tested on the PC and also
runs on the phone.

### 3.1 C++ engine (`entry/src/main/cpp`)

- Input: 16 kHz mono 16-bit PCM, analysed in frames of about 32 ms.
- Per frame: RMS level in dB, FFT magnitude spectrum, dominant spectral peaks, and a
  tonality measure (peak-to-average spectral ratio).
- Adaptive noise floor: slow running estimate of background level. A frame is "active"
  when level exceeds the floor by a margin.
- Event detector: tracks onset, sustain, and periodic on/off pattern (beeping). An event
  is emitted when activity is tonal (alarm/beep) or a repeated pulse pattern (door ring)
  and is closed after a short silence. Each event carries start time, duration and a
  fingerprint.
- Fingerprint (fixed-size float vector): top 3 peak frequencies, coarse spectral shape
  (about 16 bands), beep period and duty cycle, amplitude envelope shape.
- Matcher: `match(fingerprint, saved[])` returns the best index and a similarity score in
  [0, 1]. The app treats a score at or above a threshold as the same sound. The default
  threshold is tuned with the tests and kept as a single constant.

### 3.2 NAPI bridge

Three functions exposed to ArkTS with a typed `.d.ts`:

- `createEngine(sampleRate: number): number`, returns an engine handle.
- `process(handle: number, pcm: ArrayBuffer): EngineResult`, with
  `level` (dB) and `events: DetectedEvent[]` (start time, duration, fingerprint).
- `matchFingerprint(fp: number[], saved: number[][]): MatchResult`, with
  `index` (-1 if none) and `score`.

### 3.3 ArkTS app (`entry/src/main/ets`)

- `AudioService`: requests the microphone permission, runs `AudioCapturer`, feeds buffers
  to the engine, and publishes level and events to the UI.
- `SoundStore`: persists sounds locally in Preferences as JSON. Each sound has id,
  fingerprint, optional name, alerts enabled, times heard, last seen, and recent event
  timestamps.
- `AlertService`: short vibration pattern on any detection. Notification, titled with the
  sound name, only for a named sound with alerts on.
- Event flow: event arrives, matcher compares it with saved sounds. A match increments
  that sound's count and updates last seen. No match creates a new "Sound #N" entry.
- Notification permission is requested at runtime. If denied, vibration and history still
  work and the Listen tab shows that notifications are off.

### 3.4 UI

Two tabs.

- Listen: start/stop button, live level meter, last detected sound with time.
- History: list of sounds (name or "Sound #N", type, times heard, last seen, alert toggle).
  Tapping an item opens a dialog to rename it and toggle alerts.

Visual styling details (colours, typography, icons) are not yet decided and will be
confirmed with the user during implementation.

### 3.5 Permissions and manifest

- `ohos.permission.MICROPHONE` (user-grant, runtime request, with a reason string)
- `ohos.permission.VIBRATE`
- Notification authorisation via `notificationManager.requestEnableNotification`
- Native module wiring: CMake and `externalNativeOptions` in the entry module. API 20
  minimum is kept.

## 4. Error handling

- Microphone permission denied: Listen tab explains, offers to re-request, no crash.
- `AudioCapturer` failure or interruption (for example another app takes the mic):
  stop listening, show state, allow restart.
- Corrupt or missing stored history: start with an empty history, do not crash.
- Engine receives an empty or odd-sized buffer: ignore it and log.

## 5. Testing and validation

- C++ unit tests on the PC with synthetic audio: pure tones, beep trains, impulse trains,
  white noise (negative case), and silence. They check that beeps and alarms are
  detected, noise is not, the same sound matches itself across small variations, and
  different sounds do not match.
- A small CLI that runs the engine on a WAV file, for quick manual checks.
- The ArkTS side is covered by the smallest relevant build and lint, plus unit tests for
  `SoundStore` logic where practical.
- hvigor build producing a `.hap`.
- Emulator run: install, launch, confirm permissions and the UI. Microphone input on an
  emulator may be limited. Any simulated audio will be labelled as such, and anything
  not exercised will be reported as unverified. The user performs multi-step on-device
  testing.

## 6. Repository and process notes

- Work happens on a feature branch, committed in small steps. Merging to main is the
  user's decision.
- `AI_WORKFLOW.md` and `HACKATHON_BRIEF.md` are updated as work lands.
- No credentials, signing material or build output is committed.

## 7. Non-goals

Speech recognition, knock and loud-sound categories, background or lock-screen listening,
cloud services, audio storage, per-environment learning beyond the adaptive noise floor.

## 8. Open, reversible defaults

16 kHz sample rate, the match threshold value, and Preferences (not a database) for
storage. These can be changed during implementation without altering the design.
