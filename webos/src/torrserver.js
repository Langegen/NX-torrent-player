import { storage } from './storage.js';

/**
 * TorrServer client for webOS.
 */
export class TorrServerClient {
  getBaseUrl() {
    let url = storage.config.torrServerUrl || 'http://192.168.1.10:8090';
    return url.replace(/\/+$/, '');
  }

  async testConnection(customUrl = null) {
    const base = (customUrl || this.getBaseUrl()).replace(/\/+$/, '');
    try {
      const res = await fetch(`${base}/echo`, { method: 'GET' });
      if (res.ok) {
        const text = await res.text();
        return { ok: true, version: text };
      }
      return { ok: false, error: `HTTP ${res.status}` };
    } catch (err) {
      return { ok: false, error: err.message || 'Cannot reach server' };
    }
  }

  async addTorrent(link, title = '') {
    const base = this.getBaseUrl();
    const payload = {
      action: 'add',
      link: link,
      title: title,
      save_to_db: true
    };

    const res = await fetch(`${base}/torrents`, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(payload)
    });

    if (!res.ok) throw new Error(`TorrServer upload failed: HTTP ${res.status}`);
    return await res.json();
  }

  async getTorrent(hash) {
    const base = this.getBaseUrl();
    const res = await fetch(`${base}/torrents`, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ action: 'get', hash })
    });
    if (!res.ok) throw new Error(`TorrServer get failed: HTTP ${res.status}`);
    return await res.json();
  }

  async listTorrents() {
    const base = this.getBaseUrl();
    const res = await fetch(`${base}/torrents`, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ action: 'list' })
    });
    if (!res.ok) throw new Error(`TorrServer list failed: HTTP ${res.status}`);
    return await res.json();
  }

  getStreamUrl(hashOrMagnet, fileIndex = 0) {
    const base = this.getBaseUrl();
    const linkParam = encodeURIComponent(hashOrMagnet);
    return `${base}/stream?link=${linkParam}&index=${fileIndex}&play`;
  }
}

export const torrserver = new TorrServerClient();
