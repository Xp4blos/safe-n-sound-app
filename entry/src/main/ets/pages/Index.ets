import { common } from '@kit.AbilityKit';
import { hilog } from '@kit.PerformanceAnalysisKit';
import { CaptionsTab } from '../components/CaptionsTab';
import { HistoryTab } from '../components/HistoryTab';
import { ListenTab } from '../components/ListenTab';
import { MySoundsTab } from '../components/MySoundsTab';
import { PromptDialog } from '../components/PromptDialog';
import { SettingsTab } from '../components/SettingsTab';
import { SoundDetailDialog } from '../components/SoundDetailDialog';
import { TakeStatus, TeachDialog } from '../components/TeachDialog';
import { AppSettings } from '../model/AppSettings';
import { SoundClass, SoundClasses } from '../model/SoundClasses';
import { SoundProfile, SoundRecord } from '../model/SoundModels';
import { KeywordRule, SpeechCaption } from '../model/SpeechModels';
import { SpectrumHistory } from '../model/SpectrumHistory';
import { AlertService } from '../services/AlertService';
import { AudioService, ListenState } from '../services/AudioService';
import { PipelineListener } from '../services/SoundPipeline';
import { SettingsStore } from '../services/SettingsStore';
import { SpeechService } from '../services/SpeechService';
import { ServiceHub } from '../services/ServiceHub';
import { SoundStore } from '../services/SoundStore';

const LOG_DOMAIN: number = 0x3201;
const TAG: string = 'IndexPage';
const TAKE_SECONDS: number = 6;
const ALERT_CARD_MS: number = 60000;
const UI_REFRESH_MS: number = 120;

function copyRecord(r: SoundRecord): SoundRecord {
  const copy: SoundRecord = {
    id: r.id, label: r.label, name: r.name, alertsEnabled: r.alertsEnabled, timesHeard: r.timesHeard,
    firstSeenMs: r.firstSeenMs, lastSeenMs: r.lastSeenMs, lastAlertMs: r.lastAlertMs, lastPromptMs: r.lastPromptMs,
    promptNever: r.promptNever, recentMs: r.recentMs.slice(), profile: r.profile, template: r.template
  };
  return copy;
}

// Forwards the pipeline's notifications to the page.
class UiBridge implements PipelineListener {
  heard: (record: SoundRecord, isNew: boolean) => void = (record: SoundRecord, isNew: boolean): void => {};
  alert: (record: SoundRecord, timeMs: number) => void = (record: SoundRecord, timeMs: number): void => {};
  prompt: (record: SoundRecord) => void = (record: SoundRecord): void => {};
  classAlert: (record: SoundRecord, timeMs: number) => void = (record: SoundRecord, timeMs: number): void => {};
  detection: (timeMs: number) => void = (timeMs: number): void => {};
  changed: () => void = (): void => {};

  onSoundHeard(record: SoundRecord, isNew: boolean): void {
    this.heard(record, isNew);
  }

  onAlert(record: SoundRecord, timeMs: number): void {
    this.alert(record, timeMs);
  }

  onPrompt(record: SoundRecord): void {
    this.prompt(record);
  }

  onClassAlert(record: SoundRecord, timeMs: number): void {
    this.classAlert(record, timeMs);
  }

  onDetection(timeMs: number): void {
    this.detection(timeMs);
  }

  onChanged(): void {
    this.changed();
  }
}

@Entry
@Component
struct Index {
  @State currentTab: number = 0;
  @State listenState: ListenState = 'idle';
  @State columns: number[] = new SpectrumHistory().columns();
  @State marks: number[] = new SpectrumHistory().marks();
  @State level: number = 0;
  @State notificationsOn: boolean = true;
  @State lastSound: SoundRecord | undefined = undefined;
  @State lastSoundIsNew: boolean = false;
  @State alertRecord: SoundRecord | undefined = undefined;
  @State sounds: SoundRecord[] = [];
  @State alertHeading: string = '';
  @State captions: SpeechCaption[] = [];
  @State liveCaption: SpeechCaption | undefined = undefined;
  @State lastKeyword: string = '';
  @State rules: KeywordRule[] = [];
  @State speechActive: boolean = false;
  @State speechError: string = '';
  @State settingKeys: string[] = [];
  private spectrum: SpectrumHistory = new SpectrumHistory();
  private lastUiMs: number = 0;
  private alertToken: number = 0;
  private alerts: AlertService = new AlertService();
  private store: SoundStore | undefined = undefined;
  private audioService: AudioService | undefined = undefined;
  private settingsStore: SettingsStore | undefined = undefined;
  private speech: SpeechService | undefined = undefined;
  private takeBuffers: ArrayBuffer[] = [];
  private teachOpen: boolean = false;
  private pendingPromptId: number = -1; // a naming prompt that came up while the Teach dialog was open

