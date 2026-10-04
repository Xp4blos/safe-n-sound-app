import { describe, it, expect } from '@ohos/hypium';
import { Occurrence, SoundCatalog } from '../main/ets/model/SoundCatalog';
import { SoundProfile, SoundRecord } from '../main/ets/model/SoundModels';

function profile(hz: number, repetition: number, beeps: number, rate: number): SoundProfile {
  const p: SoundProfile = {
    dominantHz: hz, durationSec: 2.1, beepCount: beeps, beepsPerSec: rate, repetition: repetition,
    modulation: repetition === 1 ? 1 : 0, envelope: [1, 0.5, 1, 0.5, 1, 0.5, 1, 0.5]
  };
  return p;
}

const MIN: number = 60000;

function learnedCatalog(): SoundCatalog {
  const catalog = new SoundCatalog([]);
  const pending = catalog.createPending(1000000);
  catalog.completeLearning(pending.label, 'AAAA', profile(2000, 1, 3, 3));
  return catalog;
}

export default function soundCatalogTest() {
  describe('soundCatalog', () => {
    it('pending_records_get_increasing_ids_and_unique_labels', 0, () => {
      const catalog = new SoundCatalog([]);
      const a = catalog.createPending(1000);
      const b = catalog.createPending(2000);
      expect(a.id).assertEqual(1);
      expect(b.id).assertEqual(2);
      expect(a.label).assertEqual('snd-1');
      expect(b.label).assertEqual('snd-2');
      expect(a.name).assertEqual('');
      expect(a.template).assertEqual('');
      expect(a.timesHeard).assertEqual(1);
      expect(a.alertsEnabled).assertTrue();
    });

    it('completing_learning_stores_template_and_profile', 0, () => {
      const catalog = new SoundCatalog([]);
      const p = catalog.createPending(1000);
      expect(catalog.completeLearning(p.label, 'QUJD', profile(2000, 1, 3, 3))).assertTrue();
      expect(catalog.findByLabel(p.label)?.template).assertEqual('QUJD');
      expect(catalog.learnedTemplates().length).assertEqual(1);
      expect(catalog.completeLearning('snd-99', 'x', profile(1, 0, 1, 0))).assertFalse();
    });

    it('pending_records_are_not_offered_for_registration_and_can_be_discarded', 0, () => {
      const catalog = new SoundCatalog([]);
      const p = catalog.createPending(1000);
      expect(catalog.learnedTemplates().length).assertEqual(0);
      expect(catalog.discard(p.label)).assertTrue();
      expect(catalog.records().length).assertEqual(0);
      expect(catalog.discard(p.label)).assertFalse();
    });

    it('occurrence_of_an_unknown_label_is_ignored', 0, () => {
      const catalog = learnedCatalog();
      expect(catalog.registerOccurrence('snd-42', 5000000) === undefined).assertTrue();
    });

    it('second_occurrence_of_an_unnamed_sound_asks_to_name_it', 0, () => {
      const catalog = learnedCatalog();
      const second: Occurrence | undefined = catalog.registerOccurrence('snd-1', 1000000 + MIN);
      expect(second?.record.timesHeard).assertEqual(2);
      expect(second?.shouldPrompt).assertTrue();
      expect(second?.shouldAlert).assertFalse();
    });

    it('prompt_is_not_repeated_within_ten_minutes_but_returns_after', 0, () => {
      const catalog = learnedCatalog();
      const t0 = 1000000 + MIN;
      expect(catalog.registerOccurrence('snd-1', t0)?.shouldPrompt).assertTrue();
      expect(catalog.registerOccurrence('snd-1', t0 + 5 * MIN)?.shouldPrompt).assertFalse();
      expect(catalog.registerOccurrence('snd-1', t0 + 10 * MIN)?.shouldPrompt).assertTrue();
    });

    it('dont_ask_again_stops_prompts', 0, () => {
      const catalog = learnedCatalog();
      catalog.setPromptNever(1);
      expect(catalog.registerOccurrence('snd-1', 1000000 + 60 * MIN)?.shouldPrompt).assertFalse();
    });

    it('named_sound_alerts_with_a_fifteen_second_cooldown', 0, () => {
      const catalog = learnedCatalog();
      catalog.rename(1, 'Doorbell');
      const t0 = 2000000;
      expect(catalog.registerOccurrence('snd-1', t0)?.shouldAlert).assertTrue();
      expect(catalog.registerOccurrence('snd-1', t0 + 10000)?.shouldAlert).assertFalse();
      expect(catalog.registerOccurrence('snd-1', t0 + 16000)?.shouldAlert).assertTrue();
      expect(catalog.registerOccurrence('snd-1', t0 + 16000)?.shouldPrompt).assertFalse();
    });

    it('named_sound_with_alerts_off_never_alerts', 0, () => {
      const catalog = learnedCatalog();
      catalog.rename(1, 'Doorbell');
      catalog.setAlerts(1, false);
      expect(catalog.registerOccurrence('snd-1', 2000000)?.shouldAlert).assertFalse();
    });

    it('occurrences_update_count_last_seen_and_a_capped_history', 0, () => {
      const catalog = learnedCatalog();
      for (let i = 1; i <= 30; i++) {
        catalog.registerOccurrence('snd-1', 1000000 + i * 1000);
      }
      const r = catalog.findByLabel('snd-1');
      expect(r?.timesHeard).assertEqual(31);
      expect(r?.lastSeenMs).assertEqual(1030000);
      expect(r?.recentMs.length).assertEqual(20);
    });

    it('rename_trims_limits_and_clears', 0, () => {
      const catalog = learnedCatalog();
      catalog.rename(1, '  Doorbell  ');
      expect(catalog.find(1)?.name).assertEqual('Doorbell');
      catalog.rename(1, '   ');
      expect(catalog.find(1)?.name).assertEqual('');
      catalog.rename(1, 'x'.repeat(100));
      expect(catalog.find(1)?.name.length).assertEqual(40);
    });

    it('taught_sounds_are_named_and_start_with_zero_occurrences', 0, () => {
      const catalog = new SoundCatalog([]);
      const label = catalog.reserveLabel();
      const auto = catalog.createPending(4000);
      const r = catalog.addTaught(label, '  Microwave alarm ', 'QUJD', profile(2500, 1, 3, 2), 5000);
      expect(auto.label === label).assertFalse();
      expect(r.label).assertEqual(label);
      expect(r.name).assertEqual('Microwave alarm');
      expect(r.timesHeard).assertEqual(0);
      expect(r.template).assertEqual('QUJD');
      expect(catalog.learnedTemplates().length).assertEqual(1);
    });

    it('remove_returns_the_record_so_the_engine_can_forget_it', 0, () => {
      const catalog = learnedCatalog();
      const removed = catalog.remove(1);
      expect(removed?.label).assertEqual('snd-1');
      expect(catalog.records().length).assertEqual(0);
      expect(catalog.remove(1) === undefined).assertTrue();
    });

    it('parse_survives_corrupt_and_old_format_data', 0, () => {
      expect(SoundCatalog.parse('{not json').records().length).assertEqual(0);
      expect(SoundCatalog.parse('').records().length).assertEqual(0);
      expect(SoundCatalog.parse('{"a":1}').records().length).assertEqual(0);
      // First-version records had a fingerprint and no label/template: dropped, not crashed on.
      expect(SoundCatalog.parse('[{"id":1,"kind":0,"fingerprint":[1,2],"name":"","alertsEnabled":true}]')
        .records().length).assertEqual(0);
    });

    it('serialize_then_parse_round_trips', 0, () => {
      const catalog = learnedCatalog();
      catalog.rename(1, 'Alarm');
      const copy = SoundCatalog.parse(catalog.serialize());
      expect(copy.records().length).assertEqual(1);
      const r: SoundRecord | undefined = copy.find(1);
      expect(r?.name).assertEqual('Alarm');
      expect(r?.template).assertEqual('AAAA');
      expect(r?.profile.envelope.length).assertEqual(8);
    });

    it('describe_explains_a_sound_in_plain_words', 0, () => {
      expect(SoundCatalog.describe(profile(3000, 1, 3, 3))).assertEqual('High-pitched, 3 beeps per second, about 2.1 s long');
      expect(SoundCatalog.describe(profile(900, 2, 1, 0))).assertEqual('Mid-pitched, continuous tone, about 2.1 s long');
      expect(SoundCatalog.describe(profile(500, 0, 1, 0))).assertEqual('Low-pitched, single beep, about 2.1 s long');
      expect(SoundCatalog.describe(profile(2000, 1, 2, 0.5))).assertEqual('High-pitched, one beep every 2 seconds, about 2.1 s long');
      expect(SoundCatalog.describe(profile(2000, 1, 2, 0.8))).assertEqual('High-pitched, one beep every second, about 2.1 s long');
      expect(SoundCatalog.describe(profile(0, 0, 0, 0))).assertEqual('Still learning this sound');
    });
  });
}
