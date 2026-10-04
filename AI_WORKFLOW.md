# AI Workflow

This project uses AI-assisted development. Keep this document current and public-safe. Do not include credentials, tokens, personal data, private endpoints, or confidential prompts.

## Tools used

| Model, agent, MCP server, or Agent Skill | Version or source | Role in the project |
| --- | --- | --- |
| Claude Sonnet 5.5 (`claude-sonnet-5-5`) in Claude Code | Anthropic | Design dialogue, specs and plans, C++ wrapper and profile code, NAPI bridge, ArkTS app, tests, debugging, documentation |
| Fresh-context reviewer subagent (most capable model tier available in Claude Code) | Anthropic | One whole-branch code review of the first version |
| Agent Skill `superpowers:brainstorming` | superpowers plugin | Scoping and design approval before code |
| Agent Skill `superpowers:writing-plans` | superpowers plugin | Written implementation plans from the approved specs |
| Agent Skill `superpowers:executing-plans` | superpowers plugin | Task-by-task execution with a progress ledger |
| Agent Skill `superpowers:test-driven-development` | superpowers plugin | Red-green discipline |
| Hackathon skills (`ohos-app-dev`, ArkTS knowledge base) | hackyeah2026-challenge repository | Build and validation guidance, ArkTS reference |
| AI assistance used by a teammate for the ambient C++ engine | see `entry/src/main/cpp/ambient/README.md` and the teammate's repository | Prior code, included in this project |

No MCP server was used for the product work.

## Prior code and open-source disclosure

- `entry/src/main/cpp/ambient/` is prior code written by members of the team. First version: https://github.com/Xp4blos/hack-yeah-2026 (commit `512e41d`, without `speech.*`). In the merge with the second concept (https://github.com/INawrot/Ambient, `AmbientApp`) it was replaced by that repository's newer engine (chirp, cry, siren, scream, speech keywords, false-alarm control), with small additions of ours documented in `entry/src/main/cpp/ambient/CHANGES.md` (strict teaching options). The app icon (`app_icon.png`) also comes from that repository. It is the team's own work; the repository deliberately has no license file.
- The first version of this project's own C++ detector and matcher (written with Claude in this session) was replaced by that engine; only its FFT frame analyzer is kept (`entry/src/main/cpp/profile`).
- Third-party libraries: `@ohos/hypium` and `@ohos/hamock` (test tools, installed by `ohpm`), the HarmonyOS SDK and DevEco toolchain. No other open-source code is bundled.

## Important prompts and instructions

- `AGENTS.md` - repository-wide hackathon constraints and working agreement (the user directs product, UI and scope decisions).
- Project brief given by the user: an app for deaf and hard-of-hearing people that detects beeps, alarms, signals and door rings, remembers specific sounds, keeps a history, lets the user name a sound to be notified when it is heard again; core logic in C++, UI in ArkTS, using microphone, vibration and notifications.
- Functional specification F1-F9 given by the user (listening, live view, detection and fingerprint description, memory, "I know this sound" prompt, teach flow, alerts, history, my sounds) and the instruction to switch to the teammate's engine, make the microphone icon a button and add a "Teach a sound" button.
- Decisions the user confirmed: foreground-only listening, calm blue palette, pulsing mic with an 8-second live view, vibrate on detection and notify for named sounds, build with `hvigorw` while `devecocli` needed Node 22.

## AI-assisted work log

| Date | Tool/model | Request or task | Generated or changed | Human review and validation |
| --- | --- | --- | --- | --- |
| 2026-10-03 | Claude Sonnet 5.5 | Brainstorm scope, spec, plan (first engine) | specs and plans under `docs/superpowers/` | User answered scoping questions and approved the spec and plan |
| 2026-10-03 | Claude Sonnet 5.5 | First C++ engine, NAPI bridge, ArkTS app and UI | engine, bridge, services, tabs | Host and ArkTS unit tests, `.hap` build, screenshots on a phone |
| 2026-10-03 | Fresh-context reviewer subagent | Whole-branch review | findings; fixes in the following commit | Critical/Important findings fixed test-first where testable |
| 2026-10-03 | Claude Sonnet 5.5 | Phone test and fixes | stop/error race, noise floor, vibration feedback, chart redraw | Found by running on a PLR-AL00 phone; verified on the phone |
| 2026-10-03 | Claude Sonnet 5.5 | Check the teammate's engine, then integrate it | vendored engine, `SoundEngine` wrapper, `SoundProfile`, new NAPI API | Built with warnings as errors; the engine's 7 test targets plus 12 wrapper and 6 profile tests pass; compared on hum, motor and beep files |
| 2026-10-03 | Claude Sonnet 5.5 | Functional spec F1-F9 in the app | catalog, pipeline, services, Listen/History/My sounds, teach, prompt and detail dialogs | 36 ArkTS unit tests; `.hap` builds; lint 0 errors; start, spectrum, teach recording and stop exercised on the phone |

