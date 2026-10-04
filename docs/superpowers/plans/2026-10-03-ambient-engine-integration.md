# Ambient Engine Integration Implementation Plan

> **For agentic workers:** execute task by task, test first, commit after each task.

**Goal:** Replace the first sound engine with the teammate's ambient engine and deliver functional spec F1-F9 (live spectrum, auto-learned sounds, "I know this sound" prompt, manual teaching, named-sound alerts, history, my sounds, clickable mic).

**Architecture:** vendored C++ engine -> `SoundEngine` wrapper (ring buffer, live spectrum, learn/train/profile) -> NAPI -> ArkTS `NativeEngine` -> `SoundPipeline` (pure, ports) -> `SoundCatalog` (pure) -> `SoundStore`. UI: Listen / History / My sounds.

**Spec:** `docs/superpowers/specs/2026-10-03-ambient-engine-integration-design.md`

## Global Constraints
- API 20+, HarmonyOS phone, English UI, nothing relies on audio, raw audio never stored (templates and profile numbers only).
- Surface only `alarm` and `custom` events. Alert cooldown per named sound 15 s; prompt: 2nd occurrence, not again within 10 min.
- Vendored engine keeps its own code unchanged except dropping `speech.*`; attribution in `entry/src/main/cpp/ambient/README.md`.
- Never commit signing secrets (`build-profile.json5` is committed sanitized; local copy is `skip-worktree`).

## Tasks
1. **Vendor engine + host build.** Copy `engine/{include,src,tests,tools}` (no speech) to `entry/src/main/cpp/ambient/`, remove first-engine detector/fingerprint/matcher/facade/tests/tools, keep `fft`+`frame_analyzer` in `entry/src/main/cpp/profile/`. Host `tests/CMakeLists.txt` builds ambient tests + new tests. Done when: host tests (teammate's 7 + kept analyzer tests) pass.
2. **`SoundProfile`** (`profile/profile.{h,cpp}`): `SoundProfile DescribeSound(const int16_t*, size_t, int sampleRate)`; fields dominantHz, durationSec, beepCount, beepsPerSec, repetition(0 single,1 repeated,2 continuous), modulation(0 steady,1 pulsed,2 sweeping), envelope[8]. Tests: 2 kHz tone 1 s -> ~2000 Hz, single, steady; 5 beeps 0.2/0.2 s -> repeated, ~2.5/s, pulsed; 6 s tone -> continuous; sweep -> sweeping.
3. **`SoundEngine` wrapper** (`wrapper/sound_engine.{h,cpp}`): `Process`, `LearnFromRing(label, eventTimeSec)`, `TrainFromTakes(label, takes)`, `CheckTake(take)`, `AddSound(bytes)`, `RemoveSound(label)`; events only alarm/custom; 16 live bands. Tests: learn after alarm then repeated sound yields custom event with that label; hum and 200 Hz motor bursts give no events; one continuous tone gives one alarm; consistent takes train, inconsistent takes rejected with message; odd chunk sizes.
4. **NAPI bridge** new API (`createEngine, destroyEngine, process, learnSound, trainSound, checkTake, addSound, removeSound`) + `index.d.ts`; hap builds.
5. **ArkTS models + `SoundCatalog` rewrite** (records keyed by label; pending/learned; occurrence outcome with alert/prompt; rename/delete/alerts/never-ask; plain-words summary; tolerant parse) + `SpectrumHistory`. Local unit tests.
6. **Pipeline, services:** `SoundPipeline` (ports: engine, alerts, scheduler), `NativeEngine`, `AlertService` (named pattern 3x500 ms, "<Name> detected" + time, prompt notification), `AudioService` (spectrum/events callbacks, `recordTake`, teaching suspends detection). Local tests for the pipeline.
7. **UI:** `SpectrumView` (canvas, detections highlighted), clickable `PulsingMic`, Teach button + `TeachDialog`, History rows with envelope mini-chart and `SoundDetailDialog`, `MySoundsTab`, prompt dialog, alert card.
8. **Device run:** install, run acceptance steps with what is possible, report verified / not verified.
9. **Docs:** README (architecture, demo, limitations), `AI_WORKFLOW.md` (+prior code and AI disclosure), `HACKATHON_BRIEF.md`, THIRD_PARTY notes.
