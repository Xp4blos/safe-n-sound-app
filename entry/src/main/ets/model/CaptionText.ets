import { SpeechCaption } from './SpeechModels';

export interface CaptionPiece {
  text: string;
  highlighted: boolean; // a keyword
}

// Splits a caption into plain and keyword pieces for display. Defensive: out-of-range or overlapping highlight
// ranges are clipped, empty ones dropped.
export function captionPieces(caption: SpeechCaption): CaptionPiece[] {
  const text = caption.text;
  const ranges: number[][] = [];
  for (const h of caption.highlights) {
    const begin = Math.max(0, Math.min(text.length, h.begin));
    const end = Math.max(0, Math.min(text.length, h.end));
    if (end > begin) {
      ranges.push([begin, end]);
    }
  }
  ranges.sort((a: number[], b: number[]): number => a[0] - b[0]);
  const pieces: CaptionPiece[] = [];
  let cursor = 0;
  for (const r of ranges) {
    const begin = Math.max(r[0], cursor);
    if (r[1] <= begin) {
      continue;
    }
    if (begin > cursor) {
      const plain: CaptionPiece = { text: text.substring(cursor, begin), highlighted: false };
      pieces.push(plain);
    }
    const last = pieces.length > 0 ? pieces[pieces.length - 1] : undefined;
    if (last !== undefined && last.highlighted) {
      last.text += text.substring(begin, r[1]); // overlapping or touching keywords read as one
    } else {
      const hit: CaptionPiece = { text: text.substring(begin, r[1]), highlighted: true };
      pieces.push(hit);
    }
    cursor = r[1];
  }
  if (cursor < text.length) {
    const rest: CaptionPiece = { text: text.substring(cursor), highlighted: false };
    pieces.push(rest);
  }
  return pieces;
}
