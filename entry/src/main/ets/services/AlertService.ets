import { vibrator } from '@kit.SensorServiceKit';
import { notificationManager } from '@kit.NotificationKit';
import { backgroundTaskManager } from '@kit.BackgroundTasksKit';
import { hilog } from '@kit.PerformanceAnalysisKit';
import { common, wantAgent, WantAgent } from '@kit.AbilityKit';
import { SelfNoiseGuard } from '../model/SelfNoiseGuard';
import { SoundClass } from '../model/SoundClasses';
import { KeywordAlertPort } from './CaptionBoard';
import { AlertPort } from './SoundPipeline';

const LOG_DOMAIN: number = 0x3201;
const TAG: string = 'AlertService';
const SHORT_PULSE_MS: number = 300;
const LONG_PULSE_MS: number = 500;
const LONG_PULSE_COUNT: number = 3;
const LONG_PULSE_GAP_MS: number = 250;

function twoDigits(n: number): string {
  return n < 10 ? '0' + n : n.toString();
}

function clock(ms: number): string {
  const d = new Date(ms);
  return twoDigits(d.getHours()) + ':' + twoDigits(d.getMinutes()) + ':' + twoDigits(d.getSeconds());
}

function sleep(ms: number): Promise<void> {
  return new Promise<void>((resolve: () => void): void => {
    setTimeout(resolve, ms);
  });
}

// Vibration, notifications and the background-run request. Everything degrades silently when unavailable: an alert
// problem must never interrupt listening.
export class AlertService implements AlertPort, KeywordAlertPort {
  private nextNotificationId: number = 1000;
  private noise: SelfNoiseGuard = new SelfNoiseGuard();
  private vibrationToken: number = 0;
  private keepAlive: boolean = false;

  // Resolves to whether notifications may be shown, asking the user once if they have not decided yet.
  async ensureNotificationPermission(context: common.UIAbilityContext): Promise<boolean> {
    try {
      if (await notificationManager.isNotificationEnabled()) {
        return true;
      }
      await notificationManager.requestEnableNotification(context);
      return await notificationManager.isNotificationEnabled();
    } catch (e) {
      hilog.warn(LOG_DOMAIN, TAG, 'Notifications not enabled: %{public}s', JSON.stringify(e));
      return false;
    }
  }

  // ---- background listening -----------------------------------------------------------------------

  // Asks the system to keep the app (and its microphone) alive when it is not on screen. Resolves to whether it did.
  async startBackground(context: common.UIAbilityContext): Promise<boolean> {
    if (this.keepAlive) {
      return true;
    }
    try {
      const info: wantAgent.WantAgentInfo = {
        wants: [{ bundleName: context.abilityInfo.bundleName, abilityName: context.abilityInfo.name }],
        actionType: wantAgent.OperationType.START_ABILITY,
        requestCode: 0,
        actionFlags: [wantAgent.WantAgentFlags.UPDATE_PRESENT_FLAG]
      };
      const agent: WantAgent = await wantAgent.getWantAgent(info);
      await backgroundTaskManager.startBackgroundRunning(context,
        backgroundTaskManager.BackgroundMode.AUDIO_RECORDING, agent);
      this.keepAlive = true;
      hilog.info(LOG_DOMAIN, TAG, 'Background listening started');
      return true;
    } catch (e) {
      this.keepAlive = false;
      hilog.warn(LOG_DOMAIN, TAG, 'Background listening not available: %{public}s', JSON.stringify(e));
      return false;
    }
  }

  async stopBackground(context: common.UIAbilityContext): Promise<void> {
    if (!this.keepAlive) {
      return;
    }
    this.keepAlive = false;
    try {
      await backgroundTaskManager.stopBackgroundRunning(context);
      hilog.info(LOG_DOMAIN, TAG, 'Background listening stopped');
    } catch (e) {
      hilog.warn(LOG_DOMAIN, TAG, 'Stopping background listening: %{public}s', JSON.stringify(e));
    }
  }

