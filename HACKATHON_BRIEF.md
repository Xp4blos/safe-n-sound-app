# Hackathon Brief

The user owns the decisions recorded here. Unresolved fields may remain blank; do not ask the user to complete them until the current work depends on them.

## Pitch

**User problem:** People who are hard of hearing miss the signals that matter most in daily life: a doorbell or knock, a smoke alarm, a siren, a crying baby, someone calling their name. Missing them is a safety risk and a source of exclusion. Existing solutions are single-purpose hardware that handle one sound each.

**Desired demonstration:** [Not decided yet]

**Lead challenge theme:** [Intelligent Experiences / Spatial Experiences / Human-Centric Technology - not chosen yet]

**Distinctive platform capability:** Microphone capture, vibration and notifications on a Huawei phone, with sound analysis done on the device by a native C++ engine (no cloud, no stored audio).

## Target

- Platform: HarmonyOS
- API level: compatible SDK 20 (6.0.0(20)), target SDK 24 (6.1.1(24))
- Device type: phone
- Validation target: physical Huawei phone PLR-AL00 (API 26)

## Intended user flow

1. Open the app and tap the microphone or Start listening (microphone and notification permissions are requested).
2. The Listen tab shows an 8-second live spectrum; a detected signal sound appears in History as "Unknown sound".
3. When it is heard again the app asks "I've heard this sound before. Do you want to name it?"; the user names it.
4. When the named sound is heard again the phone vibrates, shows a notification "<Name> detected" and a card.
5. "Teach a sound" records 2-3 takes of a sound on purpose and saves it under a name.
6. History shows every sound with details in plain words; My sounds lists the named sounds with alert switches.
7. Built-in classes (siren, scream, baby crying, beep, knock, loud sound) alert on their own, each with its own vibration and notification; Settings switches them on or off.
8. Captions shows live speech as text with keywords such as "help" and "watch out" highlighted; they vibrate and notify. The keyword list can be edited.
9. With Keep listening in the background on, all of this continues with the screen off, as a system recording task.

## Acceptance checks

- [x] The live view moves with room sound (verified on the phone)
- [x] A real beep or doorbell appears in History as "Unknown sound" (verified on the phone, see demo video)
- [x] Playing it again shows the naming prompt; naming it works (verified)
- [x] A third time vibrates, notifies "<Name> detected" and counts on the same entry (alert card and count verified; the notification verified on the lock screen; the vibration itself cannot be seen on screen)
- [x] A taught sound alerts under its own name and is not confused with the first (verified, repeated runs)
- [x] A siren, a short beep and a loud sound are detected by class and alert (verified on the phone with a PC speaker); baby crying with the engine's own test signal; scream and knock not reproduced through a laptop speaker
- [x] A class switched off in Settings gives no alert; switched on again it alerts (verified)
- [x] Background listening: with the app in the background and with the screen off, beep and siren alerts and notifications arrive (verified; system recording notification and status-bar icon visible)
- [x] Speech: real English speech ("watch out") raises the keyword alert and is shown highlighted in Captions; a typed test sentence raises "help"; a keyword added in the UI works and survives an app restart (verified)
- [ ] Room hum, speech and the phone's own vibration create no entries over a long time (the phone's own vibration and screen taps are filtered; long-term hum/speech not yet checked)
- [ ] Named sounds are still there after restarting the app (keyword rules verified; sounds not re-checked after the merge)

## Scope boundaries

- In scope: detecting signal sounds (beeps, buzzers, alarms, chimes) and the built-in classes (siren, scream, baby crying, chirp, knock, loud sound); remembering sounds by fingerprint; history; naming; teaching sounds; vibration and notification alerts; background listening; live captions and keyword alerts (English keywords first); Listen, History, Captions, My sounds and Settings tabs; live spectrum.
- Out of scope: cloud services, audio storage, a recogniser for languages other than the platform's Mandarin model (English keywords work on the text it returns).
- Mocked or simulated behavior: the Captions tab has a typed-text test input that feeds the keyword engine; it is labelled "Test input (simulated)". Unit tests and checks use synthetic audio, and the phone tests play sounds from a PC speaker.

## First-minute narrative


**User problem:** People who are hard of hearing miss the signals that matter most in daily life: a doorbell or knock, a smoke alarm, a siren, a crying baby, someone calling their name. Missing them is a safety risk and a source of exclusion. Existing solutions are single-purpose hardware that handle one sound each.

**Desired demonstration:** [Not decided yet]

**Lead challenge theme:** [Intelligent Experiences / Spatial Experiences / Human-Centric Technology - not chosen yet]

**Distinctive platform capability:** Microphone capture, vibration and notifications on a Huawei phone, with sound analysis done on the device by a native C++ engine (no cloud, no stored audio).
