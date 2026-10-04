// Knocks and loud sounds are level-only detections, so the phone's own noise would trigger them: its vibration motor
// and the taps of the user's finger on the screen. The guard knows for how long that noise lasts.
export class SelfNoiseGuard {
  static readonly TOUCH_WINDOW_MS: number = 1200; // a tap is heard for a moment after the finger lands
  static readonly VIBRATION_TAIL_MS: number = 1500; // the motor's sound lingers after it stops

  private vibratingUntilMs: number = 0;
  private lastTouchMs: number = -1e15;

  // The phone is going to vibrate for `durationMs` starting now.
  noteVibration(nowMs: number, durationMs: number): void {
    this.vibratingUntilMs = Math.max(this.vibratingUntilMs, nowMs + durationMs + SelfNoiseGuard.VIBRATION_TAIL_MS);
  }

  noteTouch(nowMs: number): void {
    this.lastTouchMs = nowMs;
  }

  isNoisy(nowMs: number): boolean {
    return nowMs < this.vibratingUntilMs || nowMs - this.lastTouchMs < SelfNoiseGuard.TOUCH_WINDOW_MS;
  }
}
