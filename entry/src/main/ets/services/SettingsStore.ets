import { preferences } from '@kit.ArkData';
import { hilog } from '@kit.PerformanceAnalysisKit';
import { AppSettings } from '../model/AppSettings';
import { KeywordRule } from '../model/SpeechModels';

const LOG_DOMAIN: number = 0x3201;
const TAG: string = 'SettingsStore';
const STORE_NAME: string = 'settings';
const SETTINGS_KEY: string = 'app';
const RULES_KEY: string = 'keywordRules';

// Persists the settings and the user's keyword rules. Never throws: unreadable storage means defaults.
export class SettingsStore {
  private prefs: preferences.Preferences | undefined;
  readonly settings: AppSettings;
  private rules: KeywordRule[] | undefined; // undefined: the user never changed the keywords (built-in rules apply)

  private constructor(prefs: preferences.Preferences | undefined, settings: AppSettings, rules: KeywordRule[] | undefined) {
    this.prefs = prefs;
    this.settings = settings;
    this.rules = rules;
  }

  static async open(context: Context): Promise<SettingsStore> {
    try {
      const prefs = await preferences.getPreferences(context, STORE_NAME);
      const rawSettings = await prefs.get(SETTINGS_KEY, '');
      const settings = typeof rawSettings === 'string' && rawSettings !== '' ? AppSettings.parse(rawSettings) :
        new AppSettings();
      const rawRules = await prefs.get(RULES_KEY, '');
      const rules = typeof rawRules === 'string' && rawRules !== '' ? SettingsStore.parseRules(rawRules) : undefined;
      return new SettingsStore(prefs, settings, rules);
    } catch (e) {
      hilog.error(LOG_DOMAIN, TAG, 'Failed to open settings: %{public}s', JSON.stringify(e));
      return new SettingsStore(undefined, new AppSettings(), undefined);
    }
  }

  // The saved keyword rules, or undefined when the built-in ones apply.
  savedRules(): KeywordRule[] | undefined {
    return this.rules;
  }

  async saveSettings(): Promise<void> {
    await this.put(SETTINGS_KEY, this.settings.serialize());
  }

  async saveRules(rules: KeywordRule[]): Promise<void> {
    this.rules = rules;
    await this.put(RULES_KEY, JSON.stringify(rules));
  }

  async resetRules(): Promise<void> {
    this.rules = undefined;
    await this.put(RULES_KEY, '');
  }

  private async put(key: string, value: string): Promise<void> {
    if (this.prefs === undefined) {
      return;
    }
    try {
      await this.prefs.put(key, value);
      await this.prefs.flush();
    } catch (e) {
      hilog.error(LOG_DOMAIN, TAG, 'Failed to save %{public}s: %{public}s', key, JSON.stringify(e));
    }
  }

  private static parseRules(json: string): KeywordRule[] | undefined {
    try {
      const parsed = JSON.parse(json) as Object;
      if (!(parsed instanceof Array)) {
        return undefined;
      }
      const out: KeywordRule[] = [];
      for (const item of parsed as Object[]) {
        const o = item as Record<string, Object>;
        if (typeof o['phrase'] !== 'string' || !(o['pattern'] instanceof Array)) {
          continue;
        }
        const pattern: number[] = [];
        for (const v of o['pattern'] as Object[]) {
          if (typeof v === 'number') {
            pattern.push(v as number);
          }
        }
        const rule: KeywordRule = {
          phrase: o['phrase'] as string, label: typeof o['label'] === 'string' ? o['label'] as string : o['phrase'] as string,
          pattern: pattern, priority: typeof o['priority'] === 'number' ? o['priority'] as number : 0,
          cooldownS: typeof o['cooldownS'] === 'number' ? o['cooldownS'] as number : 2
        };
        out.push(rule);
      }
      return out;
    } catch (e) {
      return undefined;
    }
  }
}
