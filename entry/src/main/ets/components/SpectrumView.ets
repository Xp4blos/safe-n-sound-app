// Equaliser-style live view: the last 8 seconds of sound, oldest on the left. Each column shows the 16 frequency
// bands (low at the bottom); loudness is a colour ramp from deep blue to ice, and the moments the engine reacted
// to are highlighted with a warm amber ramp.

function lerp(a: number, b: number, t: number): number {
  return Math.round(a + (b - a) * t);
}

// Colour ramp over intensity 0..1 through three stops (rgb triples).
function ramp(v: number, low: number[], mid: number[], high: number[]): string {
  const t = Math.min(1, Math.max(0, v));
  const from = t < 0.55 ? low : mid;
  const to = t < 0.55 ? mid : high;
  const k = t < 0.55 ? t / 0.55 : (t - 0.55) / 0.45;
  const alpha = 0.3 + 0.7 * t;
  return 'rgba(' + lerp(from[0], to[0], k) + ',' + lerp(from[1], to[1], k) + ',' + lerp(from[2], to[2], k) + ',' +
    alpha.toFixed(2) + ')';
}

const COOL_LOW: number[] = [37, 78, 190];
const COOL_MID: number[] = [56, 160, 246];
const COOL_HIGH: number[] = [214, 240, 255];
const WARM_LOW: number[] = [217, 119, 6];
const WARM_MID: number[] = [251, 176, 36];
const WARM_HIGH: number[] = [255, 243, 205];

@Component
export struct SpectrumView {
  @Prop @Watch('redraw') columns: number[] = [];
  @Prop @Watch('redraw') marks: number[] = [];
  private settings: RenderingContextSettings = new RenderingContextSettings(true);
  private ctx: CanvasRenderingContext2D = new CanvasRenderingContext2D(this.settings);
  private canvasWidth: number = 0;
  private canvasHeight: number = 0;
  private bandCount: number = 16;
  private viewHeight: number = 150;

  redraw(): void {
    const w = this.canvasWidth;
    const h = this.canvasHeight;
    if (w <= 0 || h <= 0) {
      return;
    }
    this.ctx.clearRect(0, 0, w, h);
    const columnCount = Math.floor(this.columns.length / this.bandCount);
    if (columnCount === 0) {
      return;
    }
    const cw = w / columnCount;
    const ch = h / this.bandCount;
    const gap = Math.min(1.5, cw * 0.18);
    for (let c = 0; c < columnCount; c++) {
      const marked = this.marks.length > c && this.marks[c] === 1;
      for (let b = 0; b < this.bandCount; b++) {
        const v = this.columns[c * this.bandCount + b];
        if (v < 0.05) {
          continue;
        }
        this.ctx.fillStyle = marked ? ramp(v, WARM_LOW, WARM_MID, WARM_HIGH) : ramp(v, COOL_LOW, COOL_MID, COOL_HIGH);
        this.ctx.fillRect(c * cw + gap / 2, h - (b + 1) * ch + gap / 2, Math.max(1, cw - gap), Math.max(1, ch - gap));
      }
      if (marked) {
        this.ctx.fillStyle = 'rgba(251,191,36,0.95)';
        this.ctx.fillRect(c * cw, 0, Math.max(1, cw - 0.5), 3);
      }
    }
  }

  build() {
    Column() {
      Column() {
        Canvas(this.ctx)
          .width('100%')
          .height('100%')
          .onReady((): void => {
            this.redraw();
          })
          .onAreaChange((oldValue: Area, newValue: Area): void => {
            this.canvasWidth = Number(newValue.width);
            this.canvasHeight = Number(newValue.height);
            this.redraw();
          })
      }
      .width('100%')
      .height(this.viewHeight)
      .padding(10)
      .borderRadius(24)
      .linearGradient({ angle: 180, colors: [['#12203D', 0], ['#0B1426', 1]] })
      .shadow({ radius: 16, color: '#33000000', offsetY: 6 })

      Row() {
        Text($r('app.string.chart_caption_start')).fontSize(12).fontColor($r('app.color.muted'))
        Blank()
        Text($r('app.string.chart_caption_end')).fontSize(12).fontColor($r('app.color.muted'))
      }
      .width('100%')
      .padding({ left: 6, right: 6 })
      .margin({ top: 6 })
    }
    .width('100%')
  }
}
