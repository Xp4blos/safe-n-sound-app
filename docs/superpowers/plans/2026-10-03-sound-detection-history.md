# Sound Detection, History and Named Alerts Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A HarmonyOS phone app that listens through the microphone, detects beeps/alarms/signals/door rings in C++, remembers distinct sounds by fingerprint, keeps a history, and notifies when a user-named sound is heard again.

**Architecture:** ArkTS captures 16 kHz mono PCM with `AudioCapturer` and passes each buffer through a thin NAPI bridge to a dependency-free C++ engine (frame analysis, event detector, fingerprint, matcher). ArkTS owns persistence (Preferences JSON), alerts (vibration, notification) and the two-tab UI.

**Tech Stack:** ArkTS/ArkUI (API 20+), C++17, NAPI, CMake (hap build and host tests), hvigor/`devecocli`, hypium for ArkTS unit tests.

**Spec:** `docs/superpowers/specs/2026-10-03-sound-detection-history-design.md`

## Global Constraints

- Target API 20 or later; do not lower it to make a build pass.
- HarmonyOS phone only. Foreground listening only; no background task.
- Engine input is 16 kHz mono 16-bit PCM, frames of 512 samples (32 ms).
- Engine has no OS dependencies (no OHOS headers outside `napi/`).
- No cloud, no stored audio; only fingerprints and metadata are persisted.
- Alerts: vibrate on every detection; notify only when the matched sound has a name and alerts enabled.
- Permissions: `ohos.permission.MICROPHONE` (runtime request with reason), `ohos.permission.VIBRATE`, notification authorisation requested at runtime.
- Fingerprint is exactly 29 floats (layout in Task 4). Match threshold is one constant, `kMatchThreshold = 0.80f`.
- Do not invent platform APIs from memory: confirm each ArkTS/NAPI API with `devecocli docs search` before using it.
- UI styling (colours, typography, icons) is not decided; ask the user at Task 11 before choosing.
- Never commit `build-profile.json5` (local signing secrets) or build output. Work on a feature branch; merging to main is the user's call.

## Review Focus

1. Buffer of odd byte length, empty buffer, or length not a multiple of 512 samples: carry the remainder, never crash (Tasks 3, 6).
2. Stored history that is corrupt JSON or contains a fingerprint of the wrong length: ignore that data, start clean, no crash (Task 7).
3. Microphone permission denied or later revoked: visible state, listening does not start, no crash (Task 10).
4. App sent to the background while listening: stop capture so the mic is released (Task 10).
5. The same named sound ringing repeatedly: at most one notification per sound per 10 s (`kAlertCooldownMs = 10000`), while every detection still vibrates (Task 7).

---

### Task 1: Host test harness and engine skeleton

**Files:**
- Create: `entry/src/main/cpp/engine/CMakeLists.txt` (static lib `sns_engine`, C++17)
- Create: `entry/src/main/cpp/engine/types.h`
- Create: `entry/src/main/cpp/tests/CMakeLists.txt` (host build of `sns_tests`)
- Create: `entry/src/main/cpp/tests/test_main.cpp`, `entry/src/main/cpp/tests/test_util.h`, `entry/src/main/cpp/tests/synth.h`
- Modify: `.gitignore` (add `/build-host`)

**Interfaces:**
- Produces (`types.h`, namespace `sns`):
  `constexpr int kSampleRate = 16000; constexpr int kFrameSize = 512; constexpr int kFingerprintSize = 29; using Fingerprint = std::array<float, kFingerprintSize>;`
  `enum class EventKind { Tonal = 0, Pulsed = 1 };`
  `struct Event { double startSec; double durationSec; EventKind kind; Fingerprint fp; };`
  `struct EngineOutput { float levelDb; std::vector<Event> events; };`
