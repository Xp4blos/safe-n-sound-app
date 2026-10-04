import { speechRecognizer } from '@kit.CoreSpeechKit';
import { hilog } from '@kit.PerformanceAnalysisKit';

const LOG_DOMAIN: number = 0x3201;
const TAG: string = 'SystemRecognizer';
const CHUNK_BYTES: number = 1280; // Core Speech Kit accepts 640 or 1280 byte writes at 16 kHz
const MAX_FAIL_STREAK: number = 5;

export interface RecognizerSink {
  onText(text: string, isFinal: boolean): void;
  onSessionEnded(): void; // the recogniser's session ended (silence, time limit): the utterance is over
  onProblem(message: string): void;
}

function describe(e: Object): string {
  const err = e as Record<string, Object>;
  const code = err['code'] !== undefined ? String(err['code']) : '';
  const message = err['message'] !== undefined ? String(err['message']) : JSON.stringify(e);
  return code === '' ? message : code + ': ' + message;
}

// The system speech recogniser (Huawei Core Speech Kit). On this platform it recognises Mandarin only (zh-CN, on
// the device); English keywords still reach the keyword rules because the recogniser returns text for whatever it
// hears. Sessions end after silence or a time limit, so a new one is started automatically.
export class SystemRecognizer {
  private sink: RecognizerSink;
  private engine: speechRecognizer.SpeechRecognitionEngine | undefined = undefined;
  private sessionId: string = '';
  private listening: boolean = false; // audio may only be written after onStart
  private wanted: boolean = false;
  private failStreak: number = 0;
  private carry: Uint8Array = new Uint8Array(0);

  constructor(sink: RecognizerSink) {
    this.sink = sink;
  }

  isRunning(): boolean {
    return this.wanted;
  }

  // Resolves to whether the recogniser could be started.
  async start(): Promise<boolean> {
    if (this.wanted) {
      return true;
    }
    try {
      this.engine = await this.createEngine();
      this.wanted = true;
      this.failStreak = 0;
      this.carry = new Uint8Array(0);
      this.beginSession();
      return true;
    } catch (e) {
      this.sink.onProblem('Speech recognition unavailable: ' + describe(e as Object));
      await this.stop();
      return false;
    }
  }

  async stop(): Promise<void> {
    this.wanted = false;
    const engine = this.engine;
    const wasListening = this.listening;
    this.engine = undefined;
    this.listening = false;
    if (engine !== undefined) {
      try {
        if (wasListening) {
          engine.finish(this.sessionId);
        }
      } catch (e) {
        hilog.warn(LOG_DOMAIN, TAG, 'finish: %{public}s', describe(e as Object));
      }
      try {
        engine.shutdown();
      } catch (e) {
        hilog.warn(LOG_DOMAIN, TAG, 'shutdown: %{public}s', describe(e as Object));
      }
    }
  }

  // PCM (16 kHz mono 16-bit) from the shared microphone, forwarded in the fixed-size blocks the recogniser needs.
  feed(pcm: ArrayBuffer): void {
    const engine = this.engine;
    if (engine === undefined || !this.listening) {
      return;
    }
    const incoming = new Uint8Array(pcm);
    const all = new Uint8Array(this.carry.length + incoming.length);
    all.set(this.carry, 0);
    all.set(incoming, this.carry.length);
    let offset = 0;
    try {
      while (all.length - offset >= CHUNK_BYTES) {
        engine.writeAudio(this.sessionId, all.slice(offset, offset + CHUNK_BYTES));
        offset += CHUNK_BYTES;
      }
    } catch (e) {
      this.carry = new Uint8Array(0);
      this.sessionEnded('Cannot write audio: ' + describe(e as Object));
      return;
    }
    this.carry = all.slice(offset);
  }

  private async createEngine(): Promise<speechRecognizer.SpeechRecognitionEngine> {
    const params: speechRecognizer.CreateEngineParams = {
      language: 'zh-CN', // the only language the recogniser supports on this platform
      online: 1, // 1 = on the device
      extraParams: { 'locate': 'CN', 'recognizerMode': 'short' }
    };
    const engine = await speechRecognizer.createEngine(params);
    const self: SystemRecognizer = this;
    const listener: speechRecognizer.RecognitionListener = {
      onStart(sessionId: string, eventMessage: string) {
        if (sessionId !== self.sessionId) {
          return;
        }
        self.failStreak = 0;
        self.carry = new Uint8Array(0);
        self.listening = true;
      },
      onEvent(sessionId: string, eventCode: number, eventMessage: string) {
        hilog.info(LOG_DOMAIN, TAG, 'event %{public}d: %{public}s', eventCode, eventMessage);
      },
      onResult(sessionId: string, result: speechRecognizer.SpeechRecognitionResult) {
        if (sessionId !== self.sessionId) {
          return;
        }
        self.failStreak = 0;
        hilog.info(LOG_DOMAIN, TAG, 'RESULT final=%{public}d len=%{public}d', result.isFinal ? 1 : 0,
          result.result.length);
        self.sink.onText(result.result, result.isFinal);
      },
      onComplete(sessionId: string, eventMessage: string) {
        if (sessionId !== self.sessionId) {
          return;
        }
        self.sessionEnded('');
      },
      onError(sessionId: string, errorCode: number, errorMessage: string) {
        if (sessionId !== self.sessionId) {
          return;
        }
        self.sessionEnded('Recogniser error ' + errorCode + ': ' + errorMessage);
      }
    };
    engine.setListener(listener);
    return engine;
  }

  private beginSession(): void {
    const engine = this.engine;
    if (engine === undefined || !this.wanted) {
      return;
    }
    this.sessionId = 'sns-' + Date.now();
    const audioInfo: speechRecognizer.AudioInfo = { audioType: 'pcm', sampleRate: 16000, soundChannel: 1, sampleBit: 16 };
    const start: speechRecognizer.StartParams = {
      sessionId: this.sessionId,
      audioInfo: audioInfo,
      extraParams: { 'recognitionMode': 0, 'vadBegin': 2000, 'vadEnd': 3000, 'maxAudioDuration': 20000 }
    };
    try {
      this.listening = false;
      engine.startListening(start);
    } catch (e) {
      this.listening = false;
      this.sessionEnded('Cannot start recognition: ' + describe(e as Object));
    }
  }

  // The session ended (silence, time limit or an error): start the next one, slower after repeated failures.
  private sessionEnded(problem: string): void {
    this.listening = false;
    this.carry = new Uint8Array(0);
    this.sink.onSessionEnded();
    if (problem.length > 0) {
      this.failStreak++;
      this.sink.onProblem(problem);
    }
    if (!this.wanted) {
      return;
    }
    if (this.failStreak >= MAX_FAIL_STREAK) {
      this.failStreak = 0;
      const old = this.engine;
      this.engine = undefined;
      try {
        if (old !== undefined) {
          old.shutdown();
        }
      } catch (e) {
        hilog.warn(LOG_DOMAIN, TAG, 'shutdown: %{public}s', describe(e as Object));
      }
      this.createEngine().then((engine: speechRecognizer.SpeechRecognitionEngine): void => {
        if (this.wanted) {
          this.engine = engine;
          this.beginSession();
        } else {
          engine.shutdown();
        }
      }).catch((e: Object): void => {
        this.sink.onProblem('Speech recogniser unavailable: ' + describe(e));
        setTimeout((): void => {
          if (this.wanted) {
            this.sessionEnded('retry');
          }
        }, 10000);
      });
      return;
    }
    setTimeout((): void => {
      this.beginSession();
    }, Math.min(10000, 300 * (1 + this.failStreak * this.failStreak)));
  }
}
