import { SoundClasses } from './SoundClasses';
import { SoundProfile, SoundRecord } from './SoundModels';

export interface Occurrence {
  record: SoundRecord;
  shouldAlert: boolean; // named sound, alerts on, outside the cooldown
  shouldPrompt: boolean; // unnamed sound heard again: offer to name it
}

export interface LearnedTemplate {
  label: string;
  template: string;
}

function emptyProfile(): SoundProfile {
  const p: SoundProfile = {
    dominantHz: 0, durationSec: 0, beepCount: 0, beepsPerSec: 0, repetition: 0, modulation: 0,
    envelope: [0, 0, 0, 0, 0, 0, 0, 0]
  };
  return p;
}

// The remembered sounds and the rules for updating them. Pure logic: no storage, no native calls.
export class SoundCatalog {
  static readonly ALERT_COOLDOWN_MS: number = 15000;
  static readonly PROMPT_INTERVAL_MS: number = 600000; // no repeat "name it?" prompt within 10 minutes
  static readonly MAX_NAME_LENGTH: number = 40;
  static readonly MAX_RECENT: number = 20;

  private items: SoundRecord[];
  private reservedId: number = 0;

  constructor(records: SoundRecord[]) {
    this.items = records;
  }

  // ---- persistence ------------------------------------------------------------------------------

  // Tolerant loader: anything unreadable or malformed (including the first app version's format) is dropped.
  static parse(json: string): SoundCatalog {
    let parsed: Object;
    try {
      parsed = JSON.parse(json) as Object;
    } catch (e) {
      return new SoundCatalog([]);
    }
    if (!(parsed instanceof Array)) {
      return new SoundCatalog([]);
    }
    const records: SoundRecord[] = [];
    for (const item of parsed as Object[]) {
      const record = SoundCatalog.toRecord(item);
      if (record !== undefined) {
        records.push(record);
      }
    }
    return new SoundCatalog(records);
  }

  private static numbers(value: Object | undefined, length: number): number[] | undefined {
    if (!(value instanceof Array)) {
      return undefined;
    }
    const out: number[] = [];
    for (const v of value as Object[]) {
      if (typeof v !== 'number') {
        return undefined;
      }
      out.push(v as number);
    }
    if (length >= 0 && out.length !== length) {
      return undefined;
    }
    return out;
  }

  private static toProfile(value: Object | undefined): SoundProfile | undefined {
    if (value === undefined || value === null || typeof value !== 'object') {
      return undefined;
    }
    const o = value as Record<string, Object>;
    const envelope = SoundCatalog.numbers(o['envelope'], 8);
    if (envelope === undefined || typeof o['dominantHz'] !== 'number' || typeof o['durationSec'] !== 'number' ||
      typeof o['beepCount'] !== 'number' || typeof o['beepsPerSec'] !== 'number' ||
      typeof o['repetition'] !== 'number' || typeof o['modulation'] !== 'number') {
      return undefined;
    }
    const p: SoundProfile = {
      dominantHz: o['dominantHz'] as number, durationSec: o['durationSec'] as number,
      beepCount: o['beepCount'] as number, beepsPerSec: o['beepsPerSec'] as number,
      repetition: o['repetition'] as number, modulation: o['modulation'] as number, envelope: envelope
    };
    return p;
  }

  private static toRecord(item: Object): SoundRecord | undefined {
    if (item === null || typeof item !== 'object') {
      return undefined;
    }
    const o = item as Record<string, Object>;
    const recent = SoundCatalog.numbers(o['recentMs'], -1);
    const profile = SoundCatalog.toProfile(o['profile']);
    if (recent === undefined || profile === undefined) {
      return undefined;
    }
    if (typeof o['id'] !== 'number' || typeof o['label'] !== 'string' || o['label'] === '' ||
      typeof o['name'] !== 'string' || typeof o['alertsEnabled'] !== 'boolean' ||
      typeof o['timesHeard'] !== 'number' || typeof o['firstSeenMs'] !== 'number' ||
      typeof o['lastSeenMs'] !== 'number' || typeof o['lastAlertMs'] !== 'number' ||
      typeof o['lastPromptMs'] !== 'number' || typeof o['promptNever'] !== 'boolean' ||
      typeof o['template'] !== 'string') {
      return undefined;
    }
    const record: SoundRecord = {
      id: o['id'] as number, label: o['label'] as string, name: o['name'] as string,
      alertsEnabled: o['alertsEnabled'] as boolean, timesHeard: o['timesHeard'] as number,
      firstSeenMs: o['firstSeenMs'] as number, lastSeenMs: o['lastSeenMs'] as number,
      lastAlertMs: o['lastAlertMs'] as number, lastPromptMs: o['lastPromptMs'] as number,
      promptNever: o['promptNever'] as boolean, recentMs: recent.slice(-SoundCatalog.MAX_RECENT),
      profile: profile, template: o['template'] as string
    };
    return record;
  }