- Produces (`test_util.h`): `TEST(name)` registration macro, `CHECK(cond)`, `CHECK_NEAR(a, b, tol)`; `test_main.cpp` runs all and returns non-zero on failure.
- Produces (`synth.h`): `std::vector<int16_t> Tone(double hz, double sec, double amp)`, `Silence(double sec)`, `Noise(double sec, double amp, uint32_t seed)`, `BeepTrain(double hz, double onSec, double offSec, int count, double amp)`, `Impulses(double periodSec, double sec, double amp)`, `Concat(std::initializer_list<std::vector<int16_t>>)`.

- [ ] **Step 1: Toolchain spike.** Using the SDK tools (`F:\huwaei\DevEco Studio\sdk\default\openharmony\native\build-tools\cmake\bin\{cmake,ninja}.exe`, `...\native\llvm\bin\clang++.exe`), configure and build a hello-world host executable. If clang++ cannot find a C++ standard library for the Windows host, stop and ask the user which host compiler to install; do not install one unprompted.
- [ ] **Step 2: Write the failing test** in `tests/test_main.cpp`: `TEST(harness_runs)` with `CHECK(sns::kFrameSize == 512)` and `CHECK(sns::kFingerprintSize == 29)`; plus `TEST(synth_tone_length)` asserting `Tone(1000, 0.5, 0.5).size() == 8000`.
- [ ] **Step 3: Run to verify it fails** (compile error: `types.h`/`synth.h` missing). Run: `cmake -S entry/src/main/cpp/tests -B build-host -G Ninja -DCMAKE_CXX_COMPILER=<clang++.exe> && cmake --build build-host`.
- [ ] **Step 4: Implement** `types.h`, `synth.h` (header-only, deterministic noise from the seed) and the CMake files. `tests/CMakeLists.txt` adds `../engine` as a subdirectory and links `sns_engine`.
- [ ] **Step 5: Run to verify it passes.** Run: `build-host/sns_tests.exe`. Expected: both tests PASS, exit code 0.
- [ ] **Step 6: Commit** on a new branch `feature/sound-detection`, created from `docs/sound-detection-spec` so the spec and plan travel with it.

### Task 2: Frame analysis (level, spectrum, peaks, tonality)

**Files:**
- Create: `entry/src/main/cpp/engine/fft.h`, `fft.cpp`, `frame_analyzer.h`, `frame_analyzer.cpp`
- Test: `entry/src/main/cpp/tests/test_frame_analyzer.cpp`

**Interfaces:**
- Consumes: `kFrameSize`, `kSampleRate`, `synth.h`.
- Produces: `struct FrameFeatures { float levelDb; float tonality; int peakCount; std::array<float,3> peakHz; std::array<float,16> bandEnergy; };` and `class FrameAnalyzer { public: explicit FrameAnalyzer(int sampleRate); FrameFeatures Analyze(const int16_t* frame512) const; };`. `levelDb` is RMS in dBFS (full-scale sine ≈ -3 dB, silence floors at -100). `tonality` is the strongest spectral peak magnitude divided by the mean magnitude. `peakHz` is the top 3 peaks ordered by magnitude, 0 when absent. `bandEnergy` is 16 log-spaced bands from 200 Hz to 7.6 kHz, normalised to sum 1 (all zero for silence).

- [ ] **Step 1: Write failing tests:** a 1000 Hz tone at amp 0.5 gives `peakHz[0]` within 20 Hz of 1000 and `levelDb` within 1 dB of -9; white noise at amp 0.5 has `tonality` below 8 while the tone has `tonality` above 25; `Silence` gives `levelDb <= -90` and `peakCount == 0`; a 1000 Hz plus 2500 Hz mix reports both peaks within 20 Hz; `bandEnergy` sums to 1 ± 0.01 for the tone.
- [ ] **Step 2: Run to verify they fail** (`FrameAnalyzer` undefined).
- [ ] **Step 3: Implement** a radix-2 real FFT with a Hann window on the 512-sample frame, and the analyzer. Peak = local maximum above 6× mean magnitude, parabolic interpolation for frequency.
- [ ] **Step 4: Run to verify they pass.** If the tonality thresholds above are off for the chosen window, adjust the thresholds in the test and in Task 3's constant, not the test intent (tone ≫ noise).
- [ ] **Step 5: Commit.**

