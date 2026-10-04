import { describe, it, expect } from '@ohos/hypium';
import { SelfNoiseGuard } from '../main/ets/model/SelfNoiseGuard';

export default function selfNoiseGuardTest() {
  describe('selfNoiseGuard', () => {
    it('is_quiet_until_something_happens', 0, () => {
      expect(new SelfNoiseGuard().isNoisy(1000000)).assertFalse();
    });

    it('a_touch_makes_noise_for_a_short_while', 0, () => {
      const g = new SelfNoiseGuard();
      g.noteTouch(1000000);
      expect(g.isNoisy(1000000 + 500)).assertTrue();
      expect(g.isNoisy(1000000 + SelfNoiseGuard.TOUCH_WINDOW_MS - 1)).assertTrue();
      expect(g.isNoisy(1000000 + SelfNoiseGuard.TOUCH_WINDOW_MS + 1)).assertFalse();
    });

    it('vibration_makes_noise_for_its_length_plus_a_tail', 0, () => {
      const g = new SelfNoiseGuard();
      g.noteVibration(1000000, 800);
      expect(g.isNoisy(1000000 + 800)).assertTrue();
      expect(g.isNoisy(1000000 + 800 + SelfNoiseGuard.VIBRATION_TAIL_MS - 1)).assertTrue();
      expect(g.isNoisy(1000000 + 800 + SelfNoiseGuard.VIBRATION_TAIL_MS + 1)).assertFalse();
    });

    it('a_shorter_vibration_never_shortens_a_longer_one', 0, () => {
      const g = new SelfNoiseGuard();
      g.noteVibration(1000000, 5000);
      g.noteVibration(1000100, 100);
      expect(g.isNoisy(1000000 + 5000)).assertTrue();
    });

    it('touch_and_vibration_combine', 0, () => {
      const g = new SelfNoiseGuard();
      g.noteVibration(1000000, 100);
      g.noteTouch(1000000 + 10000);
      expect(g.isNoisy(1000000 + 10000 + 100)).assertTrue();
    });
  });
}
