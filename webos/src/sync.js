import { storage } from './storage.js';

/**
 * Cloud Sync client for webOS (WebDAV & Google Drive).
 */
export class CloudSync {
  constructor() {
    this.isSyncing = false;
  }

  // --- WebDAV ---
  async syncWebdav() {
    const cfg = storage.config;
    if (!cfg.syncWebdavUrl || !cfg.syncWebdavUser || !cfg.syncWebdavPass) {
      throw new Error('WebDAV credentials not set');
    }

    let baseUrl = cfg.syncWebdavUrl.replace(/\/+$/, '');
    if (!baseUrl.startsWith('http')) baseUrl = 'https://' + baseUrl;
    const folderUrl = `${baseUrl}/NX-torrent-player`;

    const authHeader = 'Basic ' + btoa(`${cfg.syncWebdavUser}:${cfg.syncWebdavPass}`);

    // Ensure folder
    try {
      await fetch(folderUrl, {
        method: 'MKCOL',
        headers: { 'Authorization': authHeader }
      });
    } catch {
      // Ignore if folder exists
    }

    // Pull remote library
    try {
      const libRes = await fetch(`${folderUrl}/library.json`, {
        method: 'GET',
        headers: { 'Authorization': authHeader }
      });
      if (libRes.ok) {
        const remoteLib = await libRes.json();
        storage.mergeLibrary(remoteLib);
      }
    } catch (e) {
      console.warn('[sync] failed to pull remote library', e);
    }

    // Pull remote history
    try {
      const histRes = await fetch(`${folderUrl}/history.json`, {
        method: 'GET',
        headers: { 'Authorization': authHeader }
      });
      if (histRes.ok) {
        const remoteHist = await histRes.json();
        storage.mergeHistory(remoteHist);
      }
    } catch (e) {
      console.warn('[sync] failed to pull remote history', e);
    }

    // Push local merged data
    const localLib = storage.loadLibrary();
    const localHist = storage.loadHistory();

    const pushLib = await fetch(`${folderUrl}/library.json`, {
      method: 'PUT',
      headers: {
        'Authorization': authHeader,
        'Content-Type': 'application/json'
      },
      body: JSON.stringify(localLib, null, 2)
    });
    if (!pushLib.ok) throw new Error(`Failed to upload library (HTTP ${pushLib.status})`);

    const pushHist = await fetch(`${folderUrl}/history.json`, {
      method: 'PUT',
      headers: {
        'Authorization': authHeader,
        'Content-Type': 'application/json'
      },
      body: JSON.stringify(localHist, null, 2)
    });
    if (!pushHist.ok) throw new Error(`Failed to upload history (HTTP ${pushHist.status})`);
  }

  // --- Google Drive ---
  async getGoogleAccessToken() {
    const cfg = storage.config;
    if (!cfg.syncGdriveRefreshToken) throw new Error('Google Drive is not linked');

    const clientId = '924822003714-3knpl8eb079c65691lqf253v5q29b9eb.apps.googleusercontent.com';
    const clientSecret = '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';

    const params = new URLSearchParams({
      client_id: clientId,
      client_secret: clientSecret,
      refresh_token: cfg.syncGdriveRefreshToken,
      grant_type: 'refresh_token'
    });

    const res = await fetch('https://oauth2.googleapis.com/token', {
      method: 'POST',
      headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
      body: params.toString()
    });

    if (!res.ok) throw new Error(`Google token refresh failed: ${res.status}`);
    const data = await res.json();
    return data.access_token;
  }

