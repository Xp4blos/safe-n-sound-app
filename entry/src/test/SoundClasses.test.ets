import { describe, it, expect } from '@ohos/hypium';
import { SoundCatalog } from '../main/ets/model/SoundCatalog';
import { AppSettings } from '../main/ets/model/AppSettings';
import { SoundClasses } from '../main/ets/model/SoundClasses';

export default function soundClassesTest() {
  describe('soundClasses', () => {
    it('every_built_in_engine_event_type_has_a_class_and_the_alarm_has_none', 0, () => {
      const types: string[] = ['siren', 'scream', 'cry', 'chirp', 'knock', 'loud_sound'];
      for (const t of types) {
        expect(SoundClasses.forEvent(t) !== undefined).assertTrue();
      }
      expect(SoundClasses.forEvent('alarm') === undefined).assertTrue();
      expect(SoundClasses.forEvent('custom') === undefined).assertTrue();
      expect(SoundClasses.forEvent('nonsense') === undefined).assertTrue();
    });

    it('classes_have_readable_titles_distinct_vibrations_and_a_stable_label', 0, () => {
      const siren = SoundClasses.forEvent('siren');
      const baby = SoundClasses.forEvent('cry');
      expect(siren?.title).assertEqual('Siren');
      expect(baby?.title).assertEqual('Baby crying');
      expect(SoundClasses.forEvent('loud_sound')?.title).assertEqual('Loud sound');
      expect(siren?.label).assertEqual('class:siren');
      expect(JSON.stringify(siren?.vibration) === JSON.stringify(baby?.vibration)).assertFalse();
      expect((siren?.vibration.length ?? 0) % 2).assertEqual(1); // on, off, on, ... always ends with an "on"
    });

    it('built_in_labels_are_recognised', 0, () => {
      expect(SoundClasses.isBuiltInLabel('class:siren')).assertTrue();
      expect(SoundClasses.isBuiltInLabel('snd-3')).assertFalse();
    });

    it('only_knocks_and_loud_sounds_are_ignored_while_the_phone_vibrates', 0, () => {
      expect(SoundClasses.forEvent('knock')?.selfVibrationSensitive).assertTrue();
      expect(SoundClasses.forEvent('loud_sound')?.selfVibrationSensitive).assertTrue();
      expect(SoundClasses.forEvent('siren')?.selfVibrationSensitive).assertFalse();
      expect(SoundClasses.forEvent('cry')?.selfVibrationSensitive).assertFalse();
    });

    it('a_class_gets_a_history_record_that_is_named_hidden_from_my_sounds_and_counted', 0, () => {
      const catalog = new SoundCatalog([]);
      const siren = SoundClasses.forEvent('siren');
      if (siren === undefined) {
        expect(false).assertTrue();
        return;
      }
      const first = catalog.ensureBuiltIn(siren.label, siren.title, 1000000);
      expect(first.name).assertEqual('Siren');
      expect(first.timesHeard).assertEqual(0);
      expect(first.template).assertEqual('');
      const again = catalog.ensureBuiltIn(siren.label, siren.title, 1000500);
      expect(again.id).assertEqual(first.id);
      expect(catalog.records().length).assertEqual(1);
      const occurrence = catalog.registerOccurrence(siren.label, 1001000);
      expect(occurrence?.shouldAlert).assertTrue();
      expect(occurrence?.record.timesHeard).assertEqual(1);
      expect(catalog.registerOccurrence(siren.label, 1002000)?.shouldAlert).assertFalse(); // inside the cooldown
      expect(catalog.learnedTemplates().length).assertEqual(0); // nothing for the engine to load
      expect(catalog.userSounds().length).assertEqual(0); // not in My sounds
    });

    it('built_in_records_survive_saving_and_do_not_disturb_learned_sound_ids', 0, () => {
      const catalog = new SoundCatalog([]);
      catalog.ensureBuiltIn('class:cry', 'Baby crying', 1000);
      const pending = catalog.createPending(2000);
      expect(pending.label).assertEqual('snd-2');
      const restored = SoundCatalog.parse(catalog.serialize());
      expect(restored.records().length).assertEqual(2);
      expect(restored.findByLabel('class:cry')?.name).assertEqual('Baby crying');
      expect(restored.userSounds().length).assertEqual(1);
    });
  });

  describe('appSettings', () => {
    it('defaults_alert_on_every_class_and_keep_listening_in_the_background', 0, () => {
      const s = new AppSettings();
      expect(s.isClassOn('siren')).assertTrue();
      expect(s.isClassOn('knock')).assertTrue();
      expect(s.isClassOn('loud_sound')).assertTrue();
      expect(s.vibrate).assertTrue();
      expect(s.notify).assertTrue();
      expect(s.keepListeningInBackground).assertTrue();
      expect(s.autoStart).assertFalse();
      expect(s.speechEnabled).assertTrue();
    });

    it('a_class_can_be_switched_off_and_unknown_classes_are_off', 0, () => {
      const s = new AppSettings();
      s.setClass('knock', false);
      expect(s.isClassOn('knock')).assertFalse();
      expect(s.isClassOn('siren')).assertTrue();
      expect(s.isClassOn('nonsense')).assertFalse();
    });

    it('settings_survive_a_round_trip_and_garbage_gives_defaults', 0, () => {
      const s = new AppSettings();
      s.setClass('cry', false);
      s.notify = false;
      s.autoStart = true;
      const restored = AppSettings.parse(s.serialize());
      expect(restored.isClassOn('cry')).assertFalse();
      expect(restored.notify).assertFalse();
      expect(restored.autoStart).assertTrue();
      expect(restored.vibrate).assertTrue();
      expect(AppSettings.parse('not json').isClassOn('siren')).assertTrue();
      expect(AppSettings.parse('{"vibrate": 5}').vibrate).assertTrue(); // wrong type: default
    });
  });
}