| 2026-10-03 | Claude Sonnet 5.5 | Modern UI, signed .hap, GitHub repo and release, demo video | restyled components, `scripts/make-signed-hap.sh`, release v1.0.0, `dist/safe-n-sound-demo.mp4` | Video recorded by driving the phone with `hdc`/`uitest` while a real alarm was played from a PC speaker; fixes found while recording |

## Workflow

### Ideation and architecture

The user supplied the product brief and later a functional specification. The model asked scoping questions, proposed an architecture (capture in ArkTS, analysis in dependency-free C++ behind a thin NAPI bridge so the DSP is testable on a PC), and wrote a spec and a plan for the user to approve. After phone testing showed that the first engine was fragile on real audio, the user supplied a teammate's engine; the model compared both on generated hum, motor, beep and long-tone files, vendored the teammate's engine, and wrapped it.

### Implementation

Work followed written plans, task by task and test first, with small commits on feature branches. Platform APIs were checked against the SDK's own `.d.ts` declarations rather than written from memory. Pure logic (catalog, pipeline, spectrum history) is separated from the platform behind small ports so it is unit tested with fakes.

### Testing and debugging

- C++: `scripts\host-tests.cmd` (MSVC + SDK cmake/ninja): our tests plus the teammate's tests and `wav_cli` end-to-end checks.
- ArkTS: `scripts/arkts-tests.sh` (hvigor + hypium), 36 tests.
- Build: `hvigorw assembleHap`; lint: `devecocli check lint` (Node 22), 0 errors, 5 style warnings.
- Device: physical Huawei phone (PLR-AL00, API 26): permissions, start/stop, Home button, live spectrum, teach recording and its on-screen result.

## Bugs found during phone testing

- After tapping Stop the Listen tab showed an error: the capturer's own `STOPPED` event was treated as a failure. Fixed by ignoring events from an already released capturer.
- A fixed -60 dB noise floor made room hum one endless event; the phone's own vibration was heard by its microphone and re-detected as new sounds. The first engine was replaced by the teammate's engine, which restricts alarms to 800-4500 Hz and ignores steady background.
- The level chart did not redraw because the list keys ignored the values.
- hilog domain `0x0000` produced no output on the test phone; domain `0x3201` is used.
- Recording the demo showed that the dialog buttons (Cancel, Save, Name it) did not close their dialogs: the framework does not set the `controller` of a custom dialog built inside a method (it was `undefined`). Fixed by passing an explicit `onClose` callback to each dialog.
- While the demo was recorded with a fast screenshot loop on the phone, repeats of the alarm were no longer recognised, although the same sound was recognised without that load; the audio stream is disturbed by heavy load on the phone. The same recording replayed on the PC gave the same result as the phone, which is how this was separated from a threshold problem. The capture was made lighter (half-size screenshots, no UI polling while a sound is analysed).
- The engine's default similarity threshold (0.8) missed quieter repeats on real audio; it is 0.6 for learned sounds, checked on a real recording and on host tests that a different pitch is still not matched.
- The "Doorbell detected" card was below the fold of the Listen tab, so it was moved to the top.
- Teach a sound on the phone: two takes of the same sound were rejected as "recordings do not match each other" (similarity 0.08). The recorded takes showed why: the trainer cuts a sound out of a recording by loudness alone, and in a noisy room random noise crosses its threshold, so both cuts were almost the whole 6 s. Takes are now reduced to their clearest tonal stretch (600-6000 Hz, tonality 25+) before training; the two real takes are a fixture with a unit test. The sound description threshold is also relative to the loudest frame.
- The "Doorbell detected" card, once at the top of the page, pushed the Stop and Teach buttons off screen for a minute; it is now an overlay that does not move anything and disappears when tapped.
- The alert card did not redraw when a second alert arrived: three screenshots in a row showed the same "Doorbell detected 23:17:59" card while the log recorded alerts for another sound. A `@Builder` with a parameter does not update; the card is now its own component, and the test also checks that the time on the card is fresh (an earlier green result was a false match on the stale card).
- The multi-take trainer sets its own recognition threshold of 0.7-0.9; it is capped at 0.6 like single recordings, because it missed a repeat that was only a few dB quieter.
- The PC-speaker test rig is not stable: the level of the same sound at the phone varied between -18 and -28 dB from run to run (and even fell when the PC volume was raised), and when the sound is less than about 12 dB above the room noise the engine rightly rejects takes and misses beeps. The best full run (both sounds taught from two real takes each, each recognised under its own name with a fresh card, My sounds listing both) failed only the first immediate Doorbell check; the later runs at weaker levels failed more steps for that reason.

