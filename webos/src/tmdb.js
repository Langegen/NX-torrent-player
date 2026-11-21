import { storage } from './storage.js';

/**
 * TMDB API client for webOS with proxy support and catalog discovery.
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
      if (v !== undefined && v !== null && v !== '') {
        url.searchParams.set(k, v);
      }
    }

    const res = await fetch(url.toString());
    if (!res.ok) throw new Error(`TMDB error HTTP ${res.status}`);
    return await res.json();
  }

  async getTrending(mediaType = 'all', timeWindow = 'week') {
    return await this.request(`/trending/${mediaType}/${timeWindow}`);
  }

  async getPopularMovies(page = 1) {
    return await this.request('/movie/popular', { page });
  }

  async getPopularShows(page = 1) {
    return await this.request('/tv/popular', { page });
  }

  async getTopRatedMovies(page = 1) {
    return await this.request('/movie/top_rated', { page });
  }

  async getTopRatedShows(page = 1) {
    return await this.request('/tv/top_rated', { page });
  }

  async getMoviesByGenre(genreId, page = 1) {
    const params = { page, sort_by: 'popularity.desc' };
    if (genreId) params.with_genres = genreId;
    return await this.request('/discover/movie', params);
  }

  async getShowsByGenre(genreId, page = 1) {
    const params = { page, sort_by: 'popularity.desc' };
    if (genreId) params.with_genres = genreId;
    return await this.request('/discover/tv', params);
  }

  async getAnime(page = 1) {
    return await this.request('/discover/tv', {
      page,
      with_genres: '16',
      with_original_language: 'ja',
      sort_by: 'popularity.desc'
    });
  }

  async search(query) {
    if (!query || !query.trim()) return { results: [] };
    return await this.request('/search/multi', { query: query.trim() });
  }

  async getDetails(kind, id) {
    const endpoint = (kind === 'tv' || kind === 'series') ? `/tv/${id}` : `/movie/${id}`;
    return await this.request(endpoint, { append_to_response: 'credits,similar' });
  }

  async getSeason(tvId, seasonNumber) {
    return await this.request(`/tv/${tvId}/season/${seasonNumber}`);
  }
}

export const tmdb = new TmdbClient();
