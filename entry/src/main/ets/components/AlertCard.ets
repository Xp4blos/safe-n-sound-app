import { SoundRecord } from '../model/SoundModels';

function twoDigits(n: number): string {
  return n < 10 ? '0' + n : n.toString();
}

function clockTime(ms: number): string {
  const d = new Date(ms);
  return twoDigits(d.getHours()) + ':' + twoDigits(d.getMinutes()) + ':' + twoDigits(d.getSeconds());
}

// The "<Name> detected" card for a named sound that was just heard. A component of its own (not a @Builder with a
// parameter) so that it redraws when a new alert replaces the record it shows.
@Component
export struct AlertCard {
  @Prop record: SoundRecord | undefined = undefined;
  @Prop heading: string = ''; // replaces "<name> detected" (keyword cards)

  private recentCount(r: SoundRecord): number {
    return r.recentMs.filter((t: number): boolean => r.lastSeenMs - t <= 30000).length;
  }

  build() {
    Row({ space: 16 }) {
      Column() {
        SymbolGlyph($r('sys.symbol.bell'))
          .fontSize(30)
          .fontColor([$r('app.color.on_accent')])
      }
      .width(56)
      .height(56)
      .borderRadius(28)
      .justifyContent(FlexAlign.Center)
      .backgroundColor('#33FFFFFF')

      Column({ space: 4 }) {
        Text(this.heading !== '' ? this.heading :
          $r('app.string.alert_card_title', this.record === undefined ? '' : this.record.name))
          .fontSize(22).fontWeight(FontWeight.Bold).fontColor($r('app.color.on_accent'))
        Text($r('app.string.at_time', this.record === undefined ? '' : clockTime(this.record.lastSeenMs)))
          .fontSize(15).fontColor($r('app.color.on_accent')).opacity(0.9)
        if (this.record !== undefined && this.recentCount(this.record) > 1) {
          Text($r('app.string.alert_ongoing', this.recentCount(this.record)))
            .fontSize(13).fontColor($r('app.color.on_accent')).opacity(0.9)
        } else {
          Text($r('app.string.alert_once', this.record === undefined ? 0 : this.record.timesHeard))
            .fontSize(13).fontColor($r('app.color.on_accent')).opacity(0.9)
        }
      }
      .alignItems(HorizontalAlign.Start)
      .layoutWeight(1)
    }
    .width('100%')
    .padding(20)
    .borderRadius(26)
    .linearGradient({ angle: 120, colors: [[$r('app.color.accent'), 0], [$r('app.color.accent_end'), 1]] })
    .shadow({ radius: 20, color: '#443B7BF0', offsetY: 8 })
  }
}
