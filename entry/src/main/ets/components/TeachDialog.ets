import { ServiceHub } from '../services/ServiceHub';
export interface TakeStatus {
  ok: boolean;
  message: string; // why the take was not usable, when !ok
}

const TAKE_SECONDS: number = 6;
const MAX_TAKES: number = 3;
const MIN_GOOD_TAKES: number = 2;

// Teach a sound on purpose: record up to 3 takes (each is checked on the spot), type a name, save.
@CustomDialog
export struct TeachDialog {
  controller?: CustomDialogController; // required by the compiler; not reliably set for dialogs built in methods
  // Closes the dialog. The page passes this in explicitly because the controller above can be undefined.
  onClose: () => void = (): void => {};
  recordTake: () => Promise<TakeStatus> = (): Promise<TakeStatus> => {
    const s: TakeStatus = { ok: false, message: 'Unavailable.' };
    return Promise.resolve(s);
  };
  saveSound: (name: string) => Promise<string> = (name: string): Promise<string> => Promise.resolve('Unavailable.');
  discardTakes: () => void = (): void => {};
  @State statuses: TakeStatus[] = [];
  @State recording: boolean = false;
  @State secondsLeft: number = 0;
  @State name: string = '';
  @State error: string = '';
  @State saving: boolean = false;
  private timer: number = -1;

  private goodTakes(): number {
    return this.statuses.filter((s: TakeStatus): boolean => s.ok).length;
  }

  private async record(): Promise<void> {
    if (this.recording || this.statuses.length >= MAX_TAKES) {
      return;
    }
    this.error = '';
    this.recording = true;
    this.secondsLeft = TAKE_SECONDS;
    this.timer = setInterval((): void => {
      this.secondsLeft = Math.max(0, this.secondsLeft - 1);
    }, 1000);
    const status = await this.recordTake();
    clearInterval(this.timer);
    this.recording = false;
    this.statuses = [...this.statuses, status];
  }

  private async save(): Promise<void> {
    this.saving = true;
    const problem = await this.saveSound(this.name);
    this.saving = false;
    if (problem === '') {
      this.onClose();
      return;
    }
    this.error = problem;
    if (problem.includes('differ') || problem.includes('similar') || problem.includes('consisten')) {
      this.discardTakes();
      this.statuses = [];
    }
  }

  aboutToDisappear(): void {
    if (this.timer >= 0) {
      clearInterval(this.timer);
    }
    this.discardTakes();
  }

  build() {
    Column({ space: 14 }) {
      Text($r('app.string.teach_title')).fontSize(22).fontWeight(FontWeight.Bold).fontColor($r('app.color.on_surface'))
      Text($r('app.string.teach_intro')).fontSize(14).fontColor($r('app.color.muted'))

      ForEach(this.statuses, (status: TakeStatus, index: number) => {
        Row({ space: 8 }) {
          Text(status.ok ? '✓' : '✗').fontSize(20).fontColor(status.ok ? $r('app.color.accent') : $r('app.color.danger'))
          if (status.ok) {
            Text($r('app.string.teach_take_ok', index + 1)).fontSize(15).fontColor($r('app.color.on_surface'))
          } else {
            Text($r('app.string.teach_take_bad', index + 1, status.message)).fontSize(15)
              .fontColor($r('app.color.danger')).layoutWeight(1)
          }
        }
        .width('100%')
      }, (status: TakeStatus, index: number) => index.toString() + status.ok)

      if (this.recording) {
        Text($r('app.string.teach_recording', this.secondsLeft)).fontSize(16).fontColor($r('app.color.accent'))
      } else if (this.statuses.length < MAX_TAKES) {
        Button($r('app.string.teach_record', this.statuses.length + 1), { type: ButtonType.Capsule })
          .height(50)
          .width('100%')
          .linearGradient({ angle: 90, colors: [[$r('app.color.accent'), 0], [$r('app.color.accent_end'), 1]] })
          .fontColor($r('app.color.on_accent'))
          .onClick((): void => {
            this.record();
          })
      }

      if (this.goodTakes() < MIN_GOOD_TAKES) {
        Text($r('app.string.teach_need_two')).fontSize(13).fontColor($r('app.color.muted'))
      }

      TextInput({ text: this.name, placeholder: $r('app.string.teach_name_hint') })
        .maxLength(40)
        .height(50)
        .borderRadius(16)
        .backgroundColor($r('app.color.surface_alt'))
        .onChange((value: string): void => {
          this.name = value;
        })

      if (this.error !== '') {
        Text($r('app.string.teach_failed', this.error)).fontSize(13).fontColor($r('app.color.danger'))
      }

      Row({ space: 12 }) {
        Button($r('app.string.btn_cancel'), { type: ButtonType.Capsule })
          .height(50)
          .layoutWeight(1)
          .backgroundColor($r('app.color.surface_alt'))
          .fontColor($r('app.color.accent'))
          .onClick((): void => {
            this.onClose();
          })
        Button($r('app.string.teach_save'), { type: ButtonType.Capsule })
          .height(50)
          .layoutWeight(1)
          .linearGradient({ angle: 90, colors: [[$r('app.color.accent'), 0], [$r('app.color.accent_end'), 1]] })
          .fontColor($r('app.color.on_accent'))
          .enabled(!this.recording && !this.saving && this.goodTakes() >= MIN_GOOD_TAKES && this.name.trim() !== '')
          .onClick((): void => {
            this.save();
          })
      }
      .width('100%')
    }
    .padding(24)
    .backgroundColor($r('app.color.surface'))
    .borderRadius(30)
    .border({ width: 1, color: $r('app.color.border') })
    .shadow({ radius: 30, color: '#55000000', offsetY: 10 })
    .onTouch((): void => {
      ServiceHub.onTouch();
    })
  }
}
