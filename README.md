# Safe'n'Sound

HarmonyOS phone app (API 20+) for deaf and hard-of-hearing people. It listens through the microphone and turns
alarms, sirens, beeps, a crying baby, knocks, doorbells and your own taught sounds into vibration, notifications and
on-screen cards, adds live captions with keyword alerts ("help", "watch out"), and keeps listening in the background.
Everything runs on the phone; audio is never stored or sent.

## Run it

You need a HarmonyOS phone (or emulator) with USB debugging and `hdc` on your `PATH`.

```bash
# 1. install the signed package (signed for the registered test phone, profile valid until 2026-10-17)
hdc install -r release/safe-n-sound-release-signed.hap

# 2. start the app (or tap its icon), tap "Start listening", allow microphone and notifications
hdc shell aa start -a EntryAbility -b com.example.safe_n_sound
```

Then play an alarm or a siren near the phone (a speaker, not headphones): the phone shows a card, vibrates and posts
a notification. Step-by-step workflows: [docs/USER_GUIDE.md](docs/USER_GUIDE.md).

**Build from source** (DevEco Studio 6.1.1 with SDK 6.1.1(24), minimum API 20; Node.js 22):

```bash
ohpm install --all
hvigorw assembleHap --mode module -p product=default -p module=entry@default --no-daemon
hdc install -r entry/build/default/outputs/default/entry-default-signed.hap
```

The repository has no signing data, so a fresh build is unsigned: in DevEco Studio use File > Project Structure >
Signing Configs > "Automatically generate signature" (phone connected), then build again. Details:
[docs/BUILD_DETAILS.md](docs/BUILD_DETAILS.md).

**Tests:** `bash scripts/arkts-tests.sh` (75 ArkTS tests) and `scripts\host-tests.cmd` (C++ engine and wrapper tests,
Windows with Visual Studio Build Tools).

## More

- Demo recording of the app under test (60 fps): [demo/safe-n-sound-tests.mp4](demo/safe-n-sound-tests.mp4); screenshots in [docs/screenshots](docs/screenshots)
- Architecture, technology and the physics behind each function: [docs/TECHNICAL_OVERVIEW.md](docs/TECHNICAL_OVERVIEW.md)
- AI tools, prompts, workflow and validation: [AI_WORKFLOW.md](AI_WORKFLOW.md)
- Platform capabilities used: microphone capture, vibrator, notifications, background continuous task, on-device speech
  recogniser, native C++ (N-API)
- Commit-by-commit development history: https://github.com/Xp4blos/safe-n-sound
