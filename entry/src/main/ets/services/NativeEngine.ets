import {
  addSound, checkTake, createEngine, destroyEngine, learnSound, LearnResult as NativeLearnResult,
  process as nativeProcess, removeSound, trainSound
} from 'libsafensound.so';
import { util } from '@kit.ArkTS';
import { hilog } from '@kit.PerformanceAnalysisKit';
import { EngineEvent, EngineResult, LearnOutcome, SoundProfile } from '../model/SoundModels';
import { EnginePort } from './SoundPipeline';

const LOG_DOMAIN: number = 0x3201;
const TAG: string = 'NativeEngine';

// Typed wrapper around the native engine handle. Converts native results into the app's own model types and
// keeps stored templates as base64 text.
export class NativeEngine implements EnginePort {
  private handle: number;
  private destroyed: boolean = false;
  private base64: util.Base64Helper = new util.Base64Helper();

  private constructor(handle: number) {
    this.handle = handle;
  }

  static create(sampleRate: number): NativeEngine {
    return new NativeEngine(createEngine(sampleRate));
  }

  process(pcm: ArrayBuffer): EngineResult {
    const native = nativeProcess(this.handle, pcm);
    const events: EngineEvent[] = [];
    for (const e of native.events) {
      const ev: EngineEvent = {
        type: e.type, timeSec: e.timeSec, startSec: e.startSec, confidence: e.confidence, freqHz: e.freqHz,
        levelDb: e.levelDb, label: e.label
      };
      events.push(ev);
    }
    for (const ev of events) {
      hilog.info(LOG_DOMAIN, TAG, 'EVENT %{public}s t=%{public}s start=%{public}s freq=%{public}d level=%{public}d label=%{public}s',
        ev.type, ev.timeSec.toFixed(2), ev.startSec.toFixed(2), Math.round(ev.freqHz), Math.round(ev.levelDb), ev.label);
    }
    const result: EngineResult = { levelDb: native.levelDb, bands: native.bands, events: events };
    return result;
  }

  // Learns the sound around an alarm event from the audio the engine still holds in memory.
  learnSound(label: string, eventTimeSec: number): LearnOutcome {
    return this.toOutcome(learnSound(this.handle, label, eventTimeSec));
  }

  trainSound(label: string, takes: ArrayBuffer[]): LearnOutcome {
    return this.toOutcome(trainSound(this.handle, label, takes));
  }

  checkTake(take: ArrayBuffer): LearnOutcome {
    return this.toOutcome(checkTake(this.handle, take));
  }

  // Registers a stored template (base64). False if it is corrupt.
  addSound(templateBase64: string): boolean {
    try {
      const bytes = this.base64.decodeSync(templateBase64);
      return addSound(this.handle, bytes.buffer as ArrayBuffer);
    } catch (e) {
      return false;
    }
  }

  removeSound(label: string): boolean {
    return removeSound(this.handle, label);
  }

  // Safe to call more than once.
  destroy(): void {
    if (!this.destroyed) {
      this.destroyed = true;
      destroyEngine(this.handle);
    }
  }

  private toOutcome(r: NativeLearnResult): LearnOutcome {
    hilog.info(LOG_DOMAIN, TAG, 'LEARN ok=%{public}d consistency=%{public}s message=%{public}s', r.ok ? 1 : 0,
      r.consistency.toFixed(2), r.message);
    const profile: SoundProfile = {
      dominantHz: r.profile.dominantHz, durationSec: r.profile.durationSec, beepCount: r.profile.beepCount,
      beepsPerSec: r.profile.beepsPerSec, repetition: r.profile.repetition, modulation: r.profile.modulation,
      envelope: r.profile.envelope
    };
    const template = r.ok ? this.base64.encodeToStringSync(new Uint8Array(r.template)) : '';
    const outcome: LearnOutcome = {
      ok: r.ok, message: r.message, template: template, profile: profile, consistency: r.consistency
    };
    return outcome;
  }
}
