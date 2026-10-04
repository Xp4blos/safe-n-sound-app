import { AppSettings } from '../model/AppSettings';
import { SoundCatalog } from '../model/SoundCatalog';
import { SoundClass, SoundClasses } from '../model/SoundClasses';
import { EngineEvent, LearnOutcome, SoundRecord } from '../model/SoundModels';

// Narrow ports so the pipeline can be tested without the native library, vibrator, storage or UI.
export interface EnginePort {
  learnSound(label: string, eventTimeSec: number): LearnOutcome;
}

export interface AlertPort {
  vibrateUnknown(): void; // one short pulse
  vibrateNamed(): void; // distinct long pattern
  notifyNamed(name: string, timeMs: number): void;
  notifyPrompt(): void; // "I've heard this sound before. Do you want to name it?"
  vibrateClass(cls: SoundClass): void; // the class's own pattern
  notifyClass(cls: SoundClass, timeMs: number): void;
  isSelfNoise(nowMs: number): boolean; // the phone itself is making noise: it vibrates, or the screen was just touched
}

export interface SavePort {
  save(): Promise<void>;
}

export interface PipelineListener {
  onSoundHeard(record: SoundRecord, isNew: boolean): void;
  onAlert(record: SoundRecord, timeMs: number): void; // a named sound: show the alert card
  onPrompt(record: SoundRecord): void; // an unnamed sound again: show the "I know this sound" dialog
  onClassAlert(record: SoundRecord, timeMs: number): void; // a built-in class (siren, ...): show the alert card
  onDetection(timeMs: number): void; // highlight the moment in the live view
  onChanged(): void; // the list of sounds changed
}

interface SoundWindow {
  fromSec: number;
  toSec: number;
}

interface HeldLoud {
  event: EngineEvent;
  dueMs: number;
}

interface HeldAlarm {
  event: EngineEvent;
  arrivedMs: number;
  dueMs: number;
  attempts: number;
  label: string; // the unknown sound created for this alarm ('' until then)
}

// What happens with the engine's events: recognised sounds are counted and alerted; an alarm that no stored
// sound explains becomes a new unknown sound whose template is learned from the audio around it.
export class SoundPipeline {
  // An alarm event is held this long: a stored sound that explains it is only reported after its template window
  // ends, and learning needs 3.5 s of audio after the alarm.
  static readonly HOLD_MS: number = 3700;
  static readonly RETRY_MS: number = 500;
  static readonly MAX_ATTEMPTS: number = 4;
  static readonly BURST_MS: number = 6000; // alarms this close to a new sound belong to the same burst
  static readonly EXPLAIN_MARGIN_SEC: number = 0.6;
  // The engine reports the first ~2 s of a siren as a loud sound; a loud sound is held until it is clear it is not one.
  static readonly LOUD_HOLD_MS: number = 3000;
  static readonly SIREN_AHEAD_SEC: number = 3; // the sound goes on after the engine confirmed it

  private catalog: SoundCatalog;
  private engine: EnginePort;
  private alerts: AlertPort;
  private saver: SavePort;
  private listener: PipelineListener;
  private settings: AppSettings;
  private held: HeldAlarm[] = [];
  private heldLoud: HeldLoud[] = [];
  private explained: SoundWindow[] = []; // windows of recognised sounds, for beeps that arrive after the match
  private lastNewSoundMs: number = -1e15;

  constructor(catalog: SoundCatalog, engine: EnginePort, alerts: AlertPort, saver: SavePort, listener: PipelineListener,
    settings: AppSettings = new AppSettings()) {
    this.catalog = catalog;
    this.engine = engine;
    this.alerts = alerts;
    this.saver = saver;
    this.listener = listener;
    this.settings = settings;
  }

  // Feed the events of one engine chunk, then release anything that has waited long enough.
  handle(events: EngineEvent[], nowMs: number): void {
    for (const ev of events) {
      if (ev.type === 'custom') {
        this.onCustom(ev, nowMs);
      } else if (SoundClasses.forEvent(ev.type) !== undefined) {
        this.onBuiltIn(ev, nowMs);
      } else if (ev.type === 'alarm') {
        this.listener.onDetection(nowMs);
        if (this.isExplained(ev.timeSec)) {
          continue; // a beep of a sound that was just recognised, it only arrived a moment after the match
        }
        const h: HeldAlarm = { event: ev, arrivedMs: nowMs, dueMs: nowMs + SoundPipeline.HOLD_MS, attempts: 0, label: '' };
        this.held.push(h);
      }
    }
    this.tick(nowMs);
  }

  // Call regularly (every audio chunk): resolves held alarms whose time has come.
  tick(nowMs: number): void {
    this.releaseLoud(nowMs);
    if (this.held.length === 0) {
      return;
    }
    const due = this.held.filter((h: HeldAlarm): boolean => h.dueMs <= nowMs);
    if (due.length === 0) {
      return;
    }
    this.held = this.held.filter((h: HeldAlarm): boolean => h.dueMs > nowMs);
    for (const h of due) {
      this.resolveAlarm(h, nowMs);
    }
  }

  private onCustom(ev: EngineEvent, nowMs: number): void {
    this.listener.onDetection(nowMs);
    this.explain(ev);

    const occurrence = this.catalog.registerOccurrence(ev.label, nowMs);
    if (occurrence === undefined) {
      return;
    }
    const record = occurrence.record;
    if (occurrence.shouldAlert) {
      if (this.settings.vibrate) {
        this.alerts.vibrateNamed();
      }
      if (this.settings.notify) {
        this.alerts.notifyNamed(record.name, nowMs);
      }
      this.listener.onAlert(record, nowMs);
    } else if (record.name === '' && this.settings.vibrate) {
      this.alerts.vibrateUnknown();
    }
    if (occurrence.shouldPrompt) {
      if (this.settings.notify) {
        this.alerts.notifyPrompt();
      }
      this.listener.onPrompt(record);
    }
    this.listener.onSoundHeard(record, false);
    this.saver.save();
  }

