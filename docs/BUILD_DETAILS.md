# Safe'n'Sound - setup, build, signing and checks in detail

The short instructions are in the [README](../README.md); this page keeps every detail.

A HarmonyOS phone app for deaf and hard-of-hearing people. The phone listens to its surroundings, detects
signal sounds (beeps, buzzers, alarms, chimes, sirens, a baby crying, knocks, loud sounds), remembers them, lets you
name them, alerts you with vibration and a notification, and turns speech into live captions that react to words
such as "help" and "watch out". It keeps listening in the background. Everything runs on the device: no cloud,
no stored audio. Only compact sound fingerprints (feature numbers) are saved.

Documentation: [technical overview](TECHNICAL_OVERVIEW.md) (technology, libraries, the physics and the
processing behind every function) and [user guide](USER_GUIDE.md) (step-by-step workflows).

Core loop: **detect -> remember -> recognise repeat -> offer to name -> alert on named sound.**
The app never relies on audio feedback: every state is visible on screen and every alert vibrates.

## Features

- **Listen tab:** tap the microphone (or the Start/Stop button) to listen. An 8-second live spectrum
  (frequency bars over time) moves with the room sound and highlights the moments the phone reacted to.
  A card shows the last detected sound; a large card appears when a named sound is heard.
- **Remembering sounds:** a new tonal signal becomes an "Unknown sound" in History; the phone learns its
  fingerprint from the audio around it (held in memory only). Hearing it again counts it on the same entry.
