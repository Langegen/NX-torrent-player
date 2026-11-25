/**
 * Local & Cloud-synchronized data store for webOS.
 * Preserves 100% binary/JSON compatibility with Switch and Android versions.
 */

const STORAGE_KEYS = {
  CONFIG: 'nx_config',
  LIBRARY: 'nx_library',
  HISTORY: 'nx_history'
};

const DEFAULT_CONFIG = {
  torrServerUrl: 'http://192.168.1.10:8090',
  tmdbApiKey: '8cf43ad9c085135b9479ad5cf6bbcbda',
  tmdbProxyUrl: '',
  tmdbProxyEnabled: true,
  syncProvider: 'none', // 'none' | 'gdrive' | 'webdav'
  syncAuto: true,
  syncLastTime: 0,
  syncLastError: '',
  syncWebdavUrl: 'https://webdav.yandex.ru',
  syncWebdavUser: '',
  syncWebdavPass: '',
  syncGdriveRefreshToken: '',
  syncGdriveUserEmail: ''
};

class Storage {
  constructor() {
    this.config = this.loadConfig();
    this.library = this.loadLibrary();
    this.history = this.loadHistory();
  }

  loadConfig() {
    try {
      const data = localStorage.getItem(STORAGE_KEYS.CONFIG);
      return data ? { ...DEFAULT_CONFIG, ...JSON.parse(data) } : { ...DEFAULT_CONFIG };
    } catch {
      return { ...DEFAULT_CONFIG };
    }
  }

  saveConfig(newConfig) {
    this.config = { ...this.config, ...newConfig };
    localStorage.setItem(STORAGE_KEYS.CONFIG, JSON.stringify(this.config));
  }

  loadLibrary() {
    try {
      const data = localStorage.getItem(STORAGE_KEYS.LIBRARY);
      return data ? JSON.parse(data) : { entries: [], folders: [] };
    } catch {
      return { entries: [], folders: [] };
    }
  }

  saveLibrary(lib) {
    this.library = lib;
    localStorage.setItem(STORAGE_KEYS.LIBRARY, JSON.stringify(lib));
  }

  loadHistory() {
    try {
      const data = localStorage.getItem(STORAGE_KEYS.HISTORY);
      return data ? JSON.parse(data) : { history: [] };
    } catch {
      return { history: [] };
    }
  }

  saveHistory(hist) {
    this.history = hist;
    localStorage.setItem(STORAGE_KEYS.HISTORY, JSON.stringify(hist));
  }

  get libraryEntries() {
    const lib = this.loadLibrary();
    return Array.isArray(lib) ? lib : (Array.isArray(lib.entries) ? lib.entries : []);
  }

  get historyList() {
    const hist = this.loadHistory();
    return Array.isArray(hist) ? hist : (Array.isArray(hist.history) ? hist.history : []);
  }

  recordHistory(item) {
    const hist = this.loadHistory();
    const now = Math.floor(Date.now() / 1000);
    item.lastWatched = item.lastWatched || now;

    const id = item.id || (item.source + '#' + (item.fileIndex >= 0 ? item.fileIndex : '0'));
    item.id = id;

    const idx = hist.history.findIndex(x => x.id === id || (x.source === item.source && x.fileIndex === item.fileIndex));
    if (idx >= 0) {
      hist.history[idx] = { ...hist.history[idx], ...item };
    } else {
      hist.history.unshift(item);
    }

    hist.history.sort((a, b) => (b.lastWatched || 0) - (a.lastWatched || 0));
    if (hist.history.length > 100) hist.history.length = 100;

    this.saveHistory(hist);
  }

  recordLibraryProgress(item, season, episode, pos, dur, finished, source, fileIndex) {
    const lib = this.loadLibrary();
    const key = item.tmdbId ? `${item.kind || 'movie'}:${item.tmdbId}` : item.title;
    let entry = lib.entries.find(e => (e.item?.tmdbId && e.item.tmdbId === item.tmdbId) || e.item?.title === item.title);

    const now = Math.floor(Date.now() / 1000);

    if (!entry) {
      entry = {
        item: { ...item },
        status: 'watching',
        added: now,
        changed: now,
        pos: 0,
        dur: 0,
        done: false,
        episodes: {},
        watchedAt: now,
        folders: []
      };
      lib.entries.push(entry);
    }

    entry.watchedAt = now;
    entry.source = source;
    entry.fileIndex = fileIndex;

    if (season > 0 && episode > 0) {
      entry.lastSeason = season;
      entry.lastEpisode = episode;
      entry.episodes = entry.episodes || {};
      const epKey = `${season}:${episode}`;
      entry.episodes[epKey] = {
        pos,
        dur,
        done: finished,
        at: now,
        source,
        fileIndex
      };
    } else {
      entry.pos = pos;
      entry.dur = dur;
      entry.done = finished;
    }

    this.saveLibrary(lib);
  }

  // Merges incoming library JSON from cloud sync
  mergeLibrary(incoming) {
    if (!incoming || !incoming.entries) return;
    const current = this.loadLibrary();

    for (const inEntry of incoming.entries) {
      const curEntry = current.entries.find(e =>
        (e.item?.tmdbId && inEntry.item?.tmdbId && e.item.tmdbId === inEntry.item.tmdbId) ||
        (e.item?.title && e.item.title === inEntry.item?.title)
      );

      if (!curEntry) {
        current.entries.push(inEntry);
        continue;
      }

      // Newer status wins
      if ((inEntry.changed || 0) > (curEntry.changed || 0)) {
        curEntry.status = inEntry.status;
        curEntry.changed = inEntry.changed;
      }

      // Merge episodes by newer timestamp
      if (inEntry.episodes) {
        curEntry.episodes = curEntry.episodes || {};
        for (const [epKey, epProg] of Object.entries(inEntry.episodes)) {
          const myEp = curEntry.episodes[epKey];
          if (!myEp || (epProg.at || 0) > (myEp.at || 0)) {
            curEntry.episodes[epKey] = epProg;
          }
        }
      }

      // Newer overall watch position
      if ((inEntry.watchedAt || 0) > (curEntry.watchedAt || 0)) {
        curEntry.watchedAt = inEntry.watchedAt;
        curEntry.lastSeason = inEntry.lastSeason;
        curEntry.lastEpisode = inEntry.lastEpisode;
        curEntry.pos = inEntry.pos;
        curEntry.dur = inEntry.dur;
        curEntry.done = inEntry.done;
        curEntry.source = inEntry.source;
        curEntry.fileIndex = inEntry.fileIndex;
      }
    }

    // Merge folders
    if (incoming.folders) {
      current.folders = Array.from(new Set([...(current.folders || []), ...incoming.folders]));
    }

    this.saveLibrary(current);
  }

  // Merges incoming history JSON from cloud sync
  mergeHistory(incoming) {
    if (!incoming || !incoming.history) return;
    const current = this.loadHistory();

    for (const inItem of incoming.history) {
      const curIdx = current.history.findIndex(x =>
        x.id === inItem.id ||
        (x.source === inItem.source && x.fileIndex === inItem.fileIndex)
      );

      if (curIdx === -1) {
        current.history.push(inItem);
      } else {
        if ((inItem.lastWatched || 0) > (current.history[curIdx].lastWatched || 0)) {
          current.history[curIdx] = inItem;
        }
      }
    }

    current.history.sort((a, b) => (b.lastWatched || 0) - (a.lastWatched || 0));
    if (current.history.length > 100) current.history.length = 100;

    this.saveHistory(current);
  }
}

export const storage = new Storage();
