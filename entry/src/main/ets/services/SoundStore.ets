import { preferences } from '@kit.ArkData';
import { hilog } from '@kit.PerformanceAnalysisKit';
import { SoundCatalog } from '../model/SoundCatalog';
import { SavePort } from './SoundPipeline';

const LOG_DOMAIN: number = 0x3201;
const TAG: string = 'SoundStore';
const STORE_NAME: string = 'sounds';
const CATALOG_KEY: string = 'catalog';

// Persists the sound catalog on the device (fingerprints and metadata only, never audio).
export class SoundStore implements SavePort {
  private prefs: preferences.Preferences | undefined;
  private current: SoundCatalog;

  private constructor(prefs: preferences.Preferences | undefined, catalog: SoundCatalog) {
    this.prefs = prefs;
    this.current = catalog;
  }

  // Never rejects: if storage cannot be read the app starts with an empty history.
  static async open(context: Context): Promise<SoundStore> {
    try {
      const prefs = await preferences.getPreferences(context, STORE_NAME);
      const raw = await prefs.get(CATALOG_KEY, '');
      const catalog = typeof raw === 'string' ? SoundCatalog.parse(raw) : new SoundCatalog([]);
      return new SoundStore(prefs, catalog);
    } catch (e) {
      hilog.error(LOG_DOMAIN, TAG, 'Failed to open sound store: %{public}s', JSON.stringify(e));
      return new SoundStore(undefined, new SoundCatalog([]));
    }
  }

  catalog(): SoundCatalog {
    return this.current;
  }

  // Failures are logged, not thrown: a missed save must not interrupt listening.
  async save(): Promise<void> {
    if (this.prefs === undefined) {
      return;
    }
    try {
      await this.prefs.put(CATALOG_KEY, this.current.serialize());
      await this.prefs.flush();
    } catch (e) {
      hilog.error(LOG_DOMAIN, TAG, 'Failed to save sound store: %{public}s', JSON.stringify(e));
    }
  }
}