## Unsuccessful approaches

- The SDK's `clang++` cannot build host tests (no Windows C++ standard library); MSVC Build Tools are used.
- `devecocli` needed Node 22+ and only Node 18 was installed at first; `hvigorw` was used directly, then Node 22 was added for `devecocli`.
- Audio from the PC never reached the phone's microphone while headphones were plugged in, so beep detection with a real sound was verified only on generated audio (see limitations).

## Known limitations

- Verified on the phone with a real alarm sound (PC speaker): detection, the "Unknown sound" entry, recognition of the repeat, the "I've heard this sound before" prompt, naming, the "Doorbell detected" card and the count of 3 in History (see the demo video and the logs). Not confirmed visually: the vibration pattern (cannot be filmed) and the system notification (published without errors, but not seen). Not exercised: a clean single run in which every Teach step passes (the best run missed one step, see above), restart persistence of named sounds (data survived reinstalls in testing but was not checked after a plain restart), the denied-permission Settings path, and a second, different sound that must not be confused with the first (covered only by a host test with generated audio).
- Detection thresholds come from the teammate's engine and were checked on generated and few real sounds; sounds shorter than 0.4 s or outside 800-4500 Hz are not detected automatically (Teach covers them).
- Foreground listening only; keyword detection and knock/loud-sound classes are not in the app.
- English UI only.

## Lessons learned

- Run the real audio path on the phone early: synthetic tests passed while real rooms exposed the noise-floor and vibration problems.
- Keep the engine free of OS dependencies: it made the DSP testable on the PC before any device work.
- Check the toolchain (Node version, host compiler) at the start.

## AI feature disclosure

Not applicable as a model: the app uses classical signal processing (FFT, thresholds, spectrogram templates, similarity) on the device and contains no machine-learning model or AI service.

### Complex signals (several notes, pitches, spacings and dynamics)
- Asked by the user to try more complex signals. Added `tests/complex_sounds.h` and `tools/complex_eval.cpp` (five synthetic signals mixed into real phone room noise) and WAV export for playback.
- Result in simulation: all five signals are detected, taught, recognised (3/3 at 10-25 dB) and not confused. Result on the phone with a PC speaker (audio captured with `DEBUG_CAPTURE_AUDIO`, then replayed on the PC): only the tremolo tone could be taught; chime 1 was not even reported as an alarm, and the trainer rejected the pairs of takes of chime, siren, arpeggio and the irregular pattern (similarity -0.00, 0.08, 0.58, 0.60 against 0.6).
- Follow-up (the user asked to investigate the trainer and to check three takes): spectrograms showed the tones clearly in every take, so the takes were similar to the eye. With the trainer's own debug output, the cause was my gate, not the trainer: the 12 dB above background threshold cut the quiet notes of multi-note sounds out of some takes and not others, so the trainer compared different pieces. A 25-recording phone capture (5 plays x 5 signals, onsets found by matched filtering against the played WAV files) gave 29/50 pairs accepted with 12 dB, 46/50 with 8 dB. Tonality and gap changes did not help, and an oracle cut showed the similarity measure is also sensitive to noise inside the window.
- With three or more takes `TrainFromTakes` now leaves out one take that disagrees if the rest agree (`droppedTake`): 50/50 triples are taught. Replaying the capture after teaching from the first three plays: 24/25 recognised correctly, 0 wrong, 1 missed. On the phone all three complex sounds passed teach, recognition and no-confusion; the earlier Doorbell/Microwave test still passes (one of two runs failed because a naming prompt for an unnamed sound opened on top of the Teach dialog).
- Tests: two real-recording tests (arpeggio takes with one odd one out; irregular pattern takes) were written first and failed, then passed. The vendored trainer is unchanged.
- Lesson: a synthetic test with recorded noise is not a substitute for a real speaker-and-room test; both are kept.

