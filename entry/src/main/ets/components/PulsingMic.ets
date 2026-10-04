// Orb microphone: soft halo rings that grow with the live sound level (0..1) around a gradient disc. Tapping it
// starts or stops listening, like the main button.
@Component
export struct PulsingMic {
  @Prop level: number = 0;
  @Prop active: boolean = false;
  onTap: () => void = (): void => {};
  private orb: number = 120;
  private extra: number = 56;

  private haloSize(scale: number): number {
    return this.orb + 22 + (this.active ? this.level * this.extra * scale : 0);
  }

  build() {
    Stack() {
      // outer and inner halo, pulsing with the level
      Column()
        .width(this.haloSize(1))
        .height(this.haloSize(1))
        .borderRadius(this.haloSize(1) / 2)
        .backgroundColor($r('app.color.accent_soft'))
        .opacity(this.active ? 0.55 : 0.0)
        .animation({ duration: 160, curve: Curve.EaseOut })
      Column()
        .width(this.haloSize(0.55) - 14)
        .height(this.haloSize(0.55) - 14)
        .borderRadius((this.haloSize(0.55) - 14) / 2)
        .backgroundColor($r('app.color.accent_soft'))
        .opacity(this.active ? 0.9 : 0.5)
        .animation({ duration: 160, curve: Curve.EaseOut })

      Column() {
        SymbolGlyph($r('sys.symbol.mic'))
          .fontSize(52)
          .fontColor([this.active ? $r('app.color.on_accent') : $r('app.color.muted')])
      }
      .width(this.orb)
      .height(this.orb)
      .borderRadius(this.orb / 2)
      .justifyContent(FlexAlign.Center)
      .alignItems(HorizontalAlign.Center)
      .linearGradient(this.active ?
        { angle: 135, colors: [[$r('app.color.accent'), 0], [$r('app.color.accent_end'), 1]] } :
        { angle: 135, colors: [[$r('app.color.surface_alt'), 0], [$r('app.color.surface_alt'), 1]] })
      .border({ width: this.active ? 0 : 1, color: $r('app.color.border') })
      .shadow({ radius: this.active ? 24 : 10, color: this.active ? '#553B7BF0' : '#22000000', offsetY: 8 })
    }
    .width(this.orb + 22 + this.extra)
    .height(this.orb + 22 + this.extra)
    .onClick((): void => {
      this.onTap();
    })
  }
}
