import { describe, it, expect } from '@ohos/hypium';
import { AppSettings } from '../main/ets/model/AppSettings';
import { SoundCatalog } from '../main/ets/model/SoundCatalog';
import { SoundClass } from '../main/ets/model/SoundClasses';
import { EngineEvent, LearnOutcome, SoundProfile, SoundRecord } from '../main/ets/model/SoundModels';
import { AlertPort, EnginePort, PipelineListener, SavePort, SoundPipeline } from '../main/ets/services/SoundPipeline';

function profile(): SoundProfile {
  const p: SoundProfile = {
    dominantHz: 2000, durationSec: 1.5, beepCount: 3, beepsPerSec: 3, repetition: 1, modulation: 1,
    envelope: [1, 0.5, 1, 0.5, 1, 0.5, 1, 0.5]
  };
  return p;
}

class FakeEngine implements EnginePort {
  results: LearnOutcome[] = [];
  calls: string[] = [];
  private next: number = 0;

  learnSound(label: string, eventTimeSec: number): LearnOutcome {
    this.calls.push(label + '@' + eventTimeSec);
    const r = this.next < this.results.length ? this.results[this.next] : FakeEngine.good();
    this.next++;
    return r;
  }

  static good(): LearnOutcome {
    const o: LearnOutcome = { ok: true, message: '', template: 'QUJD', profile: profile(), consistency: 1 };
    return o;
  }

  static bad(message: string): LearnOutcome {
    const o: LearnOutcome = { ok: false, message: message, template: '', profile: profile(), consistency: 0 };
    return o;
  }
}

class FakeAlerts implements AlertPort {
  unknownPulses: number = 0;
  namedPatterns: number = 0;
  namedNotes: string[] = [];
  prompts: number = 0;
  classPatterns: string[] = [];
  classNotes: string[] = [];
  vibrating: boolean = false; // the phone's own noise: its vibration or a touch on the screen

  vibrateUnknown(): void {
    this.unknownPulses++;
  }

  vibrateNamed(): void {
    this.namedPatterns++;
  }

  notifyNamed(name: string, timeMs: number): void {
    this.namedNotes.push(name + '@' + timeMs);
  }

  notifyPrompt(): void {
    this.prompts++;
  }

  vibrateClass(cls: SoundClass): void {
    this.classPatterns.push(cls.key);
  }

  notifyClass(cls: SoundClass, timeMs: number): void {
    this.classNotes.push(cls.title + '@' + timeMs);
  }

  isSelfNoise(nowMs: number): boolean {
    return this.vibrating;
  }
}

class FakeSaver implements SavePort {
  saves: number = 0;

  async save(): Promise<void> {
    this.saves++;
  }
}

class FakeListener implements PipelineListener {
  heard: string[] = [];
  alerts: string[] = [];
  prompts: string[] = [];
  classAlerts: string[] = [];
  detections: number = 0;
  changes: number = 0;

  onSoundHeard(record: SoundRecord, isNew: boolean): void {
    this.heard.push(record.label + (isNew ? ':new' : ':again'));
  }

  onAlert(record: SoundRecord, timeMs: number): void {
    this.alerts.push(record.name + '@' + timeMs);
  }

  onPrompt(record: SoundRecord): void {
    this.prompts.push(record.label);
  }

  onClassAlert(record: SoundRecord, timeMs: number): void {
    this.classAlerts.push(record.name + '@' + timeMs);
  }

  onDetection(timeMs: number): void {
    this.detections++;
  }

  onChanged(): void {
    this.changes++;
  }
}

function alarm(timeSec: number): EngineEvent {
  const e: EngineEvent = { type: 'alarm', timeSec: timeSec, startSec: timeSec, confidence: 1, freqHz: 2000, levelDb: -20, label: '' };
  return e;
}

function custom(label: string, startSec: number, timeSec: number): EngineEvent {
  const e: EngineEvent = { type: 'custom', timeSec: timeSec, startSec: startSec, confidence: 1, freqHz: 0, levelDb: -20, label: label };
  return e;
}

