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
  // 'alarm' (unrecognised tonal sound), 'custom' (a stored sound recognised again) or a built-in class:
  // 'chirp', 'cry', 'siren', 'scream', 'knock', 'loud_sound'
  type: string;
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

export interface LearnResult {
  ok: boolean;
  message: string; // why it failed, when !ok
  template: ArrayBuffer; // serialised sound (feature numbers, not audio)
  profile: SoundProfile;
  consistency: number;
  droppedTake: number; // index of a take left out because it disagreed with the others, or -1
}

export const createEngine: (sampleRate: number) => number;
export const destroyEngine: (handle: number) => void;
export const process: (handle: number, pcm: ArrayBuffer) => EngineResult;
export const learnSound: (handle: number, label: string, eventTimeSec: number) => LearnResult;
export const trainSound: (handle: number, label: string, takes: ArrayBuffer[]) => LearnResult;
export const checkTake: (handle: number, take: ArrayBuffer) => LearnResult;
export const addSound: (handle: number, template: ArrayBuffer) => boolean;
export const removeSound: (handle: number, label: string) => boolean;

// ---- speech: captions and keyword alerts (feed it the text of the speech recogniser) ----------------------------

export interface SpeechHighlight {
  begin: number; // UTF-16 index into text
  end: number;
  ruleId: number;
  label: string;
}

export interface SpeechCaption {
  utteranceId: number;
  isFinal: boolean;
  timeSec: number;
  text: string;
  highlights: SpeechHighlight[];
}

export interface SpeechAlertInfo {
  ruleId: number;
  label: string;
  phrase: string;
  timeSec: number;
  begin: number;
  end: number;
  pattern: number[]; // vibration, ms: on, off, on, ...
}

export interface SpeechUpdate {
  caption: SpeechCaption;
  alerts: SpeechAlertInfo[]; // highest priority first
}

export interface KeywordRuleSpec {
  phrase: string;
  label?: string;
  pattern?: number[] | string; // ms on/off, or 'sos' | 'rapid' | 'long' | 'double'
  priority?: number;
  cooldownS?: number;
}

export interface KeywordRuleInfo {
  phrase: string;
  label: string;
  pattern: number[];
  priority: number;
  cooldownS: number;
}

export interface SpeechOptions {
  useDefaultRules?: boolean;
  triggerOnPartial?: boolean;
}

export const createSpeech: (options?: SpeechOptions) => void;
export const speechResult: (text: string, isFinal: boolean, timeSec: number) => SpeechUpdate;
export const speechEndUtterance: () => void;
export const speechReset: () => void;
export const speechAddRule: (rule: KeywordRuleSpec) => number;
export const speechRemoveRule: (phrase: string) => boolean;
export const speechRules: () => KeywordRuleInfo[];
export const speechHistory: () => SpeechCaption[];
