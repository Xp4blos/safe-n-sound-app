import { hilog } from '@kit.PerformanceAnalysisKit';
import { AppSettings } from '../model/AppSettings';
import { KeywordRule, SpeechCaption } from '../model/SpeechModels';
import { CaptionBoard, KeywordAlertPort } from './CaptionBoard';
import { NativeSpeech } from './NativeSpeech';
import { SettingsStore } from './SettingsStore';
import { RecognizerSink, SystemRecognizer } from './SystemRecognizer';

const LOG_DOMAIN: number = 0x3201;
const TAG: string = 'SpeechService';

// Captions and keyword alerts: the text of the system recogniser (or of the typed-text simulator) goes through the
// native keyword rules; the Captions tab shows the result.
export class SpeechService implements RecognizerSink {
  onChanged: () => void = (): void => {};
  onKeyword: (label: string, text: string, timeMs: number) => void =
    (label: string, text: string, timeMs: number): void => {};

  error: string = '';
  active: boolean = false;
  rules: KeywordRule[] = [];

  private store: SettingsStore;
  private settings: AppSettings;
  private alerts: KeywordAlertPort;
  private native: NativeSpeech | undefined = undefined;
  private board: CaptionBoard | undefined = undefined;
  private recognizer: SystemRecognizer | undefined = undefined;
  private t0: number = Date.now();

  constructor(store: SettingsStore, alerts: KeywordAlertPort) {
    this.store = store;
    this.settings = store.settings;
    this.alerts = alerts;
  }

  captions(): SpeechCaption[] {
    return this.board === undefined ? [] : this.board.captions();
  }

  live(): SpeechCaption | undefined {
    return this.board?.live();
  }

  lastAlert(): string {
    return this.board === undefined ? '' : this.board.lastAlert();
  }

  // Creates the keyword pipeline (with the user's saved rules, or the built-in ones). Safe to call again.
  ensureReady(): boolean {
    if (this.board !== undefined) {
      return true;
    }
    try {
      const saved = this.store.savedRules();
      const native = NativeSpeech.create(saved === undefined, this.settings.speechOnPartial);
      if (saved !== undefined) {
        for (const rule of saved) {
          try {
            native.addRule(rule.phrase, rule.pattern, rule.priority);
          } catch (e) {
            hilog.warn(LOG_DOMAIN, TAG, 'Skipped a saved keyword rule');
          }
        }
      }
      this.native = native;
      const board = new CaptionBoard(native, this.alerts, this.settings);
      board.onKeyword = (label: string, text: string, timeMs: number): void => {
        this.onKeyword(label, text, timeMs);
        this.onChanged();
      };
      this.board = board;
      this.rules = native.rules();
      return true;
    } catch (e) {
      this.error = 'Keyword engine unavailable';
      hilog.error(LOG_DOMAIN, TAG, 'Could not create the speech pipeline: %{public}s', JSON.stringify(e));
      return false;
    }
  }

  // Creates the keyword pipeline again (a setting that is read at creation changed). Captions are kept.
  rebuild(): void {
    this.native = undefined;
    this.board = undefined;
    this.ensureReady();
    this.onChanged();
  }

  // Starts the recogniser (while the microphone is listening). Resolves to whether captions are live.
  async start(): Promise<boolean> {
    if (this.active) {
      return true;
    }
    if (!this.ensureReady()) {
      return false;
    }
    this.error = '';
    this.t0 = Date.now();
    if (this.recognizer === undefined) {
      this.recognizer = new SystemRecognizer(this);
    }
    this.active = await this.recognizer.start();
    this.onChanged();
    return this.active;
  }

  async stop(): Promise<void> {
    this.active = false;
    if (this.recognizer !== undefined) {
      await this.recognizer.stop();
    }
    this.board?.endUtterance();
    this.onChanged();
  }

  // Microphone PCM from the shared capture.
  feed(pcm: ArrayBuffer): void {
    if (this.active && this.recognizer !== undefined) {
      this.recognizer.feed(pcm);
    }
  }

  // Test input: text as if it had just been recognised. Clearly marked as a simulation in the UI.
  simulate(text: string): void {
    if (!this.ensureReady()) {
      return;
    }
    const now = Date.now();
    this.board?.onResult(text, true, now, (now - this.t0) / 1000);
    this.onChanged();
  }

  clear(): void {
    this.board?.clear();
    this.onChanged();
  }

  // ---- keyword rules ------------------------------------------------------------------------------

  async addRule(phrase: string, pattern: string): Promise<void> {
    if (!this.ensureReady() || this.native === undefined) {
      return;
    }
    this.native.addRule(phrase.trim(), pattern, 1);
    this.rules = this.native.rules();
    await this.store.saveRules(this.rules);
    this.onChanged();
  }

  async removeRule(phrase: string): Promise<void> {
    if (!this.ensureReady() || this.native === undefined) {
      return;
    }
    this.native.removeRule(phrase);
    this.rules = this.native.rules();
    await this.store.saveRules(this.rules);
    this.onChanged();
  }

  // RecognizerSink
  onText(text: string, isFinal: boolean): void {
    this.error = ''; // a recogniser hiccup between two sessions is over once text arrives again
    const now = Date.now();
    this.board?.onResult(text, isFinal, now, (now - this.t0) / 1000);
    this.onChanged();
  }

  onSessionEnded(): void {
    this.board?.endUtterance();
    this.onChanged();
  }

  onProblem(message: string): void {
    this.error = message;
    this.onChanged();
  }
}
