// Plain data shapes shared by the services and UI. They mirror the native module's declarations
// (entry/src/main/cpp/types/libsafensound/index.d.ts) so the pure logic here never imports the native library.

export interface SoundProfile {
  dominantHz: number;
  durationSec: number;
  beepCount: number;
  beepsPerSec: number;
  repetition: number; // 0 single, 1 repeated, 2 continuous
  modulation: number; // 0 steady, 1 pulsed, 2 sweeping
  envelope: number[]; // 8 points, max 1
}

export interface EngineEvent {
  type: string; // 'alarm' (unrecognised tonal sound) or 'custom' (a stored sound recognised again)
  timeSec: number; // engine stream time of the detection
  startSec: number; // start of the matched sound (custom)
  confidence: number;
  freqHz: number;
  levelDb: number;
  label: string; // custom: the label the sound was stored under, else ''
}

export interface EngineResult {
  levelDb: number;
  bands: number[]; // 16 live spectrum bars, 0..1
  events: EngineEvent[];
}

export interface LearnOutcome {
  ok: boolean;
  message: string; // why it failed, when !ok
  template: string; // base64 of the serialised sound (feature numbers, not audio); '' when !ok
  profile: SoundProfile;
  consistency: number;
}

// One remembered sound. Only feature numbers are stored, never audio.
export interface SoundRecord {
  id: number;
  label: string; // key the engine knows the sound by, e.g. 'snd-3'
  name: string; // '' means the user has not named it yet ("Unknown sound")
  alertsEnabled: boolean;
  timesHeard: number;
  firstSeenMs: number;
  lastSeenMs: number;
  lastAlertMs: number; // 0 = never alerted
  lastPromptMs: number; // 0 = never asked to name it
  promptNever: boolean; // "Don't ask again for this sound"
  recentMs: number[]; // timestamps of the most recent occurrences, oldest first
  profile: SoundProfile;
  template: string; // base64 engine template; '' while the sound is still being learned
}
