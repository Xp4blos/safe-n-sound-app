import { SoundClasses } from './SoundClasses';

// User settings (Settings tab). Pure data with tolerant parsing: anything unreadable falls back to the default.
export class AppSettings {
  vibrate: boolean = true; // vibrate on detections
  notify: boolean = true; // post system notifications for detections
  keepListeningInBackground: boolean = true; // keep the microphone on when the app is not on screen
  autoStart: boolean = false; // start listening when the app opens
  speechEnabled: boolean = true; // run the speech recogniser (captions and keyword alerts) while listening
  speechOnPartial: boolean = true; // alert on keywords in interim results (lowest latency)
  private classes: Map<string, boolean> = new Map<string, boolean>();

  isClassOn(key: string): boolean {
    if (SoundClasses.forEvent(key) === undefined) {
      return false;
    }
    const value = this.classes.get(key);
    return value === undefined ? true : value; // every class alerts by default
  }

  setClass(key: string, on: boolean): void {
    if (SoundClasses.forEvent(key) !== undefined) {
      this.classes.set(key, on);
    }
  }

  serialize(): string {
    const classes: Record<string, boolean> = {};
    for (const c of SoundClasses.all()) {
      classes[c.key] = this.isClassOn(c.key);
    }
    const out: Record<string, Object> = {
      'vibrate': this.vibrate, 'notify': this.notify, 'keepListeningInBackground': this.keepListeningInBackground,
      'autoStart': this.autoStart, 'speechEnabled': this.speechEnabled, 'speechOnPartial': this.speechOnPartial,
      'classes': classes
    };
    return JSON.stringify(out);
  }

  static parse(json: string): AppSettings {
    const s = new AppSettings();
    let parsed: Object;
    try {
      parsed = JSON.parse(json) as Object;
    } catch (e) {
      return s;
    }
    if (parsed === null || typeof parsed !== 'object') {
      return s;
    }
    const o = parsed as Record<string, Object>;
    if (typeof o['vibrate'] === 'boolean') {
      s.vibrate = o['vibrate'] as boolean;
    }
    if (typeof o['notify'] === 'boolean') {
      s.notify = o['notify'] as boolean;
    }
    if (typeof o['keepListeningInBackground'] === 'boolean') {
      s.keepListeningInBackground = o['keepListeningInBackground'] as boolean;
    }
    if (typeof o['autoStart'] === 'boolean') {
      s.autoStart = o['autoStart'] as boolean;
    }
    if (typeof o['speechEnabled'] === 'boolean') {
      s.speechEnabled = o['speechEnabled'] as boolean;
    }
    if (typeof o['speechOnPartial'] === 'boolean') {
      s.speechOnPartial = o['speechOnPartial'] as boolean;
    }
    const classes = o['classes'];
    if (classes !== undefined && classes !== null && typeof classes === 'object') {
      const c = classes as Record<string, Object>;
      for (const known of SoundClasses.all()) {
        if (typeof c[known.key] === 'boolean') {
          s.setClass(known.key, c[known.key] as boolean);
        }
      }
    }
    return s;
  }
}