### Task 3: Event detector

**Files:**
- Create: `entry/src/main/cpp/engine/event_detector.h`, `event_detector.cpp`
- Test: `entry/src/main/cpp/tests/test_event_detector.cpp`

**Interfaces:**
- Consumes: `FrameAnalyzer`, `FrameFeatures`, `Event`, `EventKind`.
- Produces: `class EventDetector { public: explicit EventDetector(int sampleRate); void Push(const FrameFeatures& f, double frameStartSec, std::vector<Event>* out); void Flush(std::vector<Event>* out); };` (fingerprint fields are filled by Task 4; here `fp` stays zeroed). Constants in `event_detector.h`: `kActiveMarginDb = 12.0f`, `kTonalityMin = 20.0f`, `kCloseSilenceSec = 0.8`, `kMaxEventSec = 10.0`, `kMinEventSec = 0.15`. Noise floor: running minimum-tracking average, rises slowly (about 1 dB/s) and falls fast; initial floor -60 dB.

Behaviour: a frame is active when `levelDb > floor + kActiveMarginDb` and `tonality >= kTonalityMin`. An event starts at the first active frame and closes after `kCloseSilenceSec` without active frames, or at `kMaxEventSec`. Events shorter than `kMinEventSec` are dropped. Kind is `Pulsed` if the event contains at least 3 distinct on-segments, otherwise `Tonal`. Broadband noise and impulses (knocks) never start an event.

- [ ] **Step 1: Write failing tests** (feed frames from `synth.h` through `FrameAnalyzer`, 512 samples at a time): a 1 s 1000 Hz tone at amp 0.3 after 1 s of low noise yields exactly 1 `Tonal` event with `durationSec` within 0.15 of 1.0 and `startSec` within 0.1 of 1.0; `BeepTrain(2000, 0.15, 0.15, 5, 0.3)` yields exactly 1 `Pulsed` event; 5 s of `Noise(amp 0.3)` yields 0 events; `Impulses(0.5, 3, 0.9)` yields 0 events; 10 s of `Silence` plus a quiet `Noise(amp 0.005)` yields 0 events; a clipped full-scale tone (`amp 1.0`) yields 1 event; two tones separated by 2 s of silence yield 2 events; `Flush` after a tone that has not yet closed emits it.
- [ ] **Step 2: Run to verify they fail.**
- [ ] **Step 3: Implement** as specified above.
- [ ] **Step 4: Run to verify they pass.**
- [ ] **Step 5: Commit.**

### Task 4: Fingerprint and matcher

**Files:**
- Create: `entry/src/main/cpp/engine/fingerprint.h`, `fingerprint.cpp`, `matcher.h`, `matcher.cpp`
- Modify: `entry/src/main/cpp/engine/event_detector.cpp` (fill `Event::fp`)
- Test: `entry/src/main/cpp/tests/test_matcher.cpp`

**Interfaces:**
- Consumes: `FrameFeatures`, `Fingerprint`, `EventDetector`.
- Produces: `Fingerprint BuildFingerprint(const std::vector<FrameFeatures>& activeFrames, const std::vector<bool>& activeMask, double frameSec);` `float Similarity(const Fingerprint& a, const Fingerprint& b);` `struct MatchResult { int index; float score; };` `constexpr float kMatchThreshold = 0.80f;` `MatchResult Match(const Fingerprint& fp, const std::vector<Fingerprint>& saved, float threshold = kMatchThreshold);` (`index` is -1 when the best score is below the threshold or `saved` is empty).
- Fingerprint layout (29 floats): `[0..2]` top 3 peak frequencies, each divided by 8000, 0 if absent; `[3..18]` mean `bandEnergy` over active frames; `[19]` beep period in seconds divided by 2, clamped to [0,1], 0 if not periodic; `[20]` duty cycle (on-time / period), 0 if not periodic; `[21..28]` amplitude envelope resampled to 8 points, scaled so max = 1.
- Similarity weights: peaks 0.5 (per-peak relative frequency error within 3% counts as a match), spectral cosine 0.2, rhythm 0.2 (period and duty closeness), envelope 0.1.