  aboutToAppear(): void {
    this.setUp();
  }

  private context(): common.UIAbilityContext {
    return this.getUIContext().getHostContext() as common.UIAbilityContext;
  }

  private async setUp(): Promise<void> {
    const store = await SoundStore.open(this.context());
    this.store = store;
    this.refreshSounds();
    const settingsStore = await SettingsStore.open(this.context());
    this.settingsStore = settingsStore;
    const settings = settingsStore.settings;
    this.refreshSettingKeys();
    const speech = new SpeechService(settingsStore, this.alerts);
    this.speech = speech;
    speech.onChanged = (): void => {
      this.syncSpeech();
    };
    speech.onKeyword = (label: string, text: string, timeMs: number): void => {
      hilog.info(LOG_DOMAIN, TAG, 'KEYWORD %{public}s', label);
      this.showKeywordCard(label, timeMs);
    };
    speech.ensureReady();
    this.syncSpeech();

    const bridge = new UiBridge();
    bridge.heard = (record: SoundRecord, isNew: boolean): void => {
      this.lastSound = copyRecord(record);
      this.lastSoundIsNew = isNew;
      this.refreshSounds();
    };
    bridge.alert = (record: SoundRecord, timeMs: number): void => {
      hilog.info(LOG_DOMAIN, TAG, 'ALERT card id=%{public}d name=%{public}s', record.id, record.name);
      this.showAlertCard(record, '');
    };
    bridge.classAlert = (record: SoundRecord, timeMs: number): void => {
      hilog.info(LOG_DOMAIN, TAG, 'ALERT class=%{public}s', record.label);
      this.showAlertCard(record, '');
    };
    bridge.prompt = (record: SoundRecord): void => {
      this.openPrompt(record);
    };
    bridge.detection = (timeMs: number): void => {
      this.spectrum.markDetection(timeMs);
    };
    bridge.changed = (): void => {
      this.refreshSounds();
    };

    const service = new AudioService(store, this.alerts, bridge, settings);
    service.keepInBackground = (): boolean => settings.keepListeningInBackground && this.alerts.isBackgroundActive();
    service.onPcm = (pcm: ArrayBuffer): void => {
      speech.feed(pcm);
    };
    service.onSpectrum = (bands: number[]): void => {
      const now = Date.now();
      this.spectrum.push(bands, now);
      if (now - this.lastUiMs >= UI_REFRESH_MS) {
        this.lastUiMs = now;
        this.columns = this.spectrum.columns();
        this.marks = this.spectrum.marks();
        this.level = this.spectrum.level();
      }
    };
    service.onState = (state: ListenState): void => {
      this.listenState = state;
      this.applyListeningState(state);
      if (state !== 'listening') {
        this.spectrum.reset();
        this.columns = this.spectrum.columns();
        this.marks = this.spectrum.marks();
        this.level = 0;
      }
    };
    this.audioService = service;
    ServiceHub.audio = service;
    ServiceHub.onTouch = (): void => {
      this.alerts.noteTouch(Date.now());
    };
    ServiceHub.onBackground = (): void => {
      speech.stop(); // the system recogniser disturbs the microphone while the app is not on screen
    };
    ServiceHub.onForeground = (): void => {
      if (this.listenState === 'listening' && settings.speechEnabled) {
        speech.start();
      }
    };
    if (settings.autoStart) {
      this.toggleListening();
    }
  }

  // Card on the Listen tab for a sound that just alerted; it stays for a minute or until dismissed.
  private showAlertCard(record: SoundRecord, heading: string): void {
    this.alertHeading = heading;
    this.alertRecord = copyRecord(record);
    const token = ++this.alertToken;
    setTimeout((): void => {
      if (token === this.alertToken) {
        this.alertRecord = undefined;
      }
    }, ALERT_CARD_MS);
  }

  private showKeywordCard(label: string, timeMs: number): void {
    const profile: SoundProfile = {
      dominantHz: 0, durationSec: 0, beepCount: 0, beepsPerSec: 0, repetition: 0, modulation: 0,
      envelope: [0, 0, 0, 0, 0, 0, 0, 0]
    };
    const record: SoundRecord = {
      id: -1, label: 'keyword', name: label, alertsEnabled: true, timesHeard: 1, firstSeenMs: timeMs, lastSeenMs: timeMs,
      lastAlertMs: timeMs, lastPromptMs: 0, promptNever: false, recentMs: [timeMs], profile: profile, template: ''
    };
    this.showAlertCard(record, 'Heard "' + label + '"');
  }