  // A recognised sound explains the alarm events inside its window (also those that arrive just after the match).
  private explain(ev: EngineEvent, backSec: number = 0, aheadSec: number = 0): void {
    const start = backSec > 0 ? ev.timeSec - backSec : ev.startSec;
    const from = start - SoundPipeline.EXPLAIN_MARGIN_SEC;
    const to = ev.timeSec + SoundPipeline.EXPLAIN_MARGIN_SEC + aheadSec;
    this.held = this.held.filter((h: HeldAlarm): boolean => h.event.timeSec < from || h.event.timeSec > to);
    // a beep or loud sound inside the window of a recognised sound belongs to it
    this.heldLoud = this.heldLoud.filter((h: HeldLoud): boolean => h.event.timeSec < from || h.event.timeSec > to);
    const matched: SoundWindow = { fromSec: from, toSec: to };
    this.explained.push(matched);
    if (this.explained.length > 8) {
      this.explained.shift();
    }
  }

  // A built-in class (siren, scream, baby crying, chirp, knock, loud sound).
  private onBuiltIn(ev: EngineEvent, nowMs: number): void {
    const cls = SoundClasses.forEvent(ev.type);
    if (cls === undefined || !this.settings.isClassOn(cls.key)) {
      return;
    }
    if (cls.selfVibrationSensitive && this.alerts.isSelfNoise(nowMs)) {
      return; // the phone's own vibration or a tap on its screen is not a knock or a loud sound
    }
    if (cls.key === 'loud_sound' || cls.key === 'chirp') {
      if (this.isExplained(ev.timeSec)) {
        return; // a beep or loud sound that is part of a sound that was already recognised
      }
      const held: HeldLoud = { event: ev, dueMs: nowMs + SoundPipeline.LOUD_HOLD_MS };
      this.heldLoud.push(held);
      return;
    }
    this.listener.onDetection(nowMs);
    if (cls.explainBackSec > 0) {
      // a siren, scream or cry is also heard as alarm tones, beeps and a loud sound at first, and goes on for a while
      this.explain(ev, cls.explainBackSec, SoundPipeline.SIREN_AHEAD_SEC);
    }
    this.report(cls, nowMs);
  }

  private releaseLoud(nowMs: number): void {
    if (this.heldLoud.length === 0) {
      return;
    }
    const due = this.heldLoud.filter((h: HeldLoud): boolean => h.dueMs <= nowMs);
    if (due.length === 0) {
      return;
    }
    this.heldLoud = this.heldLoud.filter((h: HeldLoud): boolean => h.dueMs > nowMs);
    const reported: string[] = [];
    for (const h of due) {
      const cls = SoundClasses.forEvent(h.event.type);
      if (cls === undefined || reported.includes(cls.key) || !this.settings.isClassOn(cls.key)) {
        continue; // several beeps or loud sounds released together count as one
      }
      reported.push(cls.key);
      this.listener.onDetection(nowMs);
      this.report(cls, nowMs);
    }
  }

  // History record, vibration, notification and card for a built-in class.
  private report(cls: SoundClass, nowMs: number): void {
    this.catalog.ensureBuiltIn(cls.label, cls.title, nowMs);
    const occurrence = this.catalog.registerOccurrence(cls.label, nowMs);
    if (occurrence === undefined) {
      return;
    }
    if (occurrence.shouldAlert) {
      if (this.settings.vibrate) {
        this.alerts.vibrateClass(cls);
      }
      if (this.settings.notify) {
        this.alerts.notifyClass(cls, nowMs);
      }
      this.listener.onClassAlert(occurrence.record, nowMs);
    }
    this.listener.onSoundHeard(occurrence.record, false);
    this.saver.save();
  }

  private isExplained(timeSec: number): boolean {
    return this.explained.some((w: SoundWindow): boolean => timeSec >= w.fromSec && timeSec <= w.toSec);
  }

  private resolveAlarm(h: HeldAlarm, nowMs: number): void {
    if (h.label === '') {
      if (nowMs - this.lastNewSoundMs < SoundPipeline.BURST_MS) {
        return; // another alarm of the burst that already produced a sound
      }
      const record = this.catalog.createPending(h.arrivedMs);
      h.label = record.label;
      this.lastNewSoundMs = nowMs;
      if (this.settings.vibrate) {
        this.alerts.vibrateUnknown();
      }
      this.listener.onSoundHeard(record, true);
      this.listener.onChanged();
    }
    this.learn(h, nowMs);
  }

  private learn(h: HeldAlarm, nowMs: number): void {
    h.attempts++;
    const outcome = this.engine.learnSound(h.label, h.event.timeSec);
    if (outcome.ok) {
      this.catalog.completeLearning(h.label, outcome.template, outcome.profile);
    } else if (outcome.message.startsWith('Not enough audio') && h.attempts < SoundPipeline.MAX_ATTEMPTS) {
      h.dueMs = nowMs + SoundPipeline.RETRY_MS;
      this.held.push(h);
      return;
    } else {
      this.catalog.discard(h.label);
    }
    this.listener.onChanged();
    this.saver.save();
  }
}
