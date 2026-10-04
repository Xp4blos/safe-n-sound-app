import { abilityAccessCtrl, common, Permissions } from '@kit.AbilityKit';
import { audio } from '@kit.AudioKit';
import { fileIo as fs } from '@kit.CoreFileKit';
import { hilog } from '@kit.PerformanceAnalysisKit';
import { AppSettings } from '../model/AppSettings';
import { LearnOutcome } from '../model/SoundModels';
import { AlertService } from './AlertService';
import { NativeEngine } from './NativeEngine';
import { PipelineListener, SoundPipeline } from './SoundPipeline';
import { SoundStore } from './SoundStore';

const LOG_DOMAIN: number = 0x3201;
const TAG: string = 'AudioService';
const SAMPLE_RATE: number = 16000;
const MIC_PERMISSION: Permissions = 'ohos.permission.MICROPHONE';

// Developer switch: write the captured audio to <app files>/debug_capture.pcm (16 kHz mono 16-bit) so real recordings
// can be replayed through the engine on a PC. Off by default; the audio never leaves the phone otherwise.
const DEBUG_CAPTURE_AUDIO: boolean = false;

export type ListenState = 'idle' | 'starting' | 'listening' | 'permission_denied' | 'error';

// Owns the microphone: permission, capture, feeding the native engine, and handing events to the pipeline.
// Foreground only; the ability stops it when the app leaves the foreground.
export class AudioService {
  onLevel: (db: number) => void = (db: number): void => {};
  onSpectrum: (bands: number[]) => void = (bands: number[]): void => {};
  onState: (state: ListenState) => void = (state: ListenState): void => {};
  // Every microphone buffer, for the speech recogniser (16 kHz mono 16-bit).
  onPcm: (pcm: ArrayBuffer) => void = (pcm: ArrayBuffer): void => {};
  // Whether listening continues when the app leaves the screen (the background task is held).
  keepInBackground: () => boolean = (): boolean => false;

  private store: SoundStore;
  private alerts: AlertService;
  private listener: PipelineListener;
  private settings: AppSettings;
  private state: ListenState = 'idle';
  private capturer: audio.AudioCapturer | undefined;
  private engine: NativeEngine | undefined;
  private pipeline: SoundPipeline | undefined;
  private debugFd: number = -1;
  private session: number = 0; // bumped by every start and stop so a start can tell it was cancelled

  // Recording a take for teaching: audio is collected here instead of being analysed for events.
  private takeResolve: ((buffer: ArrayBuffer) => void) | undefined;
  private takeReject: ((reason: Error) => void) | undefined;
  private takeChunks: ArrayBuffer[] = [];
  private takeBytes: number = 0;
  private takeWantedBytes: number = 0;

  constructor(store: SoundStore, alerts: AlertService, listener: PipelineListener, settings: AppSettings) {
    this.store = store;
    this.alerts = alerts;
    this.listener = listener;
    this.settings = settings;
  }

  currentState(): ListenState {
    return this.state;
  }