function builtIn(type: string, timeSec: number): EngineEvent {
  const e: EngineEvent = { type: type, timeSec: timeSec, startSec: timeSec, confidence: 1, freqHz: 0, levelDb: -20, label: '' };
  return e;
}

class Rig {
  engine: FakeEngine = new FakeEngine();
  alerts: FakeAlerts = new FakeAlerts();
  saver: FakeSaver = new FakeSaver();
  listener: FakeListener = new FakeListener();
  catalog: SoundCatalog = new SoundCatalog([]);
  settings: AppSettings = new AppSettings();
  pipeline: SoundPipeline =
    new SoundPipeline(this.catalog, this.engine, this.alerts, this.saver, this.listener, this.settings);

  // A previously learned sound 'snd-1' (optionally named).
  withKnownSound(name: string): Rig {
    const p = this.catalog.createPending(1000);
    this.catalog.completeLearning(p.label, 'QUJD', profile());
    if (name !== '') {
      this.catalog.rename(p.id, name);
    }
    return this;
  }
}

const T0: number = 5000000;

export default function soundPipelineTest() {
  describe('soundPipeline', () => {
    it('an_unexplained_alarm_becomes_an_unknown_sound_after_the_hold_time', 0, () => {
      const rig = new Rig();
      rig.pipeline.handle([alarm(10)], T0);
      expect(rig.listener.detections).assertEqual(1);
      rig.pipeline.tick(T0 + SoundPipeline.HOLD_MS - 100);
      expect(rig.catalog.records().length).assertEqual(0);
      rig.pipeline.tick(T0 + SoundPipeline.HOLD_MS + 100);
      expect(rig.catalog.records().length).assertEqual(1);
      expect(rig.listener.heard[0]).assertEqual('snd-1:new');
      expect(rig.alerts.unknownPulses).assertEqual(1);
      expect(rig.engine.calls[0]).assertEqual('snd-1@10');
      expect(rig.catalog.findByLabel('snd-1')?.template).assertEqual('QUJD');
      expect(rig.saver.saves > 0).assertTrue();
    });

    it('a_sound_that_could_not_be_learned_is_forgotten', 0, () => {
      const rig = new Rig();
      rig.engine.results = [FakeEngine.bad('The sound is not clear enough.')];
      rig.pipeline.handle([alarm(10)], T0);
      rig.pipeline.tick(T0 + SoundPipeline.HOLD_MS + 100);
      expect(rig.catalog.records().length).assertEqual(0);
      expect(rig.listener.changes > 0).assertTrue();
    });

    it('learning_is_retried_when_the_audio_is_not_there_yet', 0, () => {
      const rig = new Rig();
      rig.engine.results = [FakeEngine.bad('Not enough audio after the sound yet.'), FakeEngine.good()];
      rig.pipeline.handle([alarm(10)], T0);
      rig.pipeline.tick(T0 + SoundPipeline.HOLD_MS + 100);
      expect(rig.catalog.findByLabel('snd-1')?.template).assertEqual('');
      rig.pipeline.tick(T0 + SoundPipeline.HOLD_MS + SoundPipeline.RETRY_MS + 200);
      expect(rig.catalog.findByLabel('snd-1')?.template).assertEqual('QUJD');
      expect(rig.engine.calls.length).assertEqual(2);
    });

    it('a_recognised_sound_explains_the_alarm_so_no_duplicate_is_created', 0, () => {
      const rig = new Rig().withKnownSound('');
      rig.pipeline.handle([alarm(10)], T0);
      rig.pipeline.handle([custom('snd-1', 9.6, 12.5)], T0 + 2500);
      rig.pipeline.tick(T0 + 10000);
      expect(rig.catalog.records().length).assertEqual(1);
      expect(rig.catalog.find(1)?.timesHeard).assertEqual(2);
      expect(rig.engine.calls.length).assertEqual(0);
    });

    it('a_beep_that_arrives_just_after_the_match_is_still_explained_by_it', 0, () => {
      // From a real run: the stored sound was matched after two beeps, the third beep of the same play (inside the
      // matched window) arrived 0.4 s later and was learned again as a duplicate unknown sound.
      const rig = new Rig().withKnownSound('');
      rig.pipeline.handle([alarm(27.74)], T0);
      rig.pipeline.handle([alarm(28.67)], T0 + 930);
      rig.pipeline.handle([custom('snd-1', 26.11, 29.15)], T0 + 1410);
      rig.pipeline.handle([alarm(29.57)], T0 + 1830);
      rig.pipeline.tick(T0 + 10000);
      expect(rig.catalog.records().length).assertEqual(1);
      expect(rig.engine.calls.length).assertEqual(0);
    });

    it('an_alarm_long_after_a_match_is_still_a_new_sound', 0, () => {
      const rig = new Rig().withKnownSound('');
      rig.pipeline.handle([custom('snd-1', 26.11, 29.15)], T0);
      rig.pipeline.handle([alarm(40)], T0 + 10850);
      rig.pipeline.tick(T0 + 20000);
      expect(rig.catalog.records().length).assertEqual(2);
    });

    // ---- built-in classes ----------------------------------------------------------------------------

    it('a_siren_is_recorded_in_history_vibrates_notifies_and_shows_a_card', 0, () => {
      const rig = new Rig();
      rig.pipeline.handle([builtIn('siren', 10)], T0);
      expect(rig.catalog.records().length).assertEqual(1);
      expect(rig.catalog.records()[0].name).assertEqual('Siren');
      expect(rig.catalog.records()[0].timesHeard).assertEqual(1);
      expect(rig.alerts.classPatterns.join(',')).assertEqual('siren');
      expect(rig.alerts.classNotes.join(',')).assertEqual('Siren@' + T0);
      expect(rig.listener.classAlerts.join(',')).assertEqual('Siren@' + T0);
      expect(rig.saver.saves > 0).assertTrue();
    });

    it('a_class_that_is_switched_off_is_ignored_completely', 0, () => {
      const rig = new Rig();
      rig.settings.setClass('cry', false);
      rig.pipeline.handle([builtIn('cry', 10)], T0);
      expect(rig.catalog.records().length).assertEqual(0);
      expect(rig.alerts.classPatterns.length).assertEqual(0);
      expect(rig.listener.classAlerts.length).assertEqual(0);
    });

    it('a_repeated_class_alerts_once_per_cooldown_but_keeps_counting', 0, () => {
      const rig = new Rig();
      rig.pipeline.handle([builtIn('scream', 10)], T0);
      rig.pipeline.handle([builtIn('scream', 14)], T0 + 4000);
      rig.pipeline.handle([builtIn('scream', 40)], T0 + 30000);
      expect(rig.alerts.classPatterns.length).assertEqual(2);
      expect(rig.catalog.records()[0].timesHeard).assertEqual(3);
    });

    it('knocks_and_loud_sounds_are_ignored_while_the_phone_makes_noise_itself_but_sirens_are_not', 0, () => {
      const rig = new Rig();
      rig.alerts.vibrating = true;
      rig.pipeline.handle([builtIn('knock', 10), builtIn('loud_sound', 11), builtIn('siren', 12)], T0);
      rig.pipeline.tick(T0 + 10000);
      expect(rig.alerts.classPatterns.join(',')).assertEqual('siren');
      expect(rig.catalog.records().length).assertEqual(1);
    });

    it('the_vibrate_and_notify_switches_apply_to_classes_named_and_unknown_sounds', 0, () => {
      const rig = new Rig().withKnownSound('Doorbell');
      rig.settings.vibrate = false;
      rig.settings.notify = false;
      rig.pipeline.handle([builtIn('siren', 10)], T0);
      rig.pipeline.handle([custom('snd-1', 20, 22)], T0 + 20000);
      rig.pipeline.handle([alarm(60)], T0 + 60000);
      rig.pipeline.tick(T0 + 70000);
      expect(rig.alerts.classPatterns.length).assertEqual(0);
      expect(rig.alerts.classNotes.length).assertEqual(0);
      expect(rig.alerts.namedPatterns).assertEqual(0);
      expect(rig.alerts.namedNotes.length).assertEqual(0);
      expect(rig.alerts.unknownPulses).assertEqual(0);
      expect(rig.listener.classAlerts.length).assertEqual(1); // the on-screen card still shows
      expect(rig.listener.alerts.length).assertEqual(1);
    });

    it('a_beep_inside_a_recognised_sound_is_not_also_reported_as_a_chirp', 0, () => {
      // From a real run: the first beep of a taught sound is a chirp for the engine; the match comes seconds later.
      const rig = new Rig().withKnownSound('Microwave alarm');
      rig.pipeline.handle([builtIn('chirp', 10)], T0);
      rig.pipeline.handle([builtIn('chirp', 11.2)], T0 + 1200);
      rig.pipeline.handle([custom('snd-1', 9.6, 12.5)], T0 + 2500);
      rig.pipeline.tick(T0 + 10000);
      expect(rig.alerts.classPatterns.length).assertEqual(0);
      expect(rig.alerts.namedPatterns).assertEqual(1);
      expect(rig.catalog.records().length).assertEqual(1);
    });

    it('a_chirp_on_its_own_is_reported_after_the_short_hold', 0, () => {
      const rig = new Rig();
      rig.pipeline.handle([builtIn('chirp', 10)], T0);
      expect(rig.alerts.classPatterns.length).assertEqual(0); // still held
      rig.pipeline.tick(T0 + 3500);
      expect(rig.alerts.classPatterns.join(',')).assertEqual('chirp');
    });

    it('chirps_that_come_with_a_siren_belong_to_it', 0, () => {
      const rig = new Rig();
      rig.pipeline.handle([builtIn('chirp', 10.5)], T0 + 500);
      rig.pipeline.handle([builtIn('siren', 12.1)], T0 + 2100);
      rig.pipeline.handle([builtIn('chirp', 13.4)], T0 + 3400); // the wail still going on after the siren was confirmed
      rig.pipeline.tick(T0 + 10000);
      expect(rig.alerts.classPatterns.join(',')).assertEqual('siren');
      expect(rig.catalog.records().length).assertEqual(1);
    });

    it('a_loud_sound_that_turns_out_to_be_a_siren_is_not_reported_as_loud', 0, () => {
      // The engine reports the onset of a siren as a loud sound first and confirms the siren about 2 s later.
      const rig = new Rig();
      rig.pipeline.handle([builtIn('loud_sound', 10)], T0);
      rig.pipeline.handle([builtIn('siren', 12.1)], T0 + 2100);
      rig.pipeline.tick(T0 + 10000);
      expect(rig.alerts.classPatterns.join(',')).assertEqual('siren');
      expect(rig.catalog.records().length).assertEqual(1);
      expect(rig.catalog.records()[0].name).assertEqual('Siren');
    });

    it('a_loud_sound_on_its_own_is_reported_after_the_short_hold', 0, () => {
      const rig = new Rig();
      rig.pipeline.handle([builtIn('loud_sound', 10)], T0);
      expect(rig.alerts.classPatterns.length).assertEqual(0); // still held
      rig.pipeline.tick(T0 + 3500);
      expect(rig.alerts.classPatterns.join(',')).assertEqual('loud_sound');
      expect(rig.catalog.records()[0].timesHeard).assertEqual(1);
    });

    it('a_siren_explains_the_alarm_events_in_its_window_so_no_unknown_sound_is_created', 0, () => {
      const rig = new Rig();
      rig.pipeline.handle([alarm(10)], T0);
      rig.pipeline.handle([alarm(10.8)], T0 + 800);
      rig.pipeline.handle([builtIn('siren', 12.2)], T0 + 2200);
      rig.pipeline.handle([alarm(12.6)], T0 + 2600); // a late beep of the same siren
      rig.pipeline.tick(T0 + 20000);
      expect(rig.catalog.records().length).assertEqual(1); // only the Siren record
      expect(rig.engine.calls.length).assertEqual(0);
    });

    it('the_second_occurrence_of_an_unnamed_sound_asks_to_name_it', 0, () => {
      const rig = new Rig().withKnownSound('');
      rig.pipeline.handle([custom('snd-1', 9, 11)], T0);
      expect(rig.listener.prompts.length).assertEqual(1);
      expect(rig.alerts.prompts).assertEqual(1);
      expect(rig.alerts.unknownPulses).assertEqual(1);
      expect(rig.alerts.namedPatterns).assertEqual(0);
      rig.pipeline.handle([custom('snd-1', 20, 22)], T0 + 30000);
      expect(rig.listener.prompts.length).assertEqual(1);
    });

    it('a_named_sound_vibrates_notifies_and_shows_a_card', 0, () => {
      const rig = new Rig().withKnownSound('Doorbell');
      rig.pipeline.handle([custom('snd-1', 9, 11)], T0);
      expect(rig.alerts.namedPatterns).assertEqual(1);
      expect(rig.alerts.namedNotes[0]).assertEqual('Doorbell@' + T0);
      expect(rig.listener.alerts[0]).assertEqual('Doorbell@' + T0);
      expect(rig.alerts.prompts).assertEqual(0);
      expect(rig.alerts.unknownPulses).assertEqual(0);
    });

    it('a_repeating_named_sound_alerts_once_per_cooldown_but_keeps_counting', 0, () => {
      const rig = new Rig().withKnownSound('Doorbell');
      rig.pipeline.handle([custom('snd-1', 9, 11)], T0);
      rig.pipeline.handle([custom('snd-1', 12, 14)], T0 + 3000);
      rig.pipeline.handle([custom('snd-1', 15, 17)], T0 + 6000);
      expect(rig.alerts.namedPatterns).assertEqual(1);
      expect(rig.alerts.namedNotes.length).assertEqual(1);
      expect(rig.listener.heard.length).assertEqual(3);
      expect(rig.catalog.find(1)?.timesHeard).assertEqual(4);
      rig.pipeline.handle([custom('snd-1', 30, 32)], T0 + 20000);
      expect(rig.alerts.namedPatterns).assertEqual(2);
    });

    it('a_named_sound_with_alerts_off_stays_silent', 0, () => {
      const rig = new Rig().withKnownSound('Doorbell');
      rig.catalog.setAlerts(1, false);
      rig.pipeline.handle([custom('snd-1', 9, 11)], T0);
      expect(rig.alerts.namedPatterns).assertEqual(0);
      expect(rig.alerts.unknownPulses).assertEqual(0);
      expect(rig.listener.heard.length).assertEqual(1);
    });

    it('several_alarms_in_one_burst_create_one_sound', 0, () => {
      const rig = new Rig();
      rig.pipeline.handle([alarm(10)], T0);
      rig.pipeline.handle([alarm(11)], T0 + 1000);
      rig.pipeline.handle([alarm(12)], T0 + 2000);
      rig.pipeline.tick(T0 + 8000);
      expect(rig.catalog.records().length).assertEqual(1);
    });

    it('an_unknown_label_from_the_engine_is_ignored', 0, () => {
      const rig = new Rig();
      rig.pipeline.handle([custom('snd-99', 1, 2)], T0);
      expect(rig.catalog.records().length).assertEqual(0);
      expect(rig.alerts.unknownPulses).assertEqual(0);
    });

    it('nothing_happens_for_an_empty_batch', 0, () => {
      const rig = new Rig();
      rig.pipeline.handle([], T0);
      rig.pipeline.tick(T0 + 10000);
      expect(rig.listener.detections).assertEqual(0);
      expect(rig.saver.saves).assertEqual(0);
    });
  });
}
