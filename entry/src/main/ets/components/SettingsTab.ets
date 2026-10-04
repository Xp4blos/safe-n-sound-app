import { SoundClass, SoundClasses } from '../model/SoundClasses';

// Settings: which sound classes alert, how, and whether the phone keeps listening in the background.
// `onKeys` holds the switches that are on: 'vibrate', 'notify', 'background', 'autoStart', 'speech', 'partial' and
// 'class:<key>' for the sound classes.
@Component
export struct SettingsTab {
  @Prop onKeys: string[] = [];
  onToggle: (key: string, on: boolean) => void = (key: string, on: boolean): void => {};

  private isOn(key: string): boolean {
    return this.onKeys.includes(key);
  }

  @Builder
  switchRow(title: string, key: string) {
    Row() {
      Text(title)
        .fontSize(16)
        .fontColor($r('app.color.on_surface'))
        .layoutWeight(1)
      Toggle({ type: ToggleType.Switch, isOn: this.isOn(key) })
        .selectedColor($r('app.color.accent'))
        .onChange((isOn: boolean): void => {
          this.onToggle(key, isOn);
        })
    }
    .width('100%')
    .padding({ top: 6, bottom: 6 })
  }

  @Builder
  section(title: Resource) {
    Text(title)
      .fontSize(13)
      .fontWeight(FontWeight.Medium)
      .fontColor($r('app.color.muted'))
      .width('100%')
      .padding({ left: 6, top: 4 })
  }

  build() {
    Column({ space: 12 }) {
      Text($r('app.string.settings_title'))
        .fontSize(26)
        .fontWeight(FontWeight.Bold)
        .fontColor($r('app.color.on_surface'))
        .width('100%')

      Scroll() {
        Column({ space: 12 }) {
          this.section($r('app.string.settings_classes'))
          Column() {
            ForEach(SoundClasses.all(), (cls: SoundClass) => {
              this.switchRow(cls.title, 'class:' + cls.key)
            }, (cls: SoundClass) => cls.key + '|' + this.isOn('class:' + cls.key))
            Text($r('app.string.set_unknown_note'))
              .fontSize(12)
              .fontColor($r('app.color.muted'))
              .width('100%')
              .padding({ top: 6 })
          }
          .padding(16)
          .borderRadius(22)
          .backgroundColor($r('app.color.surface'))
          .border({ width: 1, color: $r('app.color.border') })

          this.section($r('app.string.settings_how'))
          Column() {
            this.switchRow('Vibrate', 'vibrate')
            this.switchRow('Notifications', 'notify')
          }
          .padding(16)
          .borderRadius(22)
          .backgroundColor($r('app.color.surface'))
          .border({ width: 1, color: $r('app.color.border') })

          this.section($r('app.string.settings_listening'))
          Column() {
            this.switchRow('Keep listening in the background', 'background')
            this.switchRow('Start listening when the app opens', 'autoStart')
          }
          .padding(16)
          .borderRadius(22)
          .backgroundColor($r('app.color.surface'))
          .border({ width: 1, color: $r('app.color.border') })

          this.section($r('app.string.settings_speech'))
          Column() {
            this.switchRow('Captions and keywords', 'speech')
            this.switchRow('React to words while still speaking', 'partial')
          }
          .padding(16)
          .borderRadius(22)
          .backgroundColor($r('app.color.surface'))
          .border({ width: 1, color: $r('app.color.border') })
        }
        .width('100%')
        .padding({ bottom: 16 })
      }
      .layoutWeight(1)
      .scrollBar(BarState.Off)
      .align(Alignment.Top)
    }
    .width('100%')
    .height('100%')
    .padding({ left: 18, right: 18, top: 20, bottom: 12 })
  }
}
