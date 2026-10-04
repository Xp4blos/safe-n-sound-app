# Safe'n'Sound - user guide

Step-by-step workflows for every function. The app has five tabs: **Listen**, **History**, **Captions**, **My sounds**
and **Settings**.

## 0. Before you start

* Use a phone with the app installed (`hdc install -r safe-n-sound-release-signed.hap`, or build from source; see the
  README).
* The first time you tap **Start listening** the app asks for the **microphone** and for **notifications**. Allow both.
  Without the microphone the app cannot hear anything; without notifications it can only vibrate and show cards.
* Everything is analysed on the phone. Audio is never stored or sent anywhere.

---

## 1. Listen: hear and see what is happening

1. Open the **Listen** tab and tap **Start listening** (or the big microphone).
2. The status chip at the top turns green and says **Listening**. The chart below shows the last 8 seconds of sound:
   each column is a moment in time, each row a pitch (low at the bottom), brighter means louder.
3. When something is detected, the last columns of the chart are highlighted and an **alert card** appears at the top
   (it stays for a minute, tap it to dismiss).
4. The **Last detected** card at the bottom shows the latest sound.
5. Tap the microphone or **Stop listening** to stop.

If you denied the microphone the tab explains it; tap the button again and the app opens the system settings page.

---

## 2. A new sound: unknown, then named, then alerting

Use this for a doorbell, a washing machine, a smoke alarm, a microwave: anything that beeps.

1. Start listening and make the sound (or let it happen).
2. After about four seconds the phone vibrates once (a short pulse) and **History** shows a new entry,
   **Unknown sound**.
3. The next time the same sound is heard, the app asks **"I've heard this sound before. Do you want to name it?"**
   * **Name it** opens the details: type a name (for example *Doorbell*), keep **Alerts** on and tap **Save**.
   * **Not now** asks again later (at most every 10 minutes).
   * **Don't ask again for this sound** stops the question for this sound.
4. From now on, whenever that sound is heard the phone vibrates with three long pulses, shows a notification
   **"Doorbell detected"** and an alert card. The same sound alerts at most once every 15 seconds, but every hearing is
   counted.
5. The sound is now listed in **My sounds**.

---

## 3. Teach a sound on purpose

Use this when you do not want to wait for two hearings, or for sounds the app does not catch by itself (very short or very
quiet ones).