  serialize(): string {
    return JSON.stringify(this.items);
  }

  // ---- queries ----------------------------------------------------------------------------------

  records(): SoundRecord[] {
    return this.items;
  }

  // The sounds that belong in My sounds: everything except the built-in classes (siren, scream, ...).
  userSounds(): SoundRecord[] {
    return this.items.filter((r: SoundRecord): boolean => !SoundClasses.isBuiltInLabel(r.label));
  }

  find(id: number): SoundRecord | undefined {
    return this.items.find((r: SoundRecord): boolean => r.id === id);
  }

  findByLabel(label: string): SoundRecord | undefined {
    return this.items.find((r: SoundRecord): boolean => r.label === label);
  }

  // Templates to register with the engine at start-up (sounds still being learned have none yet).
  learnedTemplates(): LearnedTemplate[] {
    const out: LearnedTemplate[] = [];
    for (const r of this.items) {
      if (r.template !== '') {
        const t: LearnedTemplate = { label: r.label, template: r.template };
        out.push(t);
      }
    }
    return out;
  }

  // ---- creating and removing --------------------------------------------------------------------

  private nextId(): number {
    let maxId = 0;
    for (const r of this.items) {
      maxId = Math.max(maxId, r.id);
    }
    return Math.max(maxId, this.reservedId) + 1;
  }

  // Reserves the label of a sound that is about to be taught, so the engine can be trained under it before the
  // record exists and no automatically learned sound can take the same label meanwhile.
  reserveLabel(): string {
    const id = this.nextId();
    this.reservedId = id;
    return 'snd-' + id;
  }

  // The history record of a built-in class, created when the class is first heard. It is named from the start and has
  // no template: the engine detects these classes by itself.
  ensureBuiltIn(label: string, title: string, nowMs: number): SoundRecord {
    const existing = this.findByLabel(label);
    if (existing !== undefined) {
      return existing;
    }
    const record: SoundRecord = {
      id: this.nextId(), label: label, name: title, alertsEnabled: true, timesHeard: 0, firstSeenMs: nowMs,
      lastSeenMs: nowMs, lastAlertMs: 0, lastPromptMs: 0, promptNever: false, recentMs: [], profile: emptyProfile(),
      template: ''
    };
    this.items.push(record);
    return record;
  }

  // A sound the engine just detected for the first time. Its template arrives a few seconds later.
  createPending(nowMs: number): SoundRecord {
    const id = this.nextId();
    const record: SoundRecord = {
      id: id, label: 'snd-' + id, name: '', alertsEnabled: true, timesHeard: 1, firstSeenMs: nowMs,
      lastSeenMs: nowMs, lastAlertMs: 0, lastPromptMs: 0, promptNever: false, recentMs: [nowMs],
      profile: emptyProfile(), template: ''
    };
    this.items.push(record);
    return record;
  }

  completeLearning(label: string, template: string, profile: SoundProfile): boolean {
    const record = this.findByLabel(label);
    if (record === undefined) {
      return false;
    }
    record.template = template;
    record.profile = profile;
    return true;
  }

  // Removes a record by label (learning failed). Returns whether it existed.
  discard(label: string): boolean {
    const index = this.items.findIndex((r: SoundRecord): boolean => r.label === label);
    if (index < 0) {
      return false;
    }
    this.items.splice(index, 1);
    return true;
  }

