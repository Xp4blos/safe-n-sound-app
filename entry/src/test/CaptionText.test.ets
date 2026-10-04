import { describe, it, expect } from '@ohos/hypium';
import { captionPieces } from '../main/ets/model/CaptionText';
import { SpeechCaption, SpeechHighlight } from '../main/ets/model/SpeechModels';

function cap(text: string, ranges: number[][]): SpeechCaption {
  const highlights: SpeechHighlight[] = [];
  for (const r of ranges) {
    const h: SpeechHighlight = { begin: r[0], end: r[1], ruleId: 1, label: 'x' };
    highlights.push(h);
  }
  const c: SpeechCaption = { utteranceId: 1, isFinal: true, timeSec: 0, text: text, highlights: highlights };
  return c;
}

function show(c: SpeechCaption): string {
  return captionPieces(c).map((p) => (p.highlighted ? '[' + p.text + ']' : p.text)).join('');
}

export default function captionTextTest() {
  describe('captionText', () => {
    it('keywords_are_marked_inside_the_text', 0, () => {
      expect(show(cap('please help me now', [[7, 11]]))).assertEqual('please [help] me now');
    });

    it('text_without_keywords_is_one_plain_piece', 0, () => {
      expect(captionPieces(cap('good morning', [])).length).assertEqual(1);
      expect(show(cap('good morning', []))).assertEqual('good morning');
    });

    it('several_keywords_and_a_keyword_at_either_end', 0, () => {
      expect(show(cap('help watch out help', [[0, 4], [5, 14], [15, 19]]))).assertEqual('[help] [watch out] [help]');
    });

    it('bad_ranges_are_clipped_or_dropped', 0, () => {
      expect(show(cap('abc', [[2, 99]]))).assertEqual('ab[c]');
      expect(show(cap('abc', [[2, 2], [-5, -1]]))).assertEqual('abc');
      expect(show(cap('abcdef', [[1, 4], [2, 5]]))).assertEqual('a[bcde]f');
    });

    it('an_empty_caption_has_no_pieces', 0, () => {
      expect(captionPieces(cap('', [])).length).assertEqual(0);
    });
  });
}
