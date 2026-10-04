import { describe, it, expect } from '@ohos/hypium';
import { AppSettings } from '../main/ets/model/AppSettings';
import { SpeechAlertInfo, SpeechCaption, SpeechUpdate } from '../main/ets/model/SpeechModels';
import { CaptionBoard, KeywordAlertPort, SpeechEnginePort } from '../main/ets/services/CaptionBoard';

function caption(text: string, isFinal: boolean, id: number): SpeechCaption {
  const c: SpeechCaption = { utteranceId: id, isFinal: isFinal, timeSec: 1, text: text, highlights: [] };
  return c;
}

function alertFor(text: string, phrase: string, pattern: number[]): SpeechAlertInfo {
  const begin = text.toLowerCase().indexOf(phrase);
  const a: SpeechAlertInfo = {
    ruleId: 1, label: phrase, phrase: phrase, timeSec: 1, begin: begin, end: begin + phrase.length, pattern: pattern
  };
  return a;
}

// Stands in for the native pipeline: an alert whenever the text contains "help".
class FakeSpeech implements SpeechEnginePort {
  ended: number = 0;
  resets: number = 0;

  speechResult(text: string, isFinal: boolean, timeSec: number): SpeechUpdate {
    const alerts: SpeechAlertInfo[] = [];
    if (text.toLowerCase().includes('help')) {
      alerts.push(alertFor(text, 'help', [100, 50, 100]));
    }
    const u: SpeechUpdate = { caption: caption(text, isFinal, 1), alerts: alerts };
    return u;
  }

  speechEndUtterance(): void {
    this.ended++;
  }

  speechReset(): void {
    this.resets++;
  }
}

// Two alerts for one result, most urgent first.
class TwoAlertSpeech implements SpeechEnginePort {
  speechResult(text: string, isFinal: boolean, timeSec: number): SpeechUpdate {
    const u: SpeechUpdate = {
      caption: caption(text, isFinal, 1),
      alerts: [alertFor(text, 'help', [300]), alertFor(text, 'me', [100])]
    };
    return u;
  }

  speechEndUtterance(): void {}

  speechReset(): void {}
}

class FakeKeywordAlerts implements KeywordAlertPort {
  patterns: string[] = [];
  notes: string[] = [];

  playPattern(pattern: number[]): void {
    this.patterns.push(pattern.join('/'));
  }

  notifyKeyword(label: string, timeMs: number): void {
    this.notes.push(label + '@' + timeMs);
  }
}

class Rig {
  engine: FakeSpeech = new FakeSpeech();
  alerts: FakeKeywordAlerts = new FakeKeywordAlerts();
  settings: AppSettings = new AppSettings();
  heard: string[] = [];
  board: CaptionBoard = new CaptionBoard(this.engine, this.alerts, this.settings);

  constructor() {
    this.board.onKeyword = (label: string, text: string, timeMs: number): void => {
      this.heard.push(label + ':' + text);
    };
  }
}

export default function captionBoardTest() {
  describe('captionBoard', () => {
    it('an_interim_result_is_the_live_caption_and_a_final_one_is_kept', 0, () => {
      const rig = new Rig();
      rig.board.onResult('good mor', false, 1000, 1);
      expect(rig.board.live()?.text).assertEqual('good mor');
      expect(rig.board.captions().length).assertEqual(0);
      rig.board.onResult('good morning', true, 2000, 2);
      expect(rig.board.live() === undefined).assertTrue();
      expect(rig.board.captions().length).assertEqual(1);
      expect(rig.board.captions()[0].text).assertEqual('good morning');
    });

    it('empty_text_is_ignored', 0, () => {
      const rig = new Rig();
      rig.board.onResult('', true, 1000, 1);
      expect(rig.board.captions().length).assertEqual(0);
      expect(rig.board.live() === undefined).assertTrue();
    });

    it('an_english_keyword_vibrates_with_its_pattern_notifies_and_is_reported', 0, () => {
      const rig = new Rig();
      rig.board.onResult('please help me', true, 5000, 1);
      expect(rig.alerts.patterns.join(',')).assertEqual('100/50/100');
      expect(rig.alerts.notes.join(',')).assertEqual('help@5000');
      expect(rig.heard.join(',')).assertEqual('help:help');
      expect(rig.board.lastAlert()).assertEqual('help: "help"');
    });

    it('the_vibrate_and_notify_switches_apply_to_keywords', 0, () => {
      const rig = new Rig();
      rig.settings.vibrate = false;
      rig.settings.notify = false;
      rig.board.onResult('help', true, 5000, 1);
      expect(rig.alerts.patterns.length).assertEqual(0);
      expect(rig.alerts.notes.length).assertEqual(0);
      expect(rig.heard.length).assertEqual(1); // still shown on screen
    });

    it('only_the_most_urgent_alert_vibrates', 0, () => {
      const rig = new Rig();
      const board = new CaptionBoard(new TwoAlertSpeech(), rig.alerts, rig.settings);
      board.onResult('help me', true, 1000, 1);
      expect(rig.alerts.patterns.length).assertEqual(1);
      expect(rig.alerts.patterns[0]).assertEqual('300');
    });

    it('at_most_one_hundred_captions_are_kept_oldest_dropped_first', 0, () => {
      const rig = new Rig();
      for (let i = 0; i < 105; i++) {
        rig.board.onResult('line ' + i, true, i * 1000, i);
      }
      expect(rig.board.captions().length).assertEqual(100);
      expect(rig.board.captions()[0].text).assertEqual('line 5');
    });

    it('ending_an_utterance_drops_the_live_caption_and_clearing_resets_the_engine', 0, () => {
      const rig = new Rig();
      rig.board.onResult('half a sent', false, 1000, 1);
      rig.board.endUtterance();
      expect(rig.board.live() === undefined).assertTrue();
      expect(rig.engine.ended).assertEqual(1);
      rig.board.onResult('help', true, 2000, 2);
      rig.board.clear();
      expect(rig.board.captions().length).assertEqual(0);
      expect(rig.board.lastAlert()).assertEqual('');
      expect(rig.engine.resets).assertEqual(1);
    });
  });
}