  // Background task and speech recogniser follow the microphone.
  private applyListeningState(state: ListenState): void {
    const settings = this.settingsStore?.settings;
    if (settings === undefined) {
      return;
    }
    if (state === 'listening') {
      if (settings.keepListeningInBackground) {
        this.alerts.startBackground(this.context());
      }
      if (settings.speechEnabled) {
        this.speech?.start();
      }
    } else if (state === 'idle' || state === 'error' || state === 'permission_denied') {
      this.speech?.stop();
      this.alerts.stopBackground(this.context());
    }
  }

  private syncSpeech(): void {
    const speech = this.speech;
    if (speech === undefined) {
      return;
    }
    this.captions = speech.captions().slice();
    this.liveCaption = speech.live();
    this.lastKeyword = speech.lastAlert();
    this.rules = speech.rules.slice();
    this.speechActive = speech.active;
    this.speechError = speech.error;
  }

  private refreshSettingKeys(): void {
    const settings = this.settingsStore?.settings;
    if (settings === undefined) {
      return;
    }
    const keys: string[] = [];
    if (settings.vibrate) {
      keys.push('vibrate');
    }
    if (settings.notify) {
      keys.push('notify');
    }
    if (settings.keepListeningInBackground) {
      keys.push('background');
    }
    if (settings.autoStart) {
      keys.push('autoStart');
    }
    if (settings.speechEnabled) {
      keys.push('speech');
    }
    if (settings.speechOnPartial) {
      keys.push('partial');
    }
    for (const cls of SoundClasses.all()) {
      if (settings.isClassOn(cls.key)) {
        keys.push('class:' + cls.key);
      }
    }
    this.settingKeys = keys;
  }

  private async changeSetting(key: string, on: boolean): Promise<void> {
    const store = this.settingsStore;
    if (store === undefined) {
      return;
    }
    const settings: AppSettings = store.settings;
    if (key.startsWith('class:')) {
      settings.setClass(key.substring(SoundClasses.PREFIX.length), on);
    } else if (key === 'vibrate') {
      settings.vibrate = on;
    } else if (key === 'notify') {
      settings.notify = on;
    } else if (key === 'background') {
      settings.keepListeningInBackground = on;
      if (this.listenState === 'listening') {
        if (on) {
          this.alerts.startBackground(this.context());
        } else {
          this.alerts.stopBackground(this.context());
        }
      }
    } else if (key === 'autoStart') {
      settings.autoStart = on;
    } else if (key === 'speech') {
      settings.speechEnabled = on;
      if (this.listenState === 'listening') {
        if (on) {
          this.speech?.start();
        } else {
          this.speech?.stop();
        }
      }
    } else if (key === 'partial') {
      settings.speechOnPartial = on;
      this.speech?.rebuild();
    }
    this.refreshSettingKeys();
    await store.saveSettings();
  }

  // Newest first; copies so the UI sees a changed array even when records were updated in place.
  private refreshSounds(): void {
    const store = this.store;
    if (store === undefined) {
      return;
    }
    const copies: SoundRecord[] = store.catalog().records().map((r: SoundRecord): SoundRecord => copyRecord(r));
    this.sounds = copies.sort((a: SoundRecord, b: SoundRecord): number => b.lastSeenMs - a.lastSeenMs);
  }

  private namedSounds(): SoundRecord[] {
    return this.sounds.filter((r: SoundRecord): boolean => r.name !== '' && !SoundClasses.isBuiltInLabel(r.label));
  }

  // ---- listening --------------------------------------------------------------------------------

  private async toggleListening(): Promise<void> {
    const service = this.audioService;
    if (service === undefined) {
      return;
    }
    if (this.listenState === 'listening') {
      await service.stop();
      return;
    }
    this.notificationsOn = await this.alerts.ensureNotificationPermission(this.context());
    await service.start(this.context());
  }

  // ---- naming, details, deleting ----------------------------------------------------------------

  private openDetail(id: number): void {
    hilog.info(LOG_DOMAIN, TAG, 'DIALOG detail open id=%{public}d', id);
    const store = this.store;
    const record = store === undefined ? undefined : store.catalog().find(id);
    if (record === undefined) {
      return;
    }
    const snapshot = copyRecord(record);
    let dialog: CustomDialogController | undefined = undefined;
    dialog = new CustomDialogController({
      builder: SoundDetailDialog({
        record: snapshot,
        onClose: (): void => {
          dialog?.close();
        },
        onSave: (soundId: number, name: string, alertsEnabled: boolean): void => {
          this.saveDetail(soundId, name, alertsEnabled);
        },
        onDelete: (soundId: number): void => {
          this.deleteSound(soundId);
        }
      }),
      alignment: DialogAlignment.Center,
      customStyle: true
    });
    dialog.open();
  }

