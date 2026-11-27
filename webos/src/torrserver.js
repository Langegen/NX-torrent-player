import { storage } from './storage.js';

/**
 * TorrServer client for webOS.
 * All torrent streaming on webOS runs through TorrServer.
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
      return { ok: false, error: err.message || 'Сервер недоступен' };
    }
  }

  async addTorrent(link, title = '', poster = '', data = '') {
    const base = this.getBaseUrl();
    const payload = {
      action: 'add',
      link: link,
      title: title,
      poster: poster,
      data: data,
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

  async remTorrent(hash) {
    const base = this.getBaseUrl();
    const res = await fetch(`${base}/torrents`, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ action: 'rem', hash })
    });
    if (!res.ok) throw new Error(`TorrServer rem failed: HTTP ${res.status}`);
    return true;
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

  /**
   * Parse release name into quality, codec, dub and seeds (matching release.hpp in C++)
   */
  parseRelease(name, sizeBytes = 0, seeds = 0, peers = 0, magnet = '') {
    const n = name.toLowerCase();
    let quality = '1080p';
    if (n.includes('2160p') || n.includes('4k') || n.includes('uhd')) quality = '4K UHD';
    else if (n.includes('720p')) quality = '720p';
    else if (n.includes('480p') || n.includes('dvd') || n.includes('sd')) quality = 'SD';

    let codec = 'x264';
    if (n.includes('hevc') || n.includes('x265') || n.includes('h.265')) codec = 'HEVC / x265';
    else if (n.includes('av1')) codec = 'AV1';

    let hdr = '';
    if (n.includes('dolby vision') || n.includes('dovi') || n.includes(' dv ')) hdr = 'Dolby Vision';
    else if (n.includes('hdr10+')) hdr = 'HDR10+';
    else if (n.includes('hdr')) hdr = 'HDR';

    let audio = 'Дубляж';
    if (n.includes('lostfilm')) audio = 'LostFilm';
    else if (n.includes('hdrezka') || n.includes('rezka')) audio = 'HDRezka';
    else if (n.includes('кубик в кубе') || n.includes('kubik')) audio = 'Кубик в Кубе';
    else if (n.includes('original') || n.includes('eng')) audio = 'Оригинал';
    else if (n.includes('многоголосый') || n.includes('mvo')) audio = 'Многоголосый';

    const sizeGB = (sizeBytes / (1024 * 1024 * 1024)).toFixed(1);

    return {
      rawName: name,
      quality,
      codec,
      hdr,
      audio,
      sizeStr: sizeBytes > 0 ? `${sizeGB} GB` : '',
      seeds,
      peers,
      magnet,
      isBestChoice: quality === '1080p' && seeds > 5
    };
  }
}

export const torrserver = new TorrServerClient();