- [ ] **Step 1: Write failing tests** (events produced via the real detector): the same 1000 Hz tone twice, second with +0.5% frequency and amp scaled by 0.5, scores `>= 0.90` and `Match` returns that index; 1000 Hz beep train versus 2500 Hz beep train scores `< 0.50`; a steady 1000 Hz tone versus a 1000 Hz beep train (0.15 s on/off) scores `< 0.80`; `Match` with an empty `saved` returns `index == -1`; `Match` picks the right one among three saved sounds; `Similarity(a, a) == 1` within 1e-4.
- [ ] **Step 2: Run to verify they fail.**
- [ ] **Step 3: Implement.** Keep per-event frame features inside the detector so it can call `BuildFingerprint` when the event closes.
- [ ] **Step 4: Run to verify they pass.** If the margins need tuning, tune weights and the single threshold constant, and record the final values in the test comments.
- [ ] **Step 5: Commit.**

### Task 5: Engine facade and WAV CLI

**Files:**
- Create: `entry/src/main/cpp/engine/engine.h`, `engine.cpp`, `entry/src/main/cpp/tools/wav_cli.cpp`
- Modify: `entry/src/main/cpp/tests/CMakeLists.txt` (add `wav_cli` target)
- Test: `entry/src/main/cpp/tests/test_engine.cpp`

**Interfaces:**
- Consumes: `FrameAnalyzer`, `EventDetector`, `EngineOutput`.
- Produces: `class Engine { public: explicit Engine(int sampleRate); EngineOutput Process(const int16_t* pcm, size_t sampleCount); };` Buffers of any length are accepted: leftover samples (fewer than 512) are kept and prepended to the next call. `levelDb` is the level of the most recently analysed frame (-100 if none yet). `startSec` is measured from the first sample ever passed in. `Process(nullptr, 0)` returns an empty output.
- `wav_cli <file.wav>` prints one line per event: `start=<s> dur=<s> kind=<Tonal|Pulsed>`; supports 16-bit PCM mono 16 kHz only and exits non-zero with a message otherwise.

- [ ] **Step 1: Write failing tests:** feeding a 1 s tone plus 2 s silence in chunks of 100 samples gives the same single event (start within 0.05 s) as one big call; chunk size 1 and chunk size 513 also give 1 event; `Process(nullptr, 0)` returns no events and does not crash; `levelDb` is near -9 after a tone chunk.
- [ ] **Step 2: Run to verify they fail.**
- [ ] **Step 3: Implement** `Engine` and `wav_cli`.
- [ ] **Step 4: Run to verify they pass,** then run `wav_cli` on a WAV written by a small test helper and check the printed line.
- [ ] **Step 5: Commit.**

### Task 6: NAPI bridge and native build wiring

**Files:**
- Create: `entry/src/main/cpp/CMakeLists.txt` (shared lib `safensound`, links `sns_engine`, `libace_napi.z.so`, hilog)
- Create: `entry/src/main/cpp/napi/napi_init.cpp`
- Create: `entry/src/main/cpp/types/libsafensound/index.d.ts`, `oh-package.json5`
- Modify: `entry/build-profile.json5` (`externalNativeOptions.path` to the new CMakeLists, arm64-v8a and x86_64 ABIs for emulator), `entry/oh-package.json5` (dependency on the types package)

