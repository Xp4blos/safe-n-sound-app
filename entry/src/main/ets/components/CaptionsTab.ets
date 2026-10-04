import { CaptionPiece, captionPieces } from '../model/CaptionText';
import { KeywordRule, SpeechCaption } from '../model/SpeechModels';

const PRESETS: string[] = ['sos', 'rapid', 'long', 'double'];

// Live captions with the keywords highlighted, the list of keywords (add / remove, with a vibration pattern) and a
// typed-text input that feeds the same pipeline, clearly marked as a simulation.
@Component
export struct CaptionsTab {
  @Prop captions: SpeechCaption[] = [];
  @Prop live: SpeechCaption | undefined = undefined;
  @Prop lastAlert: string = '';
  @Prop rules: KeywordRule[] = [];
  @Prop active: boolean = false;
  @Prop listening: boolean = false;
  @Prop speechOn: boolean = true;
  @Prop error: string = '';
  @State newPhrase: string = '';
  @State preset: string = 'long';
  @State testText: string = '';
  onSimulate: (text: string) => void = (text: string): void => {};
  onClear: () => void = (): void => {};
  onAddRule: (phrase: string, preset: string) => void = (phrase: string, preset: string): void => {};
  onRemoveRule: (phrase: string) => void = (phrase: string): void => {};

  private statusText(): Resource {
    if (!this.speechOn) {
      return $r('app.string.captions_off');
    }
    return this.active ? $r('app.string.captions_live') : $r('app.string.captions_idle');
  }

  private presetTitle(p: string): Resource {
    if (p === 'sos') {
      return $r('app.string.vib_sos');
    }
    if (p === 'rapid') {
      return $r('app.string.vib_rapid');
    }
    return p === 'long' ? $r('app.string.vib_long') : $r('app.string.vib_double');
  }

  @Builder
  captionLine(caption: SpeechCaption, interim: boolean) {
    Text() {
      ForEach(captionPieces(caption), (piece: CaptionPiece) => {
        Span(piece.text)
          .fontColor(piece.highlighted ? $r('app.color.danger') : (interim ? $r('app.color.muted') :
          $r('app.color.on_surface')))
          .fontWeight(piece.highlighted ? FontWeight.Bold : FontWeight.Normal)
      }, (piece: CaptionPiece, index: number) => index.toString() + piece.text)
    }
    .fontSize(17)
    .width('100%')
    .padding(14)
    .borderRadius(18)
    .backgroundColor($r('app.color.surface'))
    .border({ width: 1, color: $r('app.color.border') })
  }

  @Builder
  card(title: Resource) {
    Text(title)
      .fontSize(13)
      .fontWeight(FontWeight.Medium)
      .fontColor($r('app.color.muted'))
      .width('100%')
      .padding({ left: 6, top: 4 })
  }

