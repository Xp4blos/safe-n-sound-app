import {
  createSpeech, speechAddRule, speechEndUtterance, speechRemoveRule, speechReset, speechResult, speechRules,
  SpeechUpdate as NativeSpeechUpdate, KeywordRuleInfo as NativeRuleInfo
} from 'libsafensound.so';
import { KeywordRule, SpeechAlertInfo, SpeechCaption, SpeechHighlight, SpeechUpdate } from '../model/SpeechModels';
import { SpeechEnginePort } from './CaptionBoard';

// Typed wrapper around the native keyword pipeline. Converts native results into the app's own model types.
export class NativeSpeech implements SpeechEnginePort {
  private constructor() {}

  // useDefaultRules: start with the built-in rules (help, watch out, look out, danger and their Chinese forms).
  static create(useDefaultRules: boolean, triggerOnPartial: boolean): NativeSpeech {
    createSpeech({ useDefaultRules: useDefaultRules, triggerOnPartial: triggerOnPartial });
    return new NativeSpeech();
  }

  speechResult(text: string, isFinal: boolean, timeSec: number): SpeechUpdate {
    return NativeSpeech.convert(speechResult(text, isFinal, timeSec));
  }

  speechEndUtterance(): void {
    speechEndUtterance();
  }

  speechReset(): void {
    speechReset();
  }

  rules(): KeywordRule[] {
    const out: KeywordRule[] = [];
    for (const r of speechRules()) {
      out.push(NativeSpeech.toRule(r));
    }
    return out;
  }

  // pattern: milliseconds on/off, or a preset name: 'sos' | 'rapid' | 'long' | 'double'.
  addRule(phrase: string, pattern: number[] | string, priority: number): void {
    speechAddRule({ phrase: phrase, pattern: pattern, priority: priority });
  }

  removeRule(phrase: string): boolean {
    return speechRemoveRule(phrase);
  }

  private static toRule(r: NativeRuleInfo): KeywordRule {
    const rule: KeywordRule = {
      phrase: r.phrase, label: r.label, pattern: r.pattern.slice(), priority: r.priority, cooldownS: r.cooldownS
    };
    return rule;
  }

  private static convert(u: NativeSpeechUpdate): SpeechUpdate {
    const highlights: SpeechHighlight[] = [];
    for (const h of u.caption.highlights) {
      const item: SpeechHighlight = { begin: h.begin, end: h.end, ruleId: h.ruleId, label: h.label };
      highlights.push(item);
    }
    const caption: SpeechCaption = {
      utteranceId: u.caption.utteranceId, isFinal: u.caption.isFinal, timeSec: u.caption.timeSec,
      text: u.caption.text, highlights: highlights
    };
    const alerts: SpeechAlertInfo[] = [];
    for (const a of u.alerts) {
      const info: SpeechAlertInfo = {
        ruleId: a.ruleId, label: a.label, phrase: a.phrase, timeSec: a.timeSec, begin: a.begin, end: a.end,
        pattern: a.pattern.slice()
      };
      alerts.push(info);
    }
    const update: SpeechUpdate = { caption: caption, alerts: alerts };
    return update;
  }
}
