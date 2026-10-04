import { SoundCatalog } from '../model/SoundCatalog';
import { SoundRecord } from '../model/SoundModels';

// The named sounds: alert switch per sound; tap one to rename or delete it.
@Component
export struct MySoundsTab {
  @Prop sounds: SoundRecord[] = [];
  onOpen: (id: number) => void = (id: number): void => {};
  onToggleAlerts: (id: number, enabled: boolean) => void = (id: number, enabled: boolean): void => {};

  @Builder
  row(record: SoundRecord) {
    Row({ space: 14 }) {
      Column() {
        Text(record.name.charAt(0).toUpperCase())
          .fontSize(20)
          .fontWeight(FontWeight.Bold)
          .fontColor($r('app.color.on_accent'))
      }
      .width(48)
      .height(48)
      .borderRadius(24)
      .justifyContent(FlexAlign.Center)
      .linearGradient({ angle: 135, colors: [[$r('app.color.accent'), 0], [$r('app.color.accent_end'), 1]] })

      Column({ space: 3 }) {
        Text(record.name).fontSize(17).fontWeight(FontWeight.Medium).fontColor($r('app.color.on_surface'))
        Text(SoundCatalog.describe(record.profile)).fontSize(13).fontColor($r('app.color.muted'))
        Text($r('app.string.heard_times', record.timesHeard)).fontSize(13).fontColor($r('app.color.muted'))
      }
      .alignItems(HorizontalAlign.Start)
      .layoutWeight(1)
      .onClick((): void => {
        this.onOpen(record.id);
      })

      Column({ space: 4 }) {
        Text($r('app.string.alerts_label')).fontSize(12).fontColor($r('app.color.muted'))
        Toggle({ type: ToggleType.Switch, isOn: record.alertsEnabled })
          .selectedColor($r('app.color.accent'))
          .onChange((isOn: boolean): void => {
            this.onToggleAlerts(record.id, isOn);
          })
      }
      .alignItems(HorizontalAlign.Center)
    }
    .width('100%')
    .padding(16)
    .borderRadius(22)
    .backgroundColor($r('app.color.surface'))
    .border({ width: 1, color: $r('app.color.border') })
  }

  build() {
    Column({ space: 14 }) {
      Text($r('app.string.tab_mysounds'))
        .fontSize(26)
        .fontWeight(FontWeight.Bold)
        .fontColor($r('app.color.on_surface'))
        .width('100%')

      if (this.sounds.length === 0) {
        Column() {
          Text($r('app.string.mysounds_empty'))
            .fontSize(16)
            .fontColor($r('app.color.muted'))
            .textAlign(TextAlign.Center)
            .padding(32)
        }
        .width('100%')
        .layoutWeight(1)
        .justifyContent(FlexAlign.Center)
      } else {
        List({ space: 12 }) {
          ForEach(this.sounds, (record: SoundRecord) => {
            ListItem() {
              this.row(record)
            }
          }, (record: SoundRecord) => record.id.toString() + '|' + record.name + '|' + record.alertsEnabled + '|' +
            record.timesHeard)
        }
        .width('100%')
        .layoutWeight(1)
        .scrollBar(BarState.Off)
      }
    }
    .width('100%')
    .height('100%')
    .padding({ left: 18, right: 18, top: 20, bottom: 12 })
  }
}