  private async saveDetail(id: number, name: string, alertsEnabled: boolean): Promise<void> {
    hilog.info(LOG_DOMAIN, TAG, 'DIALOG detail save id=%{public}d name=%{public}s', id, name);
    const store = this.store;
    if (store === undefined) {
      return;
    }
    store.catalog().rename(id, name);
    store.catalog().setAlerts(id, alertsEnabled);
    await store.save();
    this.refreshSounds();
  }

  private async setAlerts(id: number, enabled: boolean): Promise<void> {
    const store = this.store;
    if (store === undefined) {
      return;
    }
    store.catalog().setAlerts(id, enabled);
    await store.save();
    this.refreshSounds();
  }

  private async deleteSound(id: number): Promise<void> {
    const store = this.store;
    if (store === undefined) {
      return;
    }
    const removed = store.catalog().remove(id);
    if (removed !== undefined) {
      this.audioService?.forgetSound(removed.label);
      if (this.lastSound !== undefined && this.lastSound.id === id) {
        this.lastSound = undefined;
      }
      if (this.alertRecord !== undefined && this.alertRecord.id === id) {
        this.alertRecord = undefined;
      }
    }
    await store.save();
    this.refreshSounds();
  }

  // "I've heard this sound before. Do you want to name it?"
  private openPrompt(record: SoundRecord): void {
    if (this.teachOpen) {
      // Two dialogs on top of each other block saving the taught sound; ask again once Teach is closed.
      hilog.info(LOG_DOMAIN, TAG, 'DIALOG prompt deferred id=%{public}d (Teach is open)', record.id);
      this.pendingPromptId = record.id;
      return;
    }
    hilog.info(LOG_DOMAIN, TAG, 'DIALOG prompt open id=%{public}d name=%{public}s', record.id, record.name);
    const id = record.id;
    let dialog: CustomDialogController | undefined = undefined;
    dialog = new CustomDialogController({
      builder: PromptDialog({
        onClose: (): void => {
          dialog?.close();
        },
        onNameIt: (): void => {
          this.openDetail(id);
        },
        onNotNow: (): void => {},
        onNever: (): void => {
          this.neverAsk(id);
        }
      }),
      alignment: DialogAlignment.Center,
      customStyle: true
    });
    dialog.open();
  }

  private openPendingPrompt(): void {
    const id = this.pendingPromptId;
    this.pendingPromptId = -1;
    const store = this.store;
    if (id < 0 || store === undefined) {
      return;
    }
    const record = store.catalog().find(id);
    if (record !== undefined && record.name === '' && !record.promptNever) {
      this.openPrompt(record);
    }
  }

  private async neverAsk(id: number): Promise<void> {
    const store = this.store;
    if (store === undefined) {
      return;
    }
    store.catalog().setPromptNever(id);
    await store.save();
  }

  // ---- teaching ---------------------------------------------------------------------------------

  private async openTeach(): Promise<void> {
    const service = this.audioService;
    if (service === undefined) {
      return;
    }
    if (this.listenState !== 'listening') {
      await this.toggleListening();
    }
    if (service.currentState() !== 'listening') {
      return;
    }
    this.takeBuffers = [];
    this.teachOpen = true;
    let dialog: CustomDialogController | undefined = undefined;
    dialog = new CustomDialogController({
      builder: TeachDialog({
        onClose: (): void => {
          dialog?.close();
          this.teachOpen = false;
          this.openPendingPrompt();
        },
        recordTake: (): Promise<TakeStatus> => this.recordTake(),
        saveSound: (name: string): Promise<string> => this.saveTaught(name),
        discardTakes: (): void => {
          this.takeBuffers = [];
        }
      }),
      alignment: DialogAlignment.Center,
      customStyle: true,
      autoCancel: false
    });
    dialog.open();
  }

  private async recordTake(): Promise<TakeStatus> {
    const service = this.audioService;
    if (service === undefined) {
      const none: TakeStatus = { ok: false, message: 'The microphone is not ready.' };
      return none;
    }
    try {
      const take = await service.recordTake(TAKE_SECONDS);
      const outcome = service.checkTake(take);
      if (outcome === undefined || !outcome.ok) {
        const reason = outcome === undefined ? 'The microphone is not ready.' : outcome.message.replace(/^recording \d+: /, '');
        const bad: TakeStatus = { ok: false, message: reason };
        return bad;
      }
      this.takeBuffers.push(take);
      const good: TakeStatus = { ok: true, message: '' };
      return good;
    } catch (e) {
      const failed: TakeStatus = { ok: false, message: 'Recording was interrupted.' };
      return failed;
    }
  }