  async start(context: common.UIAbilityContext): Promise<void> {
    if (this.state === 'starting' || this.state === 'listening') {
      return;
    }
    const session = ++this.session;
    this.setState('starting');
    const granted = await this.requestMicrophone(context);
    if (session !== this.session) {
      return; // stopped while the permission dialog was open; stop() already reset the state
    }
    if (!granted) {
      this.setState('permission_denied');
      return;
    }
    try {
      const engine = NativeEngine.create(SAMPLE_RATE);
      this.engine = engine;
      for (const t of this.store.catalog().learnedTemplates()) {
        if (!engine.addSound(t.template)) {
          hilog.warn(LOG_DOMAIN, TAG, 'Stored sound %{public}s could not be loaded', t.label);
        }
      }
      this.pipeline = new SoundPipeline(this.store.catalog(), engine, this.alerts, this.store, this.listener,
        this.settings);
      if (DEBUG_CAPTURE_AUDIO) {
        this.debugFd = fs.openSync(context.filesDir + '/debug_capture.pcm',
          fs.OpenMode.CREATE | fs.OpenMode.READ_WRITE | fs.OpenMode.TRUNC).fd;
      }
      const options: audio.AudioCapturerOptions = {
        streamInfo: {
          samplingRate: audio.AudioSamplingRate.SAMPLE_RATE_16000,
          channels: audio.AudioChannel.CHANNEL_1,
          sampleFormat: audio.AudioSampleFormat.SAMPLE_FORMAT_S16LE,
          encodingType: audio.AudioEncodingType.ENCODING_TYPE_RAW
        },
        capturerInfo: { source: audio.SourceType.SOURCE_TYPE_MIC, capturerFlags: 0 }
      };
      const capturer = await audio.createAudioCapturer(options);
      if (session !== this.session) {
        await capturer.release();
        this.engine?.destroy();
        this.engine = undefined;
        this.pipeline = undefined;
        return;
      }
      this.capturer = capturer;
      capturer.on('readData', (buffer: ArrayBuffer): void => {
        this.onBuffer(buffer);
      });
      capturer.on('stateChange', (s: audio.AudioState): void => {
        // Ignore events from a capturer we already released ourselves (stop() makes it report STOPPED).
        if (this.capturer !== capturer) {
          return;
        }
        if (this.state === 'listening' &&
          (s === audio.AudioState.STATE_STOPPED || s === audio.AudioState.STATE_RELEASED)) {
          hilog.warn(LOG_DOMAIN, TAG, 'Capturer stopped unexpectedly (state %{public}d)', s);
          this.fail();
        }
      });
      await capturer.start();
      if (session !== this.session) {
        await this.release();
        return;
      }
      this.setState('listening');
    } catch (e) {
      hilog.error(LOG_DOMAIN, TAG, 'Failed to start listening: %{public}s', JSON.stringify(e));
      this.fail();
    }
  }

  // For the app lifecycle: end a listening session, and cancel a start that is still waiting on a permission
  // dialog or the capturer, so the microphone never ends up running in the background.
  async onBackground(): Promise<void> {
    if (this.keepInBackground()) {
      return; // the user chose to keep listening in the background
    }
    if (this.state === 'starting' || this.state === 'listening') {
      await this.stop();
    }
  }

  // Idempotent; releases the microphone and the native engine.
  async stop(): Promise<void> {
    this.session++;
    await this.release();
    if (this.state !== 'permission_denied' && this.state !== 'error') {
      this.setState('idle');
    }
  }

  // ---- teaching -------------------------------------------------------------------------------------

  // Records `seconds` of raw audio (kept in memory only) for the teach flow. Events are not analysed meanwhile.
  recordTake(seconds: number): Promise<ArrayBuffer> {
    return new Promise<ArrayBuffer>((resolve: (buffer: ArrayBuffer) => void, reject: (reason: Error) => void): void => {
      if (this.state !== 'listening' || this.takeResolve !== undefined) {
        reject(new Error('The microphone is not listening.'));
        return;
      }
      this.takeChunks = [];
      this.takeBytes = 0;
      this.takeWantedBytes = Math.round(seconds * SAMPLE_RATE) * 2;
      this.takeResolve = resolve;
      this.takeReject = reject;
    });
  }

  checkTake(take: ArrayBuffer): LearnOutcome | undefined {
    return this.engine?.checkTake(take);
  }

  trainSound(label: string, takes: ArrayBuffer[]): LearnOutcome | undefined {
    return this.engine?.trainSound(label, takes);
  }

  // The user deleted a sound: the engine must stop recognising it.
  forgetSound(label: string): void {
    this.engine?.removeSound(label);
  }

  // ---- internals ------------------------------------------------------------------------------------

  private fail(): void {
    this.release();
    this.setState('error');
  }

