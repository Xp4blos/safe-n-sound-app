// The built-in sound classes the engine reports besides alarms and the user's own sounds.

export interface SoundClass {
  key: string; // the engine's event type, e.g. 'siren'
  label: string; // history record label, 'class:siren'
  title: string; // shown in History and in notifications
  vibration: number[]; // on, off, on, ... in ms (always ends with an "on")
  selfVibrationSensitive: boolean; // level-only detectors: ignored while the phone itself is vibrating
  explainBackSec: number; // alarm tones up to this long before the event belong to the same sound (0: none)
}

function make(key: string, title: string, vibration: number[], selfVibrationSensitive: boolean,
  explainBackSec: number): SoundClass {
  const c: SoundClass = {
    key: key, label: 'class:' + key, title: title, vibration: vibration, selfVibrationSensitive: selfVibrationSensitive,
    explainBackSec: explainBackSec
  };
  return c;
}

const ALL: SoundClass[] = [
  make('siren', 'Siren', [500, 250, 500, 250, 500], false, 6),
  make('scream', 'Scream', [150, 100, 150, 100, 150, 250, 450, 250, 450], false, 3), // short-short-short-long-long
  make('cry', 'Baby crying', [700, 350, 700], false, 6),
  make('chirp', 'Beep or chirp', [200, 150, 200], false, 0),
  make('knock', 'Knock', [300], true, 0),
  make('loud_sound', 'Loud sound', [400, 200, 400], true, 0)
];

export class SoundClasses {
  static readonly PREFIX: string = 'class:';

  static all(): SoundClass[] {
    return ALL;
  }

  // The class of an engine event type; undefined for 'alarm', 'custom' and anything unknown.
  static forEvent(type: string): SoundClass | undefined {
    return ALL.find((c: SoundClass): boolean => c.key === type);
  }

  static isBuiltInLabel(label: string): boolean {
    return label.startsWith(SoundClasses.PREFIX);
  }
}
