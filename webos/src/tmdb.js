import { storage } from './storage.js';

/**
 * TMDB API client for webOS with proxy support.
 */
export class TmdbClient {
  getBaseUrl() {
    const cfg = storage.config;
    if (cfg.tmdbProxyEnabled && cfg.tmdbProxyUrl) {
      return cfg.tmdbProxyUrl.replace(/\/+$/, '') + '/3';
    }
    return 'https://api.themoviedb.org/3';
  }

  getImageBaseUrl() {
    return 'https://image.tmdb.org/t/p';
  }

  getImageUrl(path, size = 'w500') {
    if (!path) return '';
    if (path.startsWith('http')) return path;
    return `${this.getImageBaseUrl()}/${size}${path}`;
  }

  async request(endpoint, params = {}) {
    const base = this.getBaseUrl();
    const cfg = storage.config;
    const apiKey = cfg.tmdbApiKey || '8cf43ad9c085135b9479ad5cf6bbcbda';

    const url = new URL(`${base}${endpoint}`);
    url.searchParams.set('api_key', apiKey);
    url.searchParams.set('language', 'ru-RU');

    for (const [k, v] of Object.entries(params)) {
      url.searchParams.set(k, v);
    }

    const res = await fetch(url.toString());
    if (!res.ok) throw new Error(`TMDB error HTTP ${res.status}`);
    return await res.json();
  }

  async getTrending(mediaType = 'all', timeWindow = 'week') {
    return await this.request(`/trending/${mediaType}/${timeWindow}`);
  }

  async getPopularMovies() {
    return await this.request('/movie/popular');
  }

  async getPopularShows() {
    return await this.request('/tv/popular');
  }

  async getTopRated() {
    return await this.request('/movie/top_rated');
  }

  async search(query) {
    return await this.request('/search/multi', { query });
  }

  async getDetails(kind, id) {
    const endpoint = kind === 'tv' || kind === 'series' ? `/tv/${id}` : `/movie/${id}`;
    return await this.request(endpoint, { append_to_response: 'credits,similar' });
  }

  async getSeason(tvId, seasonNumber) {
    return await this.request(`/tv/${tvId}/season/${seasonNumber}`);
  }
}

export const tmdb = new TmdbClient();
