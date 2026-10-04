import { SoundClasses } from '../model/SoundClasses';
import { SoundRecord } from '../model/SoundModels';
import { EnvelopeMini } from './EnvelopeMini';

function twoDigits(n: number): string {
  return n < 10 ? '0' + n : n.toString();
}

function clockTime(ms: number): string {
  const d = new Date(ms);
  return twoDigits(d.getHours()) + ':' + twoDigits(d.getMinutes());
}

// Letter shown in the avatar: the first letter of the name, "?" for an unnamed sound, "…" while it is learned.
export function avatarLetter(record: SoundRecord): string {
  if (SoundClasses.isBuiltInLabel(record.label)) {
    return '';
  }
  if (record.name !== '') {
    return record.name.charAt(0).toUpperCase();
  }
  return record.template === '' ? '…' : '?';
}

// Chronological list of everything the phone has heard (newest first), one entry per sound.
@Component
export struct HistoryTab {
  @Prop sounds: SoundRecord[] = [];
  onOpen: (id: number) => void = (id: number): void => {};

  @Builder
  row(record: SoundRecord) {
    Row({ space: 14 }) {
      Column() {
        if (SoundClasses.isBuiltInLabel(record.label)) {
          SymbolGlyph($r('sys.symbol.bell'))
            .fontSize(24)
            .fontColor([$r('app.color.on_accent')])
        } else {
          Text(avatarLetter(record))
            .fontSize(20)
            .fontWeight(FontWeight.Bold)
            .fontColor(record.name !== '' ? $r('app.color.on_accent') : $r('app.color.accent'))
        }
      }
      .width(48)
      .height(48)
      .borderRadius(24)
      .justifyContent(FlexAlign.Center)
      .backgroundColor(record.name !== '' ? $r('app.color.accent') : $r('app.color.accent_soft'))

      Column({ space: 3 }) {
        if (record.name !== '') {
          Text(record.name).fontSize(17).fontWeight(FontWeight.Medium).fontColor($r('app.color.on_surface'))
        } else if (record.template === '') {
          Text($r('app.string.learning_sound')).fontSize(17).fontWeight(FontWeight.Medium)
            .fontColor($r('app.color.muted'))
        } else {
          Text($r('app.string.unknown_sound')).fontSize(17).fontWeight(FontWeight.Medium)
            .fontColor($r('app.color.on_surface'))
        }
        Text($r('app.string.heard_times', record.timesHeard)).fontSize(13).fontColor($r('app.color.muted'))
        Text($r('app.string.last_seen', clockTime(record.lastSeenMs))).fontSize(13).fontColor($r('app.color.muted'))
      }
      .alignItems(HorizontalAlign.Start)
      .layoutWeight(1)

      if (!SoundClasses.isBuiltInLabel(record.label)) {
        EnvelopeMini({ envelope: record.profile.envelope })
      }
    }
    .width('100%')
    .padding(16)
    .borderRadius(22)
    .backgroundColor($r('app.color.surface'))
    .border({ width: 1, color: $r('app.color.border') })
    .onClick((): void => {
      if (!SoundClasses.isBuiltInLabel(record.label)) {
        this.onOpen(record.id);
      }
    })
  }

  build() {
    Column({ space: 14 }) {
      Text($r('app.string.tab_history'))
        .fontSize(26)
        .fontWeight(FontWeight.Bold)
        .fontColor($r('app.color.on_surface'))
        .width('100%')

      if (this.sounds.length === 0) {
        Column() {
          Text($r('app.string.empty_history'))
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
          }, (record: SoundRecord) => record.id.toString() + '|' + record.timesHeard + '|' + record.name + '|' +
            record.template.length)
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