**Interfaces:**
- Consumes: `Engine`, `Match`, `Fingerprint`, `kFingerprintSize`.
- Produces (`index.d.ts`, module `libsafensound.so`): 
  `export interface DetectedEvent { startSec: number; durationSec: number; kind: number; fingerprint: number[]; }`
  `export interface EngineResult { levelDb: number; events: DetectedEvent[]; }`
  `export interface MatchResult { index: number; score: number; }`
  `export const createEngine: (sampleRate: number) => number;`
  `export const destroyEngine: (handle: number) => void;`
  `export const process: (handle: number, pcm: ArrayBuffer) => EngineResult;`
  `export const matchFingerprint: (fp: number[], saved: number[][]) => MatchResult;`
- Behaviour: `destroyEngine` is an addition to the spec's three functions, needed to free native state. Handles map to `unique_ptr<Engine>` in a mutex-guarded table. `process` throws a JS error for an unknown handle or a non-ArrayBuffer argument; for an odd byte length it ignores the trailing byte and logs, without throwing. `matchFingerprint` skips any saved entry whose length is not 29 and a `fp` of the wrong length returns `{index:-1, score:0}`. Returned `index` refers to the caller's original `saved` array positions.

- [ ] **Step 1: Write the failing check:** build the hap with `devecocli build` after adding an ArkTS smoke import of `libsafensound.so` in a scratch file; expected FAIL (module not found).
- [ ] **Step 2: Implement** the bridge and wiring. Confirm NAPI registration and `externalNativeOptions` syntax with `devecocli docs search` first.
- [ ] **Step 3: Run `devecocli build`.** Expected: BUILD SUCCESS and a `.hap` under `entry/build`, with `libsafensound.so` inside it.
- [ ] **Step 4: Smoke-run on the emulator** (agent may launch the app): call `createEngine(16000)`, `process` with a 1 s synthetic tone ArrayBuffer, log the result with hilog, and check it with `devecocli log`. Expected: one event with kind 0. Label this as simulated audio. Remove the scratch smoke code afterwards.
- [ ] **Step 5: Commit.**

### Task 7: ArkTS models and SoundCatalog (pure logic)

**Files:**
- Create: `entry/src/main/ets/model/SoundModels.ets`, `entry/src/main/ets/model/SoundCatalog.ets`
- Test: `entry/src/test/SoundCatalog.test.ets` (register it in `entry/src/test/List.test.ets`)

**Interfaces:**
- Produces (`SoundModels.ets`): `DetectedEvent`, `EngineResult`, `MatchResult` (same shapes as the `.d.ts`, re-declared to keep the catalog free of native imports) and
  `export interface SoundRecord { id: number; kind: number; fingerprint: number[]; name: string; alertsEnabled: boolean; timesHeard: number; lastSeenMs: number; lastNotifiedMs: number; recentMs: number[]; }` (`name === ''` means unnamed).
- Produces (`SoundCatalog.ets`): `export interface CatalogOutcome { record: SoundRecord; isNew: boolean; shouldNotify: boolean; }`
  `export class SoundCatalog { static readonly ALERT_COOLDOWN_MS: number = 10000; static readonly MAX_NAME_LENGTH: number = 40; static readonly MAX_RECENT: number = 20; constructor(records: SoundRecord[]); static parse(json: string): SoundCatalog; serialize(): string; records(): SoundRecord[]; fingerprints(): number[][]; registerEvent(ev: DetectedEvent, matchIndex: number, nowMs: number): CatalogOutcome; rename(id: number, name: string): void; setAlerts(id: number, enabled: boolean): void; }`