  // Resolves to '' on success, otherwise the reason it failed.
  private async saveTaught(name: string): Promise<string> {
    const service = this.audioService;
    const store = this.store;
    if (service === undefined || store === undefined) {
      return 'The microphone is not ready.';
    }
    const label = store.catalog().reserveLabel();
    const outcome = service.trainSound(label, this.takeBuffers);
    if (outcome === undefined) {
      return 'The microphone is not ready.';
    }
    if (!outcome.ok) {
      return outcome.message.includes('match each other') ? 'The takes differ too much. Please record them again.' :
      outcome.message;
    }
    store.catalog().addTaught(label, name, outcome.template, outcome.profile, Date.now());
    this.takeBuffers = [];
    await store.save();
    this.refreshSounds();
    return '';
  }

  // Bottom bar item: icon above a label, highlighted for the current tab.
  @Builder
  tabItem(title: Resource, icon: Resource, index: number) {
    Column({ space: 3 }) {
      SymbolGlyph(icon)
        .fontSize(24)
        .fontColor([this.currentTab === index ? $r('app.color.accent') : $r('app.color.muted')])
      Text(title)
        .fontSize(12)
        .fontWeight(this.currentTab === index ? FontWeight.Medium : FontWeight.Normal)
        .fontColor(this.currentTab === index ? $r('app.color.accent') : $r('app.color.muted'))
    }
    .width('100%')
    .padding({ top: 8, bottom: 8 })
  }

  build() {
    Tabs({ barPosition: BarPosition.End, index: this.currentTab }) {
      TabContent() {
        ListenTab({
          state: this.listenState,
          columns: this.columns,
          marks: this.marks,
          level: this.level,
          notificationsOn: this.notificationsOn,
          lastSound: this.lastSound,
          lastSoundIsNew: this.lastSoundIsNew,
          alertRecord: this.alertRecord,
          alertHeading: this.alertHeading,
          onToggle: (): void => {
            this.toggleListening();
          },
          onTeach: (): void => {
            this.openTeach();
          },
          onDismissAlert: (): void => {
            this.alertRecord = undefined;
          }
        })
      }
      .tabBar(this.tabItem($r('app.string.tab_listen'), $r('sys.symbol.mic'), 0))

      TabContent() {
        HistoryTab({
          sounds: this.sounds,
          onOpen: (id: number): void => {
            this.openDetail(id);
          }
        })
      }
      .tabBar(this.tabItem($r('app.string.tab_history'), $r('sys.symbol.clock'), 1))

      TabContent() {
        CaptionsTab({
          captions: this.captions,
          live: this.liveCaption,
          lastAlert: this.lastKeyword,
          rules: this.rules,
          active: this.speechActive,
          listening: this.listenState === 'listening',
          speechOn: this.settingKeys.includes('speech'),
          error: this.speechError,
          onSimulate: (text: string): void => {
            this.speech?.simulate(text);
          },
          onClear: (): void => {
            this.speech?.clear();
          },
          onAddRule: (phrase: string, preset: string): void => {
            this.speech?.addRule(phrase, preset);
          },
          onRemoveRule: (phrase: string): void => {
            this.speech?.removeRule(phrase);
          }
        })
      }
      .tabBar(this.tabItem($r('app.string.tab_captions'), $r('sys.symbol.ellipsis_message'), 2))

      TabContent() {
        MySoundsTab({
          sounds: this.namedSounds(),
          onOpen: (id: number): void => {
            this.openDetail(id);
          },
          onToggleAlerts: (id: number, enabled: boolean): void => {
            this.setAlerts(id, enabled);
          }
        })
      }
      .tabBar(this.tabItem($r('app.string.tab_mysounds'), $r('sys.symbol.bell'), 3))

      TabContent() {
        SettingsTab({
          onKeys: this.settingKeys,
          onToggle: (key: string, on: boolean): void => {
            this.changeSetting(key, on);
          }
        })
      }
      .tabBar(this.tabItem($r('app.string.tab_settings'), $r('sys.symbol.gearshape'), 4))
    }
    .width('100%')
    .height('100%')
    .barHeight(68)
    .barBackgroundColor($r('app.color.surface'))
    .backgroundColor($r('app.color.page_bg'))
    .onChange((index: number): void => {
      this.currentTab = index;
    })
    .onTouch((): void => {
      this.alerts.noteTouch(Date.now()); // a tap on the screen is heard as a knock
    })
  }
}