  build() {
    Column({ space: 12 }) {
      Row() {
        Text($r('app.string.captions_title'))
          .fontSize(26)
          .fontWeight(FontWeight.Bold)
          .fontColor($r('app.color.on_surface'))
        Blank()
        Text($r('app.string.captions_clear'))
          .fontSize(15)
          .fontColor($r('app.color.accent'))
          .padding(8)
          .onClick((): void => {
            this.onClear();
          })
      }
      .width('100%')

      Row({ space: 8 }) {
        Circle().width(9).height(9).fill(this.active ? $r('app.color.success') : $r('app.color.muted'))
        Text(this.statusText()).fontSize(14).fontColor($r('app.color.on_surface')).layoutWeight(1)
      }
      .width('100%')

      Scroll() {
        Column({ space: 10 }) {
          Text($r('app.string.captions_note'))
            .fontSize(12)
            .fontColor($r('app.color.muted'))
            .width('100%')

          if (this.error !== '') {
            Text(this.error).fontSize(13).fontColor($r('app.color.danger')).width('100%')
          }

          if (this.lastAlert !== '') {
            Row({ space: 10 }) {
              SymbolGlyph($r('sys.symbol.bell'))
                .fontSize(22)
                .fontColor([$r('app.color.on_accent')])
              Text(this.lastAlert).fontSize(16).fontWeight(FontWeight.Medium).fontColor($r('app.color.on_accent'))
                .layoutWeight(1)
            }
            .width('100%')
            .padding(14)
            .borderRadius(18)
            .linearGradient({ angle: 120, colors: [[$r('app.color.accent'), 0], [$r('app.color.accent_end'), 1]] })
          }

          if (this.live !== undefined) {
            this.captionLine(this.live, true)
          }
          if (this.captions.length === 0 && this.live === undefined) {
            Text($r('app.string.captions_empty'))
              .fontSize(15)
              .fontColor($r('app.color.muted'))
              .width('100%')
              .textAlign(TextAlign.Center)
              .padding(20)
          }
          ForEach(this.captions.slice().reverse(), (caption: SpeechCaption) => {
            this.captionLine(caption, false)
          }, (caption: SpeechCaption, index: number) => index.toString() + '|' + caption.utteranceId + '|' + caption.text)

          this.card($r('app.string.keywords_title'))
          Column({ space: 10 }) {
            Flex({ wrap: FlexWrap.Wrap }) {
              ForEach(this.rules, (rule: KeywordRule) => {
                Row({ space: 6 }) {
                  Text(rule.phrase).fontSize(14).fontColor($r('app.color.on_surface'))
                  Text('×')
                    .fontSize(16)
                    .fontColor($r('app.color.muted'))
                    .onClick((): void => {
                      this.onRemoveRule(rule.phrase);
                    })
                }
                .padding({ left: 12, right: 12, top: 6, bottom: 6 })
                .margin({ right: 8, bottom: 8 })
                .borderRadius(16)
                .backgroundColor($r('app.color.surface_alt'))
              }, (rule: KeywordRule) => rule.phrase)
            }
            .width('100%')

            Row({ space: 8 }) {
              TextInput({ placeholder: $r('app.string.keywords_hint'), text: this.newPhrase })
                .layoutWeight(1)
                .height(44)
                .backgroundColor($r('app.color.surface_alt'))
                .fontColor($r('app.color.on_surface'))
                .onChange((value: string): void => {
                  this.newPhrase = value;
                })
              Text($r('app.string.keywords_add'))
                .fontSize(15)
                .fontWeight(FontWeight.Medium)
                .fontColor($r('app.color.on_accent'))
                .padding({ left: 16, right: 16, top: 11, bottom: 11 })
                .borderRadius(14)
                .backgroundColor($r('app.color.accent'))
                .onClick((): void => {
                  if (this.newPhrase.trim() !== '') {
                    this.onAddRule(this.newPhrase, this.preset);
                    this.newPhrase = '';
                  }
                })
            }
            .width('100%')

            Row({ space: 8 }) {
              ForEach(PRESETS, (p: string) => {
                Text(this.presetTitle(p))
                  .fontSize(13)
                  .fontColor(this.preset === p ? $r('app.color.on_accent') : $r('app.color.on_surface'))
                  .padding({ left: 12, right: 12, top: 6, bottom: 6 })
                  .borderRadius(14)
                  .backgroundColor(this.preset === p ? $r('app.color.accent') : $r('app.color.surface_alt'))
                  .onClick((): void => {
                    this.preset = p;
                  })
              }, (p: string) => p + this.preset)
            }
            .width('100%')
          }
          .padding(14)
          .borderRadius(18)
          .backgroundColor($r('app.color.surface'))
          .border({ width: 1, color: $r('app.color.border') })

          this.card($r('app.string.simulate_title'))
          Row({ space: 8 }) {
            TextInput({ placeholder: $r('app.string.simulate_hint'), text: this.testText })
              .layoutWeight(1)
              .height(44)
              .backgroundColor($r('app.color.surface_alt'))
              .fontColor($r('app.color.on_surface'))
              .onChange((value: string): void => {
                this.testText = value;
              })
            Text($r('app.string.simulate_btn'))
              .fontSize(15)
              .fontWeight(FontWeight.Medium)
              .fontColor($r('app.color.accent'))
              .padding({ left: 14, right: 14, top: 11, bottom: 11 })
              .borderRadius(14)
              .border({ width: 1, color: $r('app.color.accent') })
              .onClick((): void => {
                if (this.testText.trim() !== '') {
                  this.onSimulate(this.testText);
                  this.testText = '';
                }
              })
          }
          .width('100%')
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