1. Prepare the sound source (a speaker or the real device) 10-30 cm from the phone, in a reasonably quiet room.
2. On the **Listen** tab tap **Teach a sound**. (If the phone is not listening yet, it starts.)
3. Tap **Record take 1**. You have 6 seconds: play or make the sound once while the dialog says it is recording.
4. The dialog shows **Take 1 captured** or tells you why the take is not usable (for example "no clear sound above the
   background noise": move closer or make the sound louder).
5. Tap **Record take 2** and repeat. Two good takes are enough; a **third take** is allowed and helps in a noisy room:
   if one of three disagrees with the others, the app leaves it out. If two takes are too different, the app asks you to
   record again.
6. Type a name in the name field and tap **Save sound**. The sound appears in **My sounds** and alerts like any named
   sound.

Tip: record every take the same way (same distance, same sound, one occurrence per take).

---

## 4. Built-in sounds: siren, scream, baby crying, beep, knock, loud sound

These need no teaching. When one is heard, the phone vibrates with a pattern of its own, shows a notification
(for example **"Siren detected"**), an alert card, and an entry in **History** that counts how often it was heard.

To choose what you want to be warned about:

1. Open **Settings**, section **Alert me about**.
2. Switch classes on or off: Siren, Scream, Baby crying, Beep or chirp, Knock, Loud sound.
3. A switched-off class is ignored completely (no history entry, no vibration).

Notes: knocks and loud sounds ignore the phone's own noise, i.e. its vibration and the taps of your finger on the screen.
Learned and taught sounds always alert; switch them off one by one in **My sounds**.

---

## 5. Captions and keywords (speech)

The phone turns speech into text and reacts to important words such as **help** or **watch out**.

1. Start listening on the **Listen** tab (speech needs the microphone to be running; the **Captions and keywords**
   switch in **Settings** must be on).
2. Open **Captions**. The status line says **Listening for speech**. Spoken sentences appear as lines; keywords are shown
   in red and bold.
3. When a keyword is heard, the phone vibrates with the keyword's own pattern, shows a notification **"Keyword: help"**
   and a card at the top of the Captions tab and of the Listen tab.
4. The system recogniser is Mandarin-based, so some lines can be Chinese characters; English words such as *watch out*
   or *good morning* usually appear as English text and the English keywords work on them.
5. **Add a keyword:** in the **Keywords** card type a word or phrase, choose its vibration (**SOS**, **Rapid**, **Long**,
   **Double**), tap **Add**. **Remove:** tap the **x** on its chip. Your list is saved.
6. **Test without speaking:** in **Test input (simulated)** type words and tap **Simulate**. This feeds the same keyword
   engine as real speech and is only meant for testing.
7. **Clear** (top right) empties the caption list.

The recogniser works only while the app is on screen; in the background the app keeps detecting sounds but does not
transcribe speech.

---

## 6. Listening in the background and with the screen off

1. In **Settings** check that **Keep listening in the background** is on (it is by default).
2. Start listening on the **Listen** tab.
3. Press **Home**, switch to another app, or lock the phone. A status-bar icon and the system notification
   **"A recording task is in progress"** show that the microphone is still in use.
4. Sounds are still detected: the phone vibrates and posts notifications such as **"Siren detected"**, which you can
   read on the lock screen.
5. To stop, return to the app and tap **Stop listening** (or switch **Keep listening in the background** off).

Turn the setting off if you want the microphone to stop as soon as the app leaves the screen.

---

## 7. History and My sounds

* **History** lists everything the phone heard, newest first: unknown sounds, named sounds and built-in classes, with how
  many times and when last. Tap a learned entry to name it, rename it, switch its alerts or delete it.
* **My sounds** lists the named sounds with an **Alerts** switch each. Tap one to rename or delete it. Deleting makes the
  phone forget it immediately.

---

## 8. Settings overview

| Setting | Effect |
|---|---|
| Alert me about | which built-in classes alert |
| Vibrate | vibration for detections and keywords |
| Notifications | system notifications for detections and keywords (cards on screen always show) |
| Keep listening in the background | microphone stays on when the app is not on screen |
| Start listening when the app opens | no need to tap Start |
| Captions and keywords | run the speech recogniser while listening |
| React to words while still speaking | keyword alert on interim results (fastest) |

---

## 9. Trying it with a speaker (for demos and tests)

* Use a **speaker, not headphones**, 10-30 cm from the phone, in a quiet room. A laptop speaker reproduces high tones
  (2 kHz and above) much better than low ones: a smoke-alarm beep works, a baby's cry (fundamental about 400 Hz) does
  not.
* Alarm and chirp: play a beeping tone of 2-3 kHz.
* Siren: play a wailing siren (pitch rising and falling for several seconds).
* Loud sound: a burst of noise (a crash, applause).
* Knock: a real knock on the table next to the phone.
* Keywords: say *watch out* clearly near the phone, or use the simulated test input.

## 10. Troubleshooting

| Problem | What to do |
|---|---|
| "Microphone access is needed" | Allow the microphone; tap the button again, the app opens the system settings page if needed. |
| Nothing is detected | Check the status chip says Listening; move the source closer; sounds quieter than about 15 dB above the room noise are not detected. |
| No notifications | Allow notifications in the system settings; the app still vibrates and shows cards. |
| A taught sound is "not usable" | Move closer or make it louder, record each take the same way, add a third take. |
| Captions show Chinese characters | Expected: the recogniser is Mandarin-based; English keywords still work. |
| Listening stops in the background | Check **Keep listening in the background** is on; some phones also need the app excluded from battery optimisation. |
