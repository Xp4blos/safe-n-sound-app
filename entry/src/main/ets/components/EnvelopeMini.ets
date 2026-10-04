// Tiny bar chart of a sound's loudness envelope (8 points), the "pattern visual" of a history entry.
@Component
export struct EnvelopeMini {
  @Prop envelope: number[] = [];
  private barHeight: number = 28;

  build() {
    Row({ space: 2 }) {
      ForEach(this.envelope, (value: number, index: number) => {
        Column()
          .width(5)
          .height(Math.max(3, value * this.barHeight))
          .borderRadius(2)
          .backgroundColor($r('app.color.accent'))
      }, (value: number, index: number) => index.toString() + ':' + value.toFixed(2))
    }
    .height(this.barHeight)
    .alignItems(VerticalAlign.Bottom)
  }
}