- **"I've heard this sound before":** on the 2nd occurrence of an unnamed sound you are asked to name it
  (Name it / Not now / Don't ask again), at most once per 10 minutes per sound.
- **Teach a sound:** record 2-3 takes of a sound on purpose, see on screen whether each take was captured,
  name it. If the takes differ too much you are told to repeat them.
- **Alerts:** a named sound vibrates (3 long pulses), posts the notification "<Name> detected" with the
  time and shows a card; 15 s cooldown per sound; each sound has an alert switch. Unknown sounds give one
  short vibration.
- **History / My sounds:** every sound with count, last time and a small pattern chart; details in plain
  words (for example "High-pitched, 3 beeps per second, about 2.1 s long"), rename, alert switch, delete.
- **Built-in sound classes:** siren, scream, baby crying, short beep or chirp, knock and loud sound are detected
  without teaching, appear in History, alert with their own vibration pattern and notification, and can be switched
  off one by one in Settings. Knocks and loud sounds ignore the phone's own vibration and the taps on its screen.
- **Captions and keywords:** the system speech recogniser (on the device) feeds live captions; keywords such as
  "help" and "watch out" (also in Chinese) are highlighted and trigger their own vibration and notification. Words
  and vibration patterns can be added and removed; a typed-text input (marked as a simulation) feeds the same engine.
- **Background listening:** with the screen off or the app in the background the microphone keeps running as a
  system "recording task" and alerts keep arriving as notifications.
- **Settings:** which classes alert, vibration, notifications, background listening, start on open, speech.
- Data survives restarts. Room hum and the phone's own noise are not detected as sounds.

## Platform features used

Audio capture (`AudioCapturer`, 16 kHz mono) with a runtime microphone permission and privacy text,
vibrator, notifications (`notificationManager`), local storage (`Preferences`), NAPI (ArkTS <-> C++),
`@kit.AbilityKit` lifecycle, background continuous task (`backgroundTaskManager`, `AUDIO_RECORDING`) with the
`KEEP_BACKGROUND_RUNNING` permission, and the Core Speech Kit recogniser (`speechRecognizer`, on-device).

## Architecture

```
AudioCapturer (ArkTS) --PCM--> NAPI (libsafensound.so) --> SoundEngine (C++)
                                  ambient::Detector (alarm, chirp, siren, scream, cry, knock, loud sound,
                                  learned/taught sounds), ring buffer, 16-band live spectrum, SoundProfile, learn/train
ArkTS: SoundPipeline -> SoundCatalog (occurrences, naming, cooldown, prompt rules) -> SoundStore (Preferences)
       AlertService (vibrator, notifications, background task)
       PCM -> SystemRecognizer (Core Speech Kit) -> native keyword rules -> CaptionBoard -> Captions tab, alerts
       UI: Listen / History / Captions / My sounds / Settings, dialogs
```

- `entry/src/main/cpp/ambient` - the team's sound engine (prior code, see below): FFT, detectors, custom-sound
  spectrogram templates, trainer, keyword (speech) rules.
- `entry/src/main/cpp/wrapper` - `SoundEngine`: ring buffer (last 10 s, memory only), live spectrum, learning
  from a detected sound, teaching from takes, restoring stored sounds.
- `entry/src/main/cpp/profile` - FFT frame analysis and `SoundProfile` (pitch, duration, repetition, modulation,
  envelope) used for the plain-words description.
- `entry/src/main/cpp/napi` - thin NAPI bridge; typings in `entry/src/main/cpp/types/libsafensound`.
- `entry/src/main/ets` - `model/` (pure logic: catalog, classes, settings, self-noise guard, caption text),
  `services/` (audio, pipeline, alerts, storage, speech), `components/`, `pages/`.
- Data flow: an `alarm` event is held for 3.7 s; if a stored sound explains it (a `custom` event) it is counted,
  otherwise a new unknown sound is created and its template learned from the ring buffer. Only templates and
  profile numbers are stored (as base64 in Preferences JSON); raw audio never leaves memory.
- Design and plans: `docs/superpowers/specs/` and `docs/superpowers/plans/`.

## Required tools

| Tool | Version used | Needed for |
| --- | --- | --- |
| DevEco Studio | 6.1.1.280 | HarmonyOS SDK, `hvigorw`, `ohpm`, `hdc`, emulator |
| HarmonyOS SDK | target 6.1.1(24), compatible 6.0.0(20) (API 20 minimum) | building the app |
| Node.js | 22 or newer | `devecocli` (build, lint); DevEco's bundled Node 18 runs `hvigorw` |
| Visual Studio 2022 Build Tools (MSVC) | 17.x | C++ tests on the PC (optional) |

## Setup from a clean checkout

1. Install the tools above and put DevEco's `tools\hvigor\bin`, `tools\ohpm\bin` and the SDK's
   `openharmony\toolchains` (`hdc`) on `PATH`.
2. `git clone https://github.com/Xp4blos/safe-n-sound-app.git && cd safe-n-sound-app && ohpm install --all`
3. Signing: `build-profile.json5` is committed with an empty `signingConfigs`, so a fresh clone builds an
   **unsigned** `.hap` (`entry-default-unsigned.hap`). To run on a device or emulator you need a **signed**
   build: open the project in DevEco Studio, choose File > Project Structure > Signing Configs >
   "Automatically generate signature" (device or emulator connected, Huawei ID signed in), then build again.
   DevEco writes your personal signing data into `build-profile.json5`. Keep it out of commits with
   `git update-index --skip-worktree build-profile.json5`.

## Build, test, install, run

```bash
# C++ tests on the PC (MSVC + the SDK's cmake/ninja; set DEVECO_NATIVE if the SDK is elsewhere)
scripts\host-tests.cmd

# ArkTS unit tests (hvigor + hypium)
bash scripts/arkts-tests.sh

# Build the .hap -> entry/build/default/outputs/default/entry-default-signed.hap
hvigorw assembleHap --mode module -p product=default -p module=entry@default --no-daemon
# (equivalent with Node 22+: devecocli build)

# Lint (Node 22+)
devecocli check lint .

# Install and launch on a connected device or emulator
hdc install -r entry/build/default/outputs/default/entry-default-signed.hap
hdc shell aa start -a EntryAbility -b com.example.safe_n_sound
```

On first start the app asks for notification permission and the microphone permission (with a privacy
explanation). If you deny the microphone, the Listen tab explains why it is needed and the next try opens the
system Settings page.

## Engine checks on real audio

`build-host\tests\wav_cli.exe file.wav` (built by `scripts\host-tests.cmd`) runs the team's detector on a WAV file, and
`build-host\tests\replay_cli.exe recording.wav [threshold]` replays a recording through the same `SoundEngine` the app
uses: it learns the first alarm and prints every alarm and every recognition. To record audio on the phone, set
`DEBUG_CAPTURE_AUDIO` to `true` in `entry/src/main/ets/services/AudioService.ets`; the app then writes
`debug_capture.pcm` (16 kHz mono 16-bit) into its files folder, which can be pulled with `hdc file recv`.
`entry/src/main/cpp/tests/data/phone_alarm_3x.wav` is such a recording (three plays of an alarm in a noisy room) and a
unit test checks that the first play is learned and the other two are recognised.

### Complex signals

`build-host\tests\complex_eval.exe` (run from the repository root) mixes five synthetic multi-note signals (a decaying two-note
chime, a rising arpeggio that gets louder, a two-tone siren, an irregular pattern of notes with uneven gaps, and a
tremolo tone) into real room noise from the phone recording at 25/20/15/10 dB above the noise. In that setup every
signal is detected, taught from two takes, recognised 3 of 3 times at every level, and never mistaken for another one.
`complex_eval.exe --export <dir>` writes the signals as WAV files for playback tests, and
`complex_eval.exe --takes <phone.wav> <start s>...` cuts 6 s takes from a phone recording and runs the Teach checks.
Played through a PC speaker and recorded by the phone in a real room the picture is harder: broadband room noise
hides the quiet notes and the trainer's cut differed from take to take. Measured on 25 recordings (5 plays of each
signal, `complex_eval.exe --takes`): teaching from two takes works for 46 of 50 pairs (29 of 50 before the gate was
changed to 8 dB above the background) and from three takes for 50 of 50 triples, because one take that disagrees with the
others is left out. After teaching each sound from its first three plays, 24 of the 25 plays in the recording were
recognised under the right name, none under a wrong one (`complex_eval.exe --recog`). On the phone, three complex sounds
(chime, arpeggio, irregular pattern) were taught from two takes each and each alerted under its own name twice.