  async syncGoogleDrive() {
    const token = await this.getGoogleAccessToken();
    const headers = { 'Authorization': `Bearer ${token}` };

    // Find files in appDataFolder
    const listRes = await fetch('https://www.googleapis.com/drive/v3/files?spaces=appDataFolder&fields=files(id,name)', {
      headers
    });
    if (!listRes.ok) throw new Error(`Failed to list appDataFolder: ${listRes.status}`);
    const { files = [] } = await listRes.json();

    const libFile = files.find(f => f.name === 'nx_library.json');
    const histFile = files.find(f => f.name === 'nx_history.json');

    // Pull library
    if (libFile) {
      try {
        const r = await fetch(`https://www.googleapis.com/drive/v3/files/${libFile.id}?alt=media`, { headers });
        if (r.ok) storage.mergeLibrary(await r.json());
      } catch (e) {
        console.warn('[sync] failed to pull gdrive library', e);
      }
    }

    // Pull history
    if (histFile) {
      try {
        const r = await fetch(`https://www.googleapis.com/drive/v3/files/${histFile.id}?alt=media`, { headers });
        if (r.ok) storage.mergeHistory(await r.json());
      } catch (e) {
        console.warn('[sync] failed to pull gdrive history', e);
      }
    }

    // Push library
    const localLib = storage.loadLibrary();
    const libContent = JSON.stringify(localLib, null, 2);
    if (libFile) {
      await fetch(`https://www.googleapis.com/upload/drive/v3/files/${libFile.id}?uploadType=media`, {
        method: 'PATCH',
        headers: { ...headers, 'Content-Type': 'application/json' },
        body: libContent
      });
    } else {
      const boundary = 'NX_SYNC_BOUNDARY_JS_123';
      const multipart = `--${boundary}\r\nContent-Type: application/json; charset=UTF-8\r\n\r\n` +
        JSON.stringify({ name: 'nx_library.json', parents: ['appDataFolder'] }) +
        `\r\n--${boundary}\r\nContent-Type: application/json\r\n\r\n` +
        libContent + `\r\n--${boundary}--`;
      await fetch('https://www.googleapis.com/upload/drive/v3/files?uploadType=multipart', {
        method: 'POST',
        headers: { ...headers, 'Content-Type': `multipart/related; boundary=${boundary}` },
        body: multipart
      });
    }

    // Push history
    const localHist = storage.loadHistory();
    const histContent = JSON.stringify(localHist, null, 2);
    if (histFile) {
      await fetch(`https://www.googleapis.com/upload/drive/v3/files/${histFile.id}?uploadType=media`, {
        method: 'PATCH',
        headers: { ...headers, 'Content-Type': 'application/json' },
        body: histContent
      });
    } else {
      const boundary = 'NX_SYNC_BOUNDARY_JS_456';
      const multipart = `--${boundary}\r\nContent-Type: application/json; charset=UTF-8\r\n\r\n` +
        JSON.stringify({ name: 'nx_history.json', parents: ['appDataFolder'] }) +
        `\r\n--${boundary}\r\nContent-Type: application/json\r\n\r\n` +
        histContent + `\r\n--${boundary}--`;
      await fetch('https://www.googleapis.com/upload/drive/v3/files?uploadType=multipart', {
        method: 'POST',
        headers: { ...headers, 'Content-Type': `multipart/related; boundary=${boundary}` },
        body: multipart
      });
    }
  }

  async syncNow() {
    if (this.isSyncing) return;
    this.isSyncing = true;
    const cfg = storage.config;

    try {
      if (cfg.syncProvider === 'webdav') {
        await this.syncWebdav();
      } else if (cfg.syncProvider === 'gdrive') {
        await this.syncGoogleDrive();
      } else {
        return;
      }

      storage.saveConfig({
        syncLastTime: Math.floor(Date.now() / 1000),
        syncLastError: ''
      });
      window.dispatchEvent(new CustomEvent('sync:complete', { detail: { success: true } }));
    } catch (err) {
      console.error('[sync] error:', err);
      storage.saveConfig({
        syncLastError: err.message || String(err)
      });
      window.dispatchEvent(new CustomEvent('sync:complete', { detail: { success: false, error: err.message } }));
    } finally {
      this.isSyncing = false;
    }
  }

  triggerAutoSync() {
    if (storage.config.syncAuto && storage.config.syncProvider !== 'none') {
      this.syncNow();
    }
  }
}

export const sync = new CloudSync();