  isBackgroundActive(): boolean {
    return this.keepAlive;
  }

  // ---- vibration ----------------------------------------------------------------------------------

  // One short pulse for a sound the user has not named.
  vibrateUnknown(): void {
    this.playPattern([SHORT_PULSE_MS]);
  }

  // Three long pulses: distinct from the short one and long enough to notice in a pocket.
  vibrateNamed(): void {
    const pattern: number[] = [];
    for (let i = 0; i < LONG_PULSE_COUNT; i++) {
      if (i > 0) {
        pattern.push(LONG_PULSE_GAP_MS);
      }
      pattern.push(LONG_PULSE_MS);
    }
    this.playPattern(pattern);
  }

  vibrateClass(cls: SoundClass): void {
    this.playPattern(cls.vibration);
  }

  // pattern: on, off, on, ... in milliseconds. A new pattern replaces the one that is still playing.
  playPattern(pattern: number[]): void {
    if (pattern.length === 0) {
      return;
    }
    let total = 0;
    for (const ms of pattern) {
      total += ms;
    }
    this.noise.noteVibration(Date.now(), total);
    const mine = ++this.vibrationToken;
    this.runPattern(pattern, mine);
  }

  // The screen was touched: the tap is heard by the microphone as a knock for a moment.
  noteTouch(nowMs: number): void {
    this.noise.noteTouch(nowMs);
  }

  // True while the phone makes noise itself (it vibrates, or a finger just tapped the screen): that is not a knock or a
  // loud sound.
  isSelfNoise(nowMs: number): boolean {
    return this.noise.isNoisy(nowMs);
  }

  private async runPattern(pattern: number[], token: number): Promise<void> {
    for (let i = 0; i < pattern.length; i++) {
      if (token !== this.vibrationToken) {
        return;
      }
      if (i % 2 === 0 && pattern[i] > 0) {
        this.buzz(pattern[i]);
      }
      await sleep(pattern[i]);
    }
  }

  // Some usages are blocked in the background or in silent mode, others are not: try them in turn.
  private buzz(durationMs: number): void {
    const usages: vibrator.Usage[] = ['alarm', 'communication', 'notification', 'unknown'];
    const attempt = (index: number): void => {
      if (index >= usages.length) {
        return;
      }
      try {
        vibrator.startVibration({ type: 'time', duration: durationMs }, { id: 0, usage: usages[index] })
          .catch((e: Object): void => {
            hilog.warn(LOG_DOMAIN, TAG, 'Vibration (%{public}s) failed: %{public}s', usages[index], JSON.stringify(e));
            attempt(index + 1);
          });
      } catch (e) {
        hilog.warn(LOG_DOMAIN, TAG, 'Vibration (%{public}s) threw: %{public}s', usages[index], JSON.stringify(e));
        attempt(index + 1);
      }
    };
    attempt(0);
  }

  // ---- notifications ------------------------------------------------------------------------------

  notifyNamed(name: string, timeMs: number): void {
    this.publish(name + ' detected', 'at ' + clock(timeMs));
  }

  notifyClass(cls: SoundClass, timeMs: number): void {
    this.publish(cls.title + ' detected', 'at ' + clock(timeMs));
  }

  notifyKeyword(label: string, timeMs: number): void {
    this.publish('Keyword: ' + label, 'heard at ' + clock(timeMs));
  }

  notifyPrompt(): void {
    this.publish("I've heard this sound before", "Do you want to name it? Open Safe'n'Sound.");
  }

  private publish(title: string, text: string): void {
    notificationManager.publish({
      id: this.nextNotificationId++,
      notificationSlotType: notificationManager.SlotType.SOCIAL_COMMUNICATION,
      content: {
        notificationContentType: notificationManager.ContentType.NOTIFICATION_CONTENT_BASIC_TEXT,
        normal: { title: title, text: text }
      }
    }).catch((e: Object): void => {
      hilog.warn(LOG_DOMAIN, TAG, 'Could not publish notification: %{public}s', JSON.stringify(e));
    });
  }
}