## Signed .hap

A signed package is what a device or emulator accepts. It is produced like this (the signing data stays on the
machine, it is never committed):

1. Open the project in DevEco Studio, connect the phone (or start the emulator) and sign in with a Huawei ID.
2. File > Project Structure > Signing Configs > tick "Automatically generate signature" > Apply. DevEco creates a
   debug certificate (alias `debugKey`) and a debug profile in `%USERPROFILE%\.ohos\config` and writes them into
   `build-profile.json5` (keep that change out of git: `git update-index --skip-worktree build-profile.json5`).
3. Build and verify both variants with one command:

   ```bash
   bash scripts/make-signed-hap.sh
   ```

   This runs `hvigorw assembleHap` for `debug` and `release` (`-p buildMode=release`), copies the results to
   `dist/safe-n-sound-debug-signed.hap` and `dist/safe-n-sound-release-signed.hap`, and checks each signature with
   the SDK's `hap-sign-tool.jar verify-app` (expected: `Verify success`).
4. Install: `hdc install -r dist/safe-n-sound-release-signed.hap`.

What this signature is, honestly: the certificate and profile are the auto-generated **debug** ones. The profile is
bound to the bundle `com.example.safe_n_sound` and to the devices that were registered when it was generated, and it
is valid for about two weeks (the profile of the submitted build: 2026-10-03 to 2026-10-17, one registered device).
So the package installs on that phone only; to install it on another phone or an emulator, repeat step 2 with that
device connected and rebuild. Publishing to other users would need a release certificate and profile from AppGallery
Connect, which this project does not have. The signed release build is in `release/safe-n-sound-release-signed.hap` and
attached to the GitHub release [v1.0.0](https://github.com/Xp4blos/safe-n-sound-app/releases/tag/v1.0.0) (note
that the profile inside it contains the registered device's ID). `dist/` is git-ignored.

## How to trigger a detection for a demo

The detector reacts to **tonal signals of 0.4 s or longer between 800 and 4500 Hz** (smoke-alarm and appliance
beeps, door chimes, buzzers). Use a second device or a speaker (not headphones) and hold it 10-30 cm from the
phone's microphone in a quiet room; search the web for "smoke detector beep", "microwave beep" or a 2 kHz tone.

1. Tap the microphone, allow the permissions, stay quiet for 2 seconds.
2. Play the sound for 3-5 seconds: after about 4 seconds it appears in History as "Unknown sound".
3. Play it again: the "I've heard this sound before" dialog appears; tap Name it and call it "Doorbell".
4. Play it a third time: the phone vibrates, shows the "Doorbell detected" notification and card, and
   History shows the same entry with count 3.
5. For a sound shorter than 0.4 s, use **Teach a sound** on the Listen tab instead.

## Demo recording

`demo/safe-n-sound-tests.mp4` is a screen recording of the phone (60 fps, with the sound of the room, status bar cropped,
no added captions) made with the phone's system screen recorder while an automated test run exercises the app: start of
listening, Settings (Knock and Loud sound switched off because the PC speaker's clicks would trigger them), an alarm sound
played from the PC speaker, teaching a sound and its alert, a siren, captions with a spoken and a typed keyword,
background listening with the notification, My sounds and History. The sounds are played from a PC speaker next to the
phone. In this run the "I've heard this sound before" prompt did not appear for the doorbell sound (the speaker-to-phone
setup is not repeatable: the level of the same sound varies by several dB between plays); that flow is covered by the
earlier recording and by the tests. The vibration itself cannot be filmed. `docs/screenshots/` has stills from the run.

## Known limitations

- Background listening is a system recording task and depends on the phone: it was verified with the screen off and the
  app in the background on the test phone; some phones also need the app excluded from battery optimisation.
- The speech recogniser is Mandarin-based (the only language the platform offers on the device: queried on the test
  phone). English words such as "watch out" usually come back as English text and the English keywords work, but
  a single word such as "help" is recognised less reliably than a phrase; captions can show Chinese characters. The
  recogniser only runs while the app is on screen (it disturbs the microphone in the background).
- Scream and baby-crying detection are heuristics of the engine, tuned on synthetic audio. Through a laptop speaker
  a baby's cry (fundamental about 400 Hz) and a synthetic scream were not reproduced well enough to be detected
  there (the engine's own cry and scream test signals are detected when read from file); knock detection could not be
  exercised with a speaker.
- Detection thresholds come from the team's engine; the similarity needed to recognise a learned sound again is 0.6
  (the engine default 0.8 missed quieter real repeats), tuned on recordings of one alarm sound in one noisy room; sounds shorter than 0.4 s,
  very quiet sounds, and sounds outside 800-4500 Hz are not detected automatically (Teach covers them).
- Complex multi-note sounds are detected and can be taught, but in a noisy room two takes occasionally disagree
  (4 of 50 pairs in the recordings above); a third take lets the app leave the odd one out. One of 25 plays of an
  irregular pattern was not recognised afterwards.
- Knocks and loud sounds are level-only detections: they are ignored while the phone vibrates and for a moment after a
  tap on its screen, but a real loud noise nearby will be reported (switch them off in Settings if it is too much).
- English UI only. Lint reports 7 style warnings (prefer `@Builder` over small components) and no errors.

## License

There is deliberately no license file: this is the team's own work, all rights reserved by the authors.

