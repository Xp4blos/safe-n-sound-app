# Safety hardening - change log

## Bug fixes
- speech.cpp: `fired_in_utt` is clamped to the current number of occurrences, so a keyword the
  recognizer revised away can no longer swallow a later real "help" (alert was silently lost).
- Built-in keyword rules ("help", "watch out", ...) now use a 1 s cooldown (was 2 s).
  Custom rules keep their own `cooldown_s`.

## Thread safety / lifetime
- Detector: every public method is serialised by a mutex (UI thread may add/remove custom
  sounds while the audio thread runs `process()`). Detector is no longer copyable.
- SpeechSession: exceptions from the app callback are swallowed; callbacks are ignored after
  destruction starts (`closed_`). `SpeechRecognizer::stop()` must join its callback thread.

## Silent-failure protection
- `Detector::stream_time_s()` - compare with wall-clock time in the app to detect a dead mic.

## Input validation
- `Config::validate()` rejects NaN, non-positive durations/dB values, bad `background_alpha`,
  absurd sample_rate / frame_size.
- `validate_sound()` rejects out-of-range refractory/min_level/frame/sample rate.
- Stored sounds: format v2 = v1 + CRC32. v1 data is still readable. Trailing bytes rejected.
- Trainer: after training, the template must recognise each of its own recordings.

## Detection
- A custom-sound match only hides built-in events that are at most 6 dB louder than the match.
  (Defensive: in tests the matcher already stops matching before this limit is reached.)

## Build / tests
- CMake: `AMBIENT_WERROR`, `AMBIENT_SANITIZE` options, stack protector, `_GLIBCXX_ASSERTIONS`.
  Removed the speech_test.cpp path fallback. CRLF -> LF in speech files and tests.
- New tests/test_safety.cpp: chunk invariance (1..50000 samples), config validation, stored-sound
  integrity (CRC, v1 compatibility, trailing bytes), noisy floor / mains hum / DC offset / loud fan /
  clipping, alarm frequency+duration boundaries, bang inside a custom window, stream clock,
  concurrent add/remove while processing.
- speech_test.cpp: regression test for the lost-"help" bug; cooldown expectations updated.

## New sound classes (merged)
- `EventType::Chirp`: short (50-350 ms) steady-pitch beep in the alarm band, e.g. smoke-alarm
  low-battery chirp. A repeat at the same pitch 5-120 s later raises confidence. Chirps are held back
  while custom sounds are registered, so a learned beep is not also reported as a chirp.
- `EventType::Cry`: baby crying - harmonic voice (f0 300-700 Hz, subharmonic check rejects adult voices),
  pulses >= 0.35 s with moving pitch; 2 pulses within 2 s (or one >= 1.5 s) = one event per episode.
  Cry no longer also produces `LoudSound`. Needs FFT resolution <= 50 Hz/bin, else it disables itself.
- All thresholds are `Config::chirp_*` / `cry_*` (validated NaN-safe). Heuristic, tuned on synthetic audio only.
- A beep is no longer reported as `Knock` (when `detect_chirp`); a tone only counts as "tonal" for
  Knock/LoudSound suppression if the *loudest* frame is tonal, so a bang on top of a beep is still reported.
- State machine: a sudden jump (>= `onset_db` over previous frame and background) while `Sustained`
  now starts a new transient (second knock / bang right after another sound was previously ignored).
- tests/test_sounds.cpp (new); test_custom.cpp: `test_hold_behaviour` sets `detect_chirp = false`.

## Siren and scream (added after the chirp/cry merge)
- `EventType::Siren`: tonal sound (400-2200 Hz, >= 50 % of power at the peak) lasting >= 2 s whose pitch
  sweeps >= 20 % and turns around at least once - wail, yelp and hi-lo all qualify; a steady tone stays an
  Alarm. Once a siren is confirmed, further Alarm events for it are suppressed. Before that (first ~2 s)
  an Alarm and/or one LoudSound can still fire, because the siren is only recognisable after `siren_min_s`.
- `EventType::Scream`: loud (>= -35 dBFS, >= 15 dB over the background at the onset), harmonic voice with
  f0 700-1800 Hz, moving pitch (>= 4 %), spectrum not too pure (harmonic share 0.35-0.85, so whistles and
  sirens are excluded), >= 0.4 s. Reported once per scream, at ~0.4 s. Screams do not also give LoudSound.
- Harmonic scan generalised (cry range / scream range). A candidate fundamental must now have at least half
  of its harmonic slots occupied - fixes a high-pitched voice (f0 ~1.5 kHz) being read as a cry at f0/3.
- Scream detection is the least reliable of the new classes (synthetic data: ~85-90 % on deliberately harsh
  pitch jitter; missed screams still surface as LoudSound). Tune `scream_*` on real recordings.

## Not done
- No HarmonyOS/N-API wrapper is included (wrap every entry point in try/catch there).

## Changes made in the Safe'n'Sound merge (not in the upstream repository)
- trainer: `TrainOptions::allow_single_fallback` (default true, upstream behaviour) and `max_dropped` (default unlimited);
  `TrainResult::dropped` lists the recordings that were left out. The app sets `allow_single_fallback = false`,
  `max_dropped = 1`, `min_consistency = 0.55`: two recordings that disagree are rejected (upstream keeps one of them and
  accepted 47 of 50 pairs of different sounds in our measurement), and with three or more takes one odd one out is
  dropped. Fixed a typo in an error message ("in your used" -> "in your recordings").
- tests/test_core.cpp: the "contradicting recordings are rejected" check uses the strict options.
