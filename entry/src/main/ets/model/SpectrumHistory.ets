// Rolling record of the live spectrum over the last 8 seconds (64 columns of 125 ms, 16 frequency bands each)
// with the moments the engine reacted highlighted. Pure logic: the caller supplies the clock.
export class SpectrumHistory {
  static readonly SLOT_MS: number = 125;
  static readonly COLUMNS: number = 64; // 64 x 125 ms = 8 s
  static readonly BANDS: number = 16;
  static readonly MARK_SPAN: number = 4; // a detection highlights this many columns (the 0.5 s before it)

  private cells: number[] = [];
  private flags: number[] = [];
  private lastSlot: number = -1;

  constructor() {
    this.reset();
  }

  reset(): void {
    this.cells = [];
    for (let i = 0; i < SpectrumHistory.COLUMNS * SpectrumHistory.BANDS; i++) {
      this.cells.push(0);
    }
    this.flags = [];
    for (let i = 0; i < SpectrumHistory.COLUMNS; i++) {
      this.flags.push(0);
    }
    this.lastSlot = -1;
  }

  private advance(nowMs: number): void {
    const slot = Math.floor(nowMs / SpectrumHistory.SLOT_MS);
    if (this.lastSlot < 0) {
      this.lastSlot = slot;
    }
    if (slot > this.lastSlot) {
      const shift = Math.min(slot - this.lastSlot, SpectrumHistory.COLUMNS);
      this.cells = this.cells.slice(shift * SpectrumHistory.BANDS);
      this.flags = this.flags.slice(shift);
      for (let i = 0; i < shift * SpectrumHistory.BANDS; i++) {
        this.cells.push(0);
      }
      for (let i = 0; i < shift; i++) {
        this.flags.push(0);
      }
      this.lastSlot = slot;
    }
  }

  // `bands` are 0..1 per band; within one slot the loudest value per band wins.
  push(bands: number[], nowMs: number): void {
    this.advance(nowMs);
    const base = (SpectrumHistory.COLUMNS - 1) * SpectrumHistory.BANDS;
    for (let b = 0; b < SpectrumHistory.BANDS; b++) {
      const raw = b < bands.length ? bands[b] : 0;
      const v = Math.min(1, Math.max(0, raw));
      this.cells[base + b] = Math.max(this.cells[base + b], v);
    }
  }

  markDetection(nowMs: number): void {
    this.advance(nowMs);
    for (let i = 0; i < SpectrumHistory.MARK_SPAN; i++) {
      this.flags[SpectrumHistory.COLUMNS - 1 - i] = 1;
    }
  }

  // Row-major: index = column * BANDS + band, oldest column first.
  columns(): number[] {
    return this.cells.slice();
  }

  // 1 where the column is highlighted as a detection, one entry per column.
  marks(): number[] {
    return this.flags.slice();
  }

  // Loudest band of the newest column, 0..1 (drives the pulsing microphone).
  level(): number {
    const base = (SpectrumHistory.COLUMNS - 1) * SpectrumHistory.BANDS;
    let m = 0;
    for (let b = 0; b < SpectrumHistory.BANDS; b++) {
      m = Math.max(m, this.cells[base + b]);
    }
    return m;
  }
}