### Naming prompt over the Teach dialog
- Bug found in a test run: the "I've heard this sound before" prompt opened on top of the Teach dialog and blocked saving. Fix in `Index.ets`: while Teach is open the prompt is deferred and shown when Teach closes (only if the sound is still unnamed and not set to "never ask"). Verified on the phone with a script: a second play while Teach was open logged `prompt deferred`, Teach stayed usable, and after Cancel the prompt appeared (screenshot checked). No automated ArkTS test, since the page is UI code.

### Second demo video and a duplicate-sound bug found while recording it
- Recorded a longer demo (4 min 8 s, `dist/safe-n-sound-demo-2.mp4`, not committed, `dist/` is git-ignored): detection, naming, alert, then Teach with three takes for two complex sounds (an irregular note pattern and a pulsing tone), each alerting under its own name. Recorded with `build-host/record_demo2.py` (local helper).
- Bug: the third beep of a recognised sound arrived a moment after the match, was not explained by it and was learned again as a duplicate unknown sound (`snd-2`), which then asked to be named. Found from the log of a failed recording take; a pipeline test reproducing the log timings failed first, then passed after the pipeline remembers the window each match explains (`SoundPipeline.explained`). ArkTS tests: 38.
- Test-rig findings: the PC volume kept falling back to 42% during the session (a tiny tool, `setvol.exe`, now sets it before each play); the first sound after a pause is partly lost unless a stream of near-silence plays first; the laptop speaker plays 1-1.5 kHz weakly (chime and siren reached the phone at -24 to -27 dB, below the trainer's 15 dB over room noise), so the film uses signals with higher pitches. These explain several of the unstable results of the earlier speaker tests.

## Merge of the second concept (Ambient) with Safe'n'Sound

### Contribution log of the concept repository (merged from INawrot/Ambient, `AI_WORKFLOW.md` there)
All entries used Claude Sonnet 5.5 and were reviewed by a human of the team. Summary of their log:
- 2026-10-03 13:11 UTC: basic C++ sound-recognition engine with CMake and tests.
- 2026-10-03 14:19 UTC: custom sounds (saving and recognising later), with tests.
- 2026-10-03 18:08 UTC: speech module (text for the deaf, keyword rules such as "help", "watch out" with vibration), with tests.
- 2026-10-03 19:29 UTC: reliability and safety review, new safety tests.
- 2026-10-03 23:22 UTC: new sounds: crying child, wailing siren, chirping alarm, human scream, with a test.
- 2026-10-04 05:36 UTC: DevEco application with the UI; limitation noted by them: two language options, the recognition system sometimes makes mistakes. Their README states that the ArkUI files, the kit calls and the hvigor build had not been run on a device or compiler for ArkTS.

### This session (Claude Sonnet 5.5 in Claude Code), user's instructions
Take from the concept: the logo, the functionality (background operation, push notifications, loud-sound detection, speech detection); keep this project's GUI style, colours, chart and design; keep the recording of own sounds if it is the better one (to be checked); combine into one project, test the functions as before; ask questions when in doubt. Decisions given by the user: five tabs; keywords first in English (the system recogniser returns text for English words even though its language is Mandarin); captions show the recogniser's text as it is; all classes alert by default (including knock and loud sound) with a guard against the phone's own noise; notifications for every class and for named sounds.

### What was done and how it was checked
- **Engine swap.** The concept's engine replaced the vendored one. Comparing the two trainers on 25 real phone recordings (five multi-note signals x five plays, speaker to phone, noisy room) decided the teaching policy: the concept trainer is more lenient (lower thresholds, drops outliers, steps the threshold down) and recognised 25/25 plays against 24/25 for the earlier one, but it accepted 47/50 pairs of *different* sounds because it silently keeps one of two disagreeing recordings. Added strict options to the vendored trainer (`allow_single_fallback`, `max_dropped`, `dropped`), minimum pairwise similarity 0.55 chosen from a sweep: 46/50 pairs and 50/50 triples of the same sound taught, 0/50 mixed pairs accepted, 25/25 recognised. The concept's own core test expected rejection of contradicting recordings and failed on its own trainer; it now uses the strict options.
- **Wrapper and NAPI.** Built-in classes pass through as events; speech pipeline functions added to the NAPI module; four wrapper tests written first (siren, chirp, loud sound, knock) and failed before the mapping.
- **App logic (TDD, ArkTS tests 72).** Classes, settings, catalog records for classes, pipeline handling, caption board, caption text pieces and the self-noise guard each got tests that failed first. A siren is first reported by the engine as a loud sound: a held loud sound that a siren cancels was added after seeing it in a replay of the engine on a test signal.
- **Phone tests (real device, PC speaker).** Siren, short beep, loud sound and the engine's own baby-cry signal were detected and alerted; scream and knock were not reproduced through the laptop speaker. Settings switch off/on verified. Background and screen-off listening verified, including the notifications on the lock screen. Speech: the recogniser (Mandarin model) returned English text; "watch out" raised the keyword alert; typed test input and a user-added keyword verified, the keyword survived a restart. Regression of the earlier Teach test passed (14/15 steps in the last run; the one miss was the first alert right after teaching, which depends on the sound level at the phone).

### Failures and lessons from this merge
- The engine's loud-sound detector made the phone's own screen taps and vibration into alerts (found because the alert card of a knock covered the card of the recognised sound during a test): added a self-noise guard (vibration plus tail and a short window after touches, also inside dialogs) with tests.
- The system speech recogniser stopped the microphone from delivering sound when the app was in the background (system log: `AudioCapturer create failed`): speech is now stopped when the app goes to the background and resumed in the foreground; without speech the background path worked at once, which showed the cause.
- Seen in a phone log: the first beep of a taught sound alerted as "Beep or chirp" before the match arrived, so the user would get two alerts; chirps are now held like loud sounds and explained by a recognised sound or a siren (tests first, then 16/16 steps of the teaching run with no extra chirp alert).
- A transient recogniser error stayed on screen for ever: cleared when text arrives again.
- Test rig lessons: several "failures" were the test setup (the PC volume was muted by the user once, the phone was left on its lock screen after a screen-off test, tab coordinates changed with five tabs). The harness now unlocks the phone and sets the volume before each play.
- Honest limits: scream and baby-crying detection are heuristics of the engine and were not verified on real voices; the speech recogniser is Mandarin-based; knock detection was not exercised with a real knock.

## Submission repository and test recording (Claude Sonnet 5.5 in Claude Code)
- On the user's request a new repository was created from the committed tree of the merge branch, with one commit on one branch, a short README (how to run first), the signed release package in `release/` and a demo recording. The package was built from a clean export of that tree (the repository has no signing data; the local signing profile was only substituted while building) and its signature verified with the SDK's `hap-sign-tool`; ArkTS tests (75), C++ tests (35 plus the engine suites) and lint were run again in that clean tree. The full commit-by-commit history stays in the development repository linked from the README.
- The test recording was made with the phone's system screen recorder (control-center tile, driven with `uitest`), because the phone has no command-line recorder and a screenshot loop cannot give 60 fps. The recorder produces variable-frame-rate H.264 (60 fps while the screen animates); it was cropped (the recorder's own status-bar pill), trimmed and converted to constant 60 fps with ffmpeg; the microphone track of the recorder is kept so the alarms can be heard. No captions or overlays were added.
- Recording lessons: the recorder's settings (microphone switch) persist between runs, so the script reads the switch colour before clicking; the notification list and the app shown in the background step were chosen/cleared so that the video shows no unrelated personal content; Knock and Loud sound were switched off in the video because the PC speaker's clicks and the screen taps are heard as such; the same sound played from the speaker varies by several dB between plays, which made the naming prompt fail in the recorded run (a retry did not help because the step stopped at the missing prompt) - this is reported in the docs rather than hidden.
