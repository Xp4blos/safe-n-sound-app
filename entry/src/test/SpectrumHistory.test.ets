import { describe, it, expect } from '@ohos/hypium';
import { SpectrumHistory } from '../main/ets/model/SpectrumHistory';

function bands(value: number): number[] {
  const b: number[] = [];
  for (let i = 0; i < SpectrumHistory.BANDS; i++) {
    b.push(value);
  }
  return b;
}

function cell(h: SpectrumHistory, column: number, band: number): number {
  return h.columns()[column * SpectrumHistory.BANDS + band];
}

export default function spectrumHistoryTest() {
  describe('spectrumHistory', () => {
    it('starts_empty_with_the_expected_shape', 0, () => {
      const h = new SpectrumHistory();
      expect(h.columns().length).assertEqual(SpectrumHistory.COLUMNS * SpectrumHistory.BANDS);
      expect(h.marks().length).assertEqual(SpectrumHistory.COLUMNS);
      expect(h.columns().every((v: number): boolean => v === 0)).assertTrue();
      expect(h.level()).assertEqual(0);
    });

    it('window_covers_eight_seconds', 0, () => {
      expect(SpectrumHistory.COLUMNS * SpectrumHistory.SLOT_MS).assertEqual(8000);
    });

    it('pushes_fill_the_newest_column_and_keep_the_loudest_per_band', 0, () => {
      const h = new SpectrumHistory();
      const b = bands(0);
      b[3] = 0.4;
      h.push(b, 1000);
      const b2 = bands(0);
      b2[3] = 0.9;
      b2[5] = 0.2;
      h.push(b2, 1050);
      const newest = SpectrumHistory.COLUMNS - 1;
      expect(cell(h, newest, 3)).assertEqual(0.9);
      expect(cell(h, newest, 5)).assertEqual(0.2);
      expect(h.level()).assertEqual(0.9);
    });

    it('a_new_time_slot_scrolls_older_columns_left', 0, () => {
      const h = new SpectrumHistory();
      h.push(bands(0.8), 1000);
      h.push(bands(0.1), 1000 + SpectrumHistory.SLOT_MS);
      expect(cell(h, SpectrumHistory.COLUMNS - 2, 0)).assertEqual(0.8);
      expect(cell(h, SpectrumHistory.COLUMNS - 1, 0)).assertEqual(0.1);
    });

    it('a_gap_inserts_empty_columns_and_old_data_falls_off', 0, () => {
      const h = new SpectrumHistory();
      h.push(bands(0.8), 1000);
      h.push(bands(0.1), 1000 + 3 * SpectrumHistory.SLOT_MS);
      expect(cell(h, SpectrumHistory.COLUMNS - 4, 0)).assertEqual(0.8);
      expect(cell(h, SpectrumHistory.COLUMNS - 3, 0)).assertEqual(0);
      h.push(bands(0), 1000 + 1000 * SpectrumHistory.SLOT_MS);
      expect(h.columns().every((v: number): boolean => v === 0)).assertTrue();
    });

    it('detections_highlight_the_last_few_columns_and_scroll_with_them', 0, () => {
      const h = new SpectrumHistory();
      h.push(bands(0.5), 1000);
      h.markDetection(1000);
      const marks = h.marks();
      const last = SpectrumHistory.COLUMNS - 1;
      for (let i = 0; i < SpectrumHistory.MARK_SPAN; i++) {
        expect(marks[last - i]).assertEqual(1);
      }
      expect(marks[last - SpectrumHistory.MARK_SPAN]).assertEqual(0);
      h.push(bands(0.5), 1000 + SpectrumHistory.SLOT_MS);
      expect(h.marks()[last - 1]).assertEqual(1);
      expect(h.marks()[last]).assertEqual(0);
    });

    it('values_are_clamped_and_short_band_lists_are_tolerated', 0, () => {
      const h = new SpectrumHistory();
      h.push([5, -3], 1000);
      const newest = SpectrumHistory.COLUMNS - 1;
      expect(cell(h, newest, 0)).assertEqual(1);
      expect(cell(h, newest, 1)).assertEqual(0);
      expect(cell(h, newest, 2)).assertEqual(0);
    });

    it('reset_clears_everything', 0, () => {
      const h = new SpectrumHistory();
      h.push(bands(0.8), 1000);
      h.markDetection(1000);
      h.reset();
      expect(h.columns().every((v: number): boolean => v === 0)).assertTrue();
      expect(h.marks().every((v: number): boolean => v === 0)).assertTrue();
    });
  });
}