- Behaviour: `registerEvent` with `matchIndex` of -1 creates a record (`id` = max id + 1, starting at 1, alerts enabled by default, unnamed) and returns `shouldNotify=false`; with a valid index it bumps `timesHeard`, `lastSeenMs`, appends to `recentMs` (capped) and sets `shouldNotify` only when `name !== ''`, `alertsEnabled`, and `nowMs - lastNotifiedMs >= ALERT_COOLDOWN_MS` (then updates `lastNotifiedMs`). An out-of-range `matchIndex` other than -1 is treated as -1. `rename` trims, truncates to `MAX_NAME_LENGTH`, and an empty or whitespace-only result makes the sound unnamed. `parse` returns an empty catalog for invalid JSON and drops any record that is missing fields or whose fingerprint length is not 29. Newest records first in `records()` is the UI's concern, not the catalog's.

- [ ] **Step 1: Write failing tests:** new event creates "unnamed" record id 1 then id 2; matched event increments `timesHeard` to 2; named plus alerts on notifies once, a second event 5 s later does not (`shouldNotify=false`), one 11 s later does; named with alerts off never notifies; `rename('  Doorbell  ')` gives `'Doorbell'`, `rename('   ')` gives `''`, a 100-char name is cut to 40; `parse('{not json')` gives 0 records; `parse` of a record with a 28-float fingerprint drops it and keeps a valid sibling; `serialize` then `parse` round-trips; `recentMs` never exceeds 20.
- [ ] **Step 2: Run to verify they fail.** Run the local unit tests with the command documented by the `ohos-app-dev` skill (look it up via `devecocli docs search` if absent). Expected: FAIL, `SoundCatalog` not defined.
- [ ] **Step 3: Implement** the models and the catalog.
- [ ] **Step 4: Run to verify they pass.**
- [ ] **Step 5: Commit.**

### Task 8: SoundStore (persistence)

**Files:**
- Create: `entry/src/main/ets/services/SoundStore.ets`

**Interfaces:**
- Consumes: `SoundCatalog.parse/serialize`.
- Produces: `export class SoundStore { static async open(context: Context): Promise<SoundStore>; catalog(): SoundCatalog; async save(): Promise<void>; }` Backed by `@kit.ArkData` Preferences, file name `sounds`, key `catalog`. A failed read or an invalid payload yields an empty catalog; a failed write is logged and does not throw.

- [ ] **Step 1: Implement** after confirming the Preferences API with `devecocli docs search`.
- [ ] **Step 2: Verify:** `devecocli build` passes and `devecocli check lint` is clean for the new file. On the emulator, once the UI exists (Task 11), restart the app and confirm history survives (reported as verified only if exercised).
- [ ] **Step 3: Commit.**

### Task 9: AlertService and manifest permissions

**Files:**
- Create: `entry/src/main/ets/services/AlertService.ets`
- Modify: `entry/src/main/module.json5` (`requestPermissions`), `entry/src/main/resources/base/element/string.json` (reason strings)

**Interfaces:**
- Produces: `export class AlertService { async ensureNotificationPermission(): Promise<boolean>; vibrate(): void; async notifyKnownSound(name: string): Promise<void>; }`. `vibrate` plays a short pulse pattern (about 300 ms on, 150 ms off, 300 ms on) and swallows failures. `notifyKnownSound` publishes a notification titled with the sound name and does nothing, without throwing, when notifications are not permitted.
- Manifest: `ohos.permission.MICROPHONE` with `reason` and `usedScene` (abilities: `EntryAbility`, `when: inuse`), and `ohos.permission.VIBRATE`.

- [ ] **Step 1: Implement** after confirming the vibrator, notification and permission-declaration APIs with `devecocli docs search`.
- [ ] **Step 2: Verify:** `devecocli build` succeeds and lint is clean.
- [ ] **Step 3: Commit.**

### Task 10: NativeEngine wrapper and AudioService

**Files:**
- Create: `entry/src/main/ets/services/NativeEngine.ets`, `entry/src/main/ets/services/AudioService.ets`
- Modify: `entry/src/main/ets/entryability/EntryAbility.ets` (stop listening in `onBackground`)

