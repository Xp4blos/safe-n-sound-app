import { AppSettings } from '../model/AppSettings';
import { SpeechAlertInfo, SpeechCaption, SpeechUpdate } from '../model/SpeechModels';

// The native speech pipeline as the board needs it (a fake in tests).
export interface SpeechEnginePort {
  speechResult(text: string, isFinal: boolean, timeSec: number): SpeechUpdate;
  speechEndUtterance(): void;
  speechReset(): void;
}

// What a keyword alert does on the phone.
export interface KeywordAlertPort {
  playPattern(pattern: number[]): void;
  notifyKeyword(label: string, timeMs: number): void;
}

// Turns what the speech recogniser returns into captions and keyword alerts. Pure logic: the text may come from
// the system recogniser or from the typed-text simulator.
export class CaptionBoard {
  static readonly MAX_CAPTIONS: number = 100;

  // A keyword was heard (for the on-screen card): label, the words as spoken, time.
  onKeyword: (label: string, text: string, timeMs: number) => void =
    (label: string, text: string, timeMs: number): void => {};

  private engine: SpeechEnginePort;
  private alerts: KeywordAlertPort;
  private settings: AppSettings;
  private finished: SpeechCaption[] = [];
  private liveCaption: SpeechCaption | undefined = undefined;
  private lastKeyword: string = '';

  constructor(engine: SpeechEnginePort, alerts: KeywordAlertPort, settings: AppSettings) {
    this.engine = engine;
    this.alerts = alerts;
    this.settings = settings;
  }

  captions(): SpeechCaption[] {
    return this.finished;
  }

  live(): SpeechCaption | undefined {
    return this.liveCaption;
  }

  lastAlert(): string {
    return this.lastKeyword;
  }

  // One recogniser result. `timeSec` is a monotonic clock for the engine, `nowMs` the wall clock for notifications.
  onResult(text: string, isFinal: boolean, nowMs: number, timeSec: number): void {
    if (text.length === 0) {
      return;
    }
    const update = this.engine.speechResult(text, isFinal, timeSec);
    if (isFinal) {
      this.finished.push(update.caption);
      if (this.finished.length > CaptionBoard.MAX_CAPTIONS) {
        this.finished.shift();
      }
      this.liveCaption = undefined;
    } else {
      this.liveCaption = update.caption;
    }
    if (update.alerts.length > 0) {
      this.raise(update.alerts, update.caption.text, nowMs);
    }
  }

  // The recognition session ended without a final result.
  endUtterance(): void {
    this.liveCaption = undefined;
    this.engine.speechEndUtterance();
  }

  clear(): void {
    this.finished = [];
    this.liveCaption = undefined;
    this.lastKeyword = '';
    this.engine.speechReset();
  }

  // Alerts arrive most urgent first: only the first one vibrates.
  private raise(alerts: SpeechAlertInfo[], text: string, nowMs: number): void {
    const top = alerts[0];
    const spoken = text.substring(top.begin, top.end);
    this.lastKeyword = top.label + ': "' + spoken + '"';
    if (this.settings.vibrate) {
      this.alerts.playPattern(top.pattern);
    }
    if (this.settings.notify) {
      this.alerts.notifyKeyword(top.label, nowMs);
    }
    this.onKeyword(top.label, spoken, nowMs);
  }
}