  private async release(): Promise<void> {
    const capturer = this.capturer;
    const engine = this.engine;
    this.capturer = undefined;
    this.engine = undefined;
    this.pipeline = undefined;
    this.abortTake('Listening stopped.');
    if (this.debugFd >= 0) {
      fs.closeSync(this.debugFd);
      this.debugFd = -1;
    }
    if (capturer !== undefined) {
      try {
        capturer.off('readData');
        await capturer.stop();
      } catch (e) {
        hilog.warn(LOG_DOMAIN, TAG, 'Stopping capturer: %{public}s', JSON.stringify(e));
      }
      try {
        await capturer.release();
      } catch (e) {
        hilog.warn(LOG_DOMAIN, TAG, 'Releasing capturer: %{public}s', JSON.stringify(e));
      }
    }
    if (engine !== undefined) {
      engine.destroy();
    }
  }

  private abortTake(reason: string): void {
    const reject = this.takeReject;
    this.takeResolve = undefined;
    this.takeReject = undefined;
    this.takeChunks = [];
    if (reject !== undefined) {
      reject(new Error(reason));
    }
  }

  private collectTake(buffer: ArrayBuffer): void {
    this.takeChunks.push(buffer.slice(0));
    this.takeBytes += buffer.byteLength;
    if (this.takeBytes < this.takeWantedBytes) {
      return;
    }
    const merged = new Uint8Array(this.takeBytes);
    let offset = 0;
    for (const chunk of this.takeChunks) {
      merged.set(new Uint8Array(chunk), offset);
      offset += chunk.byteLength;
    }
    const resolve = this.takeResolve;
    this.takeResolve = undefined;
    this.takeReject = undefined;
    this.takeChunks = [];
    if (resolve !== undefined) {
      resolve(merged.buffer as ArrayBuffer);
    }
  }

  private onBuffer(buffer: ArrayBuffer): void {
    const engine = this.engine;
    const pipeline = this.pipeline;
    if (engine === undefined || pipeline === undefined) {
      return;
    }
    try {
      if (this.debugFd >= 0) {
        fs.writeSync(this.debugFd, buffer);
      }
      const result = engine.process(buffer);
      this.onLevel(result.levelDb);
      this.onSpectrum(result.bands);
      this.onPcm(buffer);
      if (this.takeResolve !== undefined) {
        this.collectTake(buffer); // teaching: no events are analysed meanwhile
        return;
      }
      pipeline.handle(result.events, Date.now());
    } catch (e) {
      hilog.error(LOG_DOMAIN, TAG, 'Processing audio failed: %{public}s', JSON.stringify(e));
    }
  }

  private async requestMicrophone(context: common.UIAbilityContext): Promise<boolean> {
    try {
      const atManager = abilityAccessCtrl.createAtManager();
      const result = await atManager.requestPermissionsFromUser(context, [MIC_PERMISSION]);
      if (result.authResults.length > 0 && result.authResults[0] === abilityAccessCtrl.GrantStatus.PERMISSION_GRANTED) {
        return true;
      }
      // After an earlier denial the system shows no dialog; send the user to Settings instead.
      const shown = result.dialogShownResults;
      if (shown !== undefined && shown.length > 0 && !shown[0]) {
        const statuses = await atManager.requestPermissionOnSetting(context, [MIC_PERMISSION]);
        return statuses.length > 0 && statuses[0] === abilityAccessCtrl.GrantStatus.PERMISSION_GRANTED;
      }
      return false;
    } catch (e) {
      hilog.error(LOG_DOMAIN, TAG, 'Permission request failed: %{public}s', JSON.stringify(e));
      return false;
    }
  }

  private setState(state: ListenState): void {
    hilog.info(LOG_DOMAIN, TAG, 'State %{public}s -> %{public}s', this.state, state);
    this.state = state;
    this.onState(state);
  }
}