  // A sound the user taught on purpose: named from the start.
  addTaught(label: string, name: string, template: string, profile: SoundProfile, nowMs: number): SoundRecord {
    const parsedId = parseInt(label.replace('snd-', ''));
    const id = isNaN(parsedId) ? this.nextId() : parsedId;
    const record: SoundRecord = {
      id: id, label: label, name: SoundCatalog.cleanName(name), alertsEnabled: true, timesHeard: 0,
      firstSeenMs: nowMs, lastSeenMs: nowMs, lastAlertMs: 0, lastPromptMs: 0, promptNever: false,
      recentMs: [], profile: profile, template: template
    };
    this.items.push(record);
    return record;
  }

  remove(id: number): SoundRecord | undefined {
    const index = this.items.findIndex((r: SoundRecord): boolean => r.id === id);
    if (index < 0) {
      return undefined;
    }
    return this.items.splice(index, 1)[0];
  }

  // ---- occurrences and alerts -------------------------------------------------------------------

  // The engine recognised `label` again. Unknown labels are ignored.
  registerOccurrence(label: string, nowMs: number): Occurrence | undefined {
    const record = this.findByLabel(label);
    if (record === undefined) {
      return undefined;
    }
    record.timesHeard += 1;
    record.lastSeenMs = nowMs;
    record.recentMs.push(nowMs);
    if (record.recentMs.length > SoundCatalog.MAX_RECENT) {
      record.recentMs = record.recentMs.slice(-SoundCatalog.MAX_RECENT);
    }

    const alertCooledDown = record.lastAlertMs === 0 ||
      nowMs - record.lastAlertMs >= SoundCatalog.ALERT_COOLDOWN_MS;
    const shouldAlert = record.name !== '' && record.alertsEnabled && alertCooledDown;
    if (shouldAlert) {
      record.lastAlertMs = nowMs;
    }

    const promptCooledDown = record.lastPromptMs === 0 ||
      nowMs - record.lastPromptMs >= SoundCatalog.PROMPT_INTERVAL_MS;
    const shouldPrompt = record.name === '' && !record.promptNever && record.timesHeard >= 2 && promptCooledDown;
    if (shouldPrompt) {
      record.lastPromptMs = nowMs;
    }
    const occurrence: Occurrence = { record: record, shouldAlert: shouldAlert, shouldPrompt: shouldPrompt };
    return occurrence;
  }

  // ---- editing ----------------------------------------------------------------------------------

  private static cleanName(name: string): string {
    return name.trim().slice(0, SoundCatalog.MAX_NAME_LENGTH).trim();
  }

  rename(id: number, name: string): void {
    const record = this.find(id);
    if (record !== undefined) {
      record.name = SoundCatalog.cleanName(name);
    }
  }

  setAlerts(id: number, enabled: boolean): void {
    const record = this.find(id);
    if (record !== undefined) {
      record.alertsEnabled = enabled;
    }
  }

  setPromptNever(id: number): void {
    const record = this.find(id);
    if (record !== undefined) {
      record.promptNever = true;
    }
  }

  // ---- plain words ------------------------------------------------------------------------------

  // e.g. "High-pitched, 3 beeps per second, about 2.1 s long"
  static describe(p: SoundProfile): string {
    if (p.dominantHz <= 0 && p.beepCount === 0) {
      return 'Still learning this sound';
    }
    const parts: string[] = [];
    if (p.dominantHz > 0) {
      parts.push(p.dominantHz < 700 ? 'Low-pitched' : (p.dominantHz < 1800 ? 'Mid-pitched' : 'High-pitched'));
    }
    if (p.repetition === 2) {
      parts.push('continuous tone');
    } else if (p.repetition === 1 && p.beepsPerSec >= 1) {
      parts.push(String(Math.round(p.beepsPerSec * 2) / 2) + ' beeps per second');
    } else if (p.repetition === 1 && p.beepsPerSec > 0) {
      const every = Math.round(1 / p.beepsPerSec);
      parts.push(every <= 1 ? 'one beep every second' : 'one beep every ' + every + ' seconds');
    } else if (p.repetition === 1) {
      parts.push(p.beepCount + ' beeps');
    } else if (p.modulation === 2) {
      parts.push('sweeping tone');
    } else {
      parts.push('single beep');
    }
    parts.push('about ' + p.durationSec.toFixed(1) + ' s long');
    return parts.join(', ');
  }
}
