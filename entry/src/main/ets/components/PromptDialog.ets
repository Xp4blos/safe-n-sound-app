import { ServiceHub } from '../services/ServiceHub';
// "I've heard this sound before. Do you want to name it?" with the three choices of the spec.
@CustomDialog
export struct PromptDialog {
  controller?: CustomDialogController; // required by the compiler; not reliably set for dialogs built in methods
  // Closes the dialog. The page passes this in explicitly because the controller above can be undefined.
  onClose: () => void = (): void => {};
  onNameIt: () => void = (): void => {};
  onNotNow: () => void = (): void => {};
  onNever: () => void = (): void => {};

  build() {
    Column({ space: 16 }) {
      Text($r('app.string.prompt_title')).fontSize(22).fontWeight(FontWeight.Bold).fontColor($r('app.color.on_surface'))
      Text($r('app.string.prompt_text')).fontSize(16).fontColor($r('app.color.muted'))

      Button($r('app.string.prompt_name'), { type: ButtonType.Capsule })
        .height(50)
        .width('100%')
        .linearGradient({ angle: 90, colors: [[$r('app.color.accent'), 0], [$r('app.color.accent_end'), 1]] })
        .fontColor($r('app.color.on_accent'))
        .onClick((): void => {
          this.onClose();
          this.onNameIt();
        })
      Button($r('app.string.prompt_later'), { type: ButtonType.Capsule })
        .height(50)
        .width('100%')
        .backgroundColor($r('app.color.surface_alt'))
        .fontColor($r('app.color.accent'))
        .onClick((): void => {
          this.onClose();
          this.onNotNow();
        })
      Button($r('app.string.prompt_never'), { type: ButtonType.Capsule })
        .height(50)
        .width('100%')
        .backgroundColor(Color.Transparent)
        .fontColor($r('app.color.muted'))
        .onClick((): void => {
          this.onClose();
          this.onNever();
        })
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
