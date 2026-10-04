// Plain data shapes for captions and keyword alerts. They mirror the native module's declarations
// (cpp/types/libsafensound/index.d.ts) so the pure logic never imports the native library.

export interface SpeechHighlight {
  begin: number; // UTF-16 index into the caption text
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
  pattern: number[]; // vibration: on, off, on, ... in ms
}

export interface SpeechUpdate {
  caption: SpeechCaption;
  alerts: SpeechAlertInfo[]; // most urgent first
}

export interface KeywordRule {
  phrase: string;
  label: string;
  pattern: number[];
  priority: number;
  cooldownS: number;
}