**Interfaces:**
- Consumes: `libsafensound.so` functions, `SoundCatalog`, `SoundStore`, `AlertService`.
- Produces: `export type ListenState = 'idle' | 'starting' | 'listening' | 'permission_denied' | 'error';`
  `export class AudioService { constructor(store: SoundStore, alerts: AlertService); async start(context: Context): Promise<void>; async stop(): Promise<void>; onLevel: (db: number) => void; onSound: (record: SoundRecord, isNew: boolean) => void; onState: (s: ListenState) => void; }`
- Behaviour: `start` requests the microphone permission via `abilityAccessCtrl`; if denied it sets `permission_denied` and returns without creating a capturer. It creates an `AudioCapturer` (16 kHz, mono, S16LE), feeds each read buffer to the engine, then for each event calls the native `matchFingerprint` against `catalog.fingerprints()`, `catalog.registerEvent`, `alerts.vibrate()`, and `alerts.notifyKnownSound(name)` when `shouldNotify`, then `store.save()`. Event wall-clock time is `captureStartMs + startSec*1000`. Capturer errors or interruptions set `error` and release resources. `stop` is idempotent and destroys the native engine handle. `EntryAbility.onBackground` calls `stop`.

- [ ] **Step 1: Implement** after confirming `AudioCapturer` and permission APIs with `devecocli docs search`.
- [ ] **Step 2: Verify:** build and lint pass. Add a local unit test for the event-handling path if it can be separated cleanly with a fake engine; otherwise state it as covered only by the Task 12 device check.
- [ ] **Step 3: Commit.**

### Task 11: UI (Listen and History tabs, naming dialog)

**Files:**
- Create: `entry/src/main/ets/components/ListenTab.ets`, `HistoryTab.ets`, `SoundEditDialog.ets`
- Modify: `entry/src/main/ets/pages/Index.ets` (Tabs container), `entry/src/main/resources/base/element/string.json`

**Interfaces:**
- Consumes: `AudioService`, `SoundStore`, `SoundCatalog`, `ListenState`.
- Behaviour (from the approved layout): Listen tab shows a start/stop button, a live level meter, the last detected sound with time, and a visible message for `permission_denied` (with a retry action) and `error`; it also shows when notifications are off. History tab lists sounds newest first (name or "Sound #N", kind, times heard, last seen, alert toggle); tapping a row opens `SoundEditDialog` to rename and toggle alerts, and saves through the store.

- [ ] **Step 1: Ask the user for visual choices** (colours, typography, icons, meter style) before writing any styling; follow existing resource patterns for strings and colours.
- [ ] **Step 2: Implement** the three components and the Tabs shell.
- [ ] **Step 3: Verify:** `devecocli build` and `devecocli check lint` pass; install and launch on the emulator, take a screenshot of each tab with `devecocli ui screenshot`.
- [ ] **Step 4: Commit.**

### Task 12: Validation, documentation, handover

**Files:**
- Modify: `README.md` (create if absent: setup, build, install, launch, test commands, signing note), `AI_WORKFLOW.md`, `HACKATHON_BRIEF.md`

- [ ] **Step 1: Run the full checks:** host C++ tests, ArkTS local unit tests, `devecocli check lint`, `devecocli build` producing the `.hap`.
- [ ] **Step 2: Emulator run:** install, launch, grant permissions, check `devecocli log` for errors. Microphone input on the emulator may be limited; label any simulated audio and report unexercised paths as unverified.
- [ ] **Step 3: Give the user a short manual test script** (play a beep from a phone or speaker near the device, name the sound in History, play it again, expect a vibration and a named notification; deny the mic permission and confirm the message; background the app and confirm the mic indicator disappears).
- [ ] **Step 4: Update docs:** README commands, `AI_WORKFLOW.md` (tools, work log, limitations), `HACKATHON_BRIEF.md` fields the user has confirmed (problem, theme, capabilities, target).
- [ ] **Step 5: Commit** and tell the user the branch is ready; merging is theirs to do.
