import { nav } from './spatial_nav.js';
import { initMagicRemote } from './magic_remote.js';
import { storage } from './storage.js';
import { tmdb } from './tmdb.js';
import { torrserver } from './torrserver.js';
import { sync } from './sync.js';
import { player } from './player.js';

class App {
  constructor() {
    this.currentTab = 'home';
    this.init();
  }

  async init() {
    initMagicRemote();
    this.setupHeader();
    this.setupClock();
    await this.renderTab('home');

    // Trigger auto-sync on launch
    sync.triggerAutoSync();
  }

  setupClock() {
    const update = () => {
      const now = new Date();
      const hh = String(now.getHours()).padStart(2, '0');
      const mm = String(now.getMinutes()).padStart(2, '0');
      const el = document.querySelector('.header-clock');
      if (el) el.textContent = `${hh}:${mm}`;
    };
    update();
    setInterval(update, 1000);
  }

  setupHeader() {
    const tabs = document.querySelectorAll('.nav-tab');
    tabs.forEach(tab => {
      tab.onclick = () => {
        tabs.forEach(t => t.classList.remove('active'));
        tab.classList.add('active');
        this.currentTab = tab.dataset.tab;
        this.renderTab(this.currentTab);
      };
    });

    // Initial focus on first tab
    if (tabs.length > 0) nav.setFocus(tabs[0]);
  }

  async renderTab(tabName) {
    const content = document.querySelector('.app-content');
    content.innerHTML = '<div style="display:flex;justify-content:center;align-items:center;height:400px;"><div class="spinner"></div></div>';

    if (tabName === 'home') {
      await this.renderHome(content);
    } else if (tabName === 'bookmarks') {
      this.renderBookmarks(content);
    } else if (tabName === 'history') {
      this.renderHistory(content);
    } else if (tabName === 'torrents') {
      await this.renderTorrents(content);
    } else if (tabName === 'settings') {
      this.renderSettings(content);
    }
  }

  // --- HOME TAB ---
  async renderHome(container) {
    try {
      const [trending, popMovies, popShows] = await Promise.all([
        tmdb.getTrending('all', 'week'),
        tmdb.getPopularMovies(),
        tmdb.getPopularShows()
      ]);

      const heroItem = trending.results?.[0] || popMovies.results?.[0];
      container.innerHTML = '';

      // 1. Hero Banner
      if (heroItem) {
        const title = heroItem.title || heroItem.name;
        const banner = document.createElement('div');
        banner.className = 'hero-banner';
        const backdropUrl = tmdb.getImageUrl(heroItem.backdrop_path, 'original');
        banner.style.backgroundImage = `url("${backdropUrl}")`;
        banner.innerHTML = `
          <div class="hero-overlay"></div>
          <div class="hero-content">
            <h1 class="hero-title">${title}</h1>
            <div class="hero-meta">
              <span class="rating-badge">★ ${(heroItem.vote_average || 0).toFixed(1)}</span>
              <span>${(heroItem.release_date || heroItem.first_air_date || '').substring(0, 4)}</span>
              <span>${heroItem.media_type === 'tv' ? 'Сериал' : 'Фильм'}</span>
            </div>
            <p class="hero-desc">${heroItem.overview || ''}</p>
            <div class="hero-buttons">
              <button class="btn-primary btn-play focusable" tabindex="0">▶ Смотреть</button>
              <button class="btn-secondary btn-details focusable" tabindex="0">ℹ Карточка</button>
            </div>
          </div>
        `;

        banner.querySelector('.btn-play').onclick = () => this.openDetails(heroItem);
        banner.querySelector('.btn-details').onclick = () => this.openDetails(heroItem);
        container.appendChild(banner);
      }

      // 2. Continue Watching (from history)
      const hist = storage.loadHistory();
      if (hist.history && hist.history.length > 0) {
        this.renderHistoryRow(container, 'Продолжить просмотр', hist.history.slice(0, 15));
      }

      // 3. Trending Row
      if (trending.results) {
        this.renderMediaRow(container, 'В тренде', trending.results);
      }

      // 4. Popular Movies
      if (popMovies.results) {
        this.renderMediaRow(container, 'Популярные фильмы', popMovies.results);
      }

      // 5. Popular Shows
      if (popShows.results) {
        this.renderMediaRow(container, 'Популярные сериалы', popShows.results);
      }

      // Set focus to hero button
      const firstBtn = container.querySelector('.hero-buttons .btn-primary');
      if (firstBtn) nav.setFocus(firstBtn);

    } catch (err) {
      container.innerHTML = `<div style="padding: 60px; color: #ff6b6b;">Ошибка загрузки каталога: ${err.message}</div>`;
    }
  }

  renderMediaRow(container, title, items) {
    const row = document.createElement('div');
    row.className = 'category-row';
    row.innerHTML = `<h2 class="category-title">${title}</h2><div class="cards-strip"></div>`;
    const strip = row.querySelector('.cards-strip');

    items.forEach(item => {
      const card = this.createMediaCard(item);
      strip.appendChild(card);
    });

    container.appendChild(row);
  }

  renderHistoryRow(container, title, items) {
    const row = document.createElement('div');
    row.className = 'category-row';
    row.innerHTML = `<h2 class="category-title">${title}</h2><div class="cards-strip"></div>`;
    const strip = row.querySelector('.cards-strip');

    items.forEach(item => {
      const card = document.createElement('div');
      card.className = 'movie-card focusable';
      card.tabIndex = 0;
      const pct = (item.durationSec > 0) ? ((item.positionSec / item.durationSec) * 100).toFixed(0) : 0;
      card.innerHTML = `
        <img class="movie-poster" src="${item.posterPath || 'icon.png'}" alt="" />
        <div class="card-info">
          <div class="card-title">${item.title}</div>
          <div class="card-progress"><div class="card-progress-bar" style="width:${pct}%"></div></div>
        </div>
      `;
      card.onclick = () => {
        player.play({
          title: item.title,
          streamUrl: item.source,
          resumePos: item.positionSec,
          item: { tmdbId: item.tmdbId, title: item.title },
          season: item.season || 0,
          episode: item.episode || 0
        });
      };
      strip.appendChild(card);
    });

    container.appendChild(row);
  }

  createMediaCard(item) {
    const card = document.createElement('div');
    card.className = 'movie-card focusable';
    card.tabIndex = 0;

    const posterUrl = tmdb.getImageUrl(item.poster_path, 'w500');
    const title = item.title || item.name;
    const rating = (item.vote_average || 0).toFixed(1);

    card.innerHTML = `
      <img class="movie-poster" src="${posterUrl || 'icon.png'}" alt="" />
      ${item.vote_average ? `<div class="card-rating">★ ${rating}</div>` : ''}
      <div class="card-info">
        <div class="card-title">${title}</div>
      </div>
    `;

    card.onclick = () => this.openDetails(item);
    return card;
  }

  // --- DETAILS MODAL ---
  async openDetails(item) {
    const kind = item.media_type || (item.first_air_date ? 'tv' : 'movie');
    const id = item.id || item.tmdbId;

    let details = item;
    try {
      if (id) {
        details = await tmdb.getDetails(kind, id);
      }
    } catch (e) {
      console.warn('[app] details fetch failed, using snapshot', e);
    }

    const modal = document.createElement('div');
    modal.className = 'modal-overlay';
    const title = details.title || details.name;
    const poster = tmdb.getImageUrl(details.poster_path, 'w500');
    const isTv = kind === 'tv' || kind === 'series';

    modal.innerHTML = `
      <div class="modal-content">
        <div class="modal-left">
          <img class="modal-poster" src="${poster || 'icon.png'}" alt="" />
          <div style="display:flex;flex-direction:column;gap:12px;width:100%;">
            <button class="btn-primary btn-modal-play focusable" tabindex="0">▶ Смотреть через TorrServer</button>
            <button class="btn-secondary btn-modal-bookmark focusable" tabindex="0">★ В закладки</button>
            <button class="btn-secondary btn-modal-close focusable" tabindex="0">Закрыть</button>
          </div>
        </div>
        <div class="modal-right">
          <h1 class="modal-title">${title}</h1>
          <div class="hero-meta" style="margin-bottom:20px;">
            <span class="rating-badge">★ ${(details.vote_average || 0).toFixed(1)}</span>
            <span>${(details.release_date || details.first_air_date || '').substring(0, 4)}</span>
            <span>${isTv ? 'Сериал' : 'Фильм'}</span>
            ${details.genres ? `<span>${details.genres.map(g => g.name).join(', ')}</span>` : ''}
          </div>
          <p class="modal-overview">${details.overview || 'Описание отсутствует.'}</p>
          ${isTv ? `
            <h3 style="margin-bottom:14px;color:var(--accent-bright);">Сезоны и серии</h3>
            <div class="season-tabs">
              ${(details.seasons || []).filter(s => s.season_number > 0).map((s, idx) => `
                <button class="season-btn focusable ${idx === 0 ? 'focused' : ''}" data-season="${s.season_number}" tabindex="0">Сезон ${s.season_number}</button>
              `).join('')}
            </div>
            <div class="episodes-list"></div>
          ` : ''}
        </div>
      </div>
    `;

    document.body.appendChild(modal);

    const closeModal = () => {
      modal.remove();
      return true;
    };

    modal.querySelector('.btn-modal-close').onclick = closeModal;
    modal.querySelector('.btn-modal-bookmark').onclick = () => {
      storage.recordLibraryProgress({ tmdbId: id, title, kind, posterUrl: poster }, 0, 0, 0, 0, false, '', 0);
      alert('Добавлено в закладки!');
    };

    modal.querySelector('.btn-modal-play').onclick = async () => {
      // Prompt or query TorrServer torrents
      const magnet = prompt('Введите magnet-ссылку или info-hash для воспроизведения:', '');
      if (magnet) {
        try {
          const torrent = await torrserver.addTorrent(magnet, title);
          const hash = torrent.hash || magnet;
          const streamUrl = torrserver.getStreamUrl(hash, 0);
          closeModal();
          player.play({
            title,
            streamUrl,
            item: { tmdbId: id, title, kind, posterUrl: poster }
          });
        } catch (err) {
          alert('Ошибка TorrServer: ' + err.message);
        }
      }
    };

    if (isTv && details.seasons) {
      const loadSeason = async (seasonNum) => {
        const epContainer = modal.querySelector('.episodes-list');
        epContainer.innerHTML = '<div class="spinner"></div>';
        try {
          const seasonData = await tmdb.getSeason(id, seasonNum);
          epContainer.innerHTML = '';
          (seasonData.episodes || []).forEach(ep => {
            const epEl = document.createElement('div');
            epEl.className = 'episode-item focusable';
            epEl.tabIndex = 0;
            epEl.innerHTML = `
              <div>
                <strong>Серия ${ep.episode_number}:</strong> ${ep.name}
              </div>
              <div style="color:var(--text-muted);font-size:16px;">${ep.runtime ? ep.runtime + ' мин' : ''}</div>
            `;
            epEl.onclick = async () => {
              const magnet = prompt(`Воспроизвести Сезон ${seasonNum}, Серия ${ep.episode_number}. Введите magnet-ссылку:`, '');
              if (magnet) {
                const streamUrl = torrserver.getStreamUrl(magnet, ep.episode_number - 1);
                closeModal();
                player.play({
                  title: `${title} - ${ep.name}`,
                  streamUrl,
                  item: { tmdbId: id, title, kind },
                  season: seasonNum,
                  episode: ep.episode_number
                });
              }
            };
            epContainer.appendChild(epEl);
          });
        } catch (e) {
          epContainer.innerHTML = `<div>Ошибка загрузки серий: ${e.message}</div>`;
        }
      };

      const seasonBtns = modal.querySelectorAll('.season-btn');
      seasonBtns.forEach(btn => {
        btn.onclick = () => {
          seasonBtns.forEach(b => b.classList.remove('active'));
          btn.classList.add('active');
          loadSeason(btn.dataset.season);
        };
      });

      if (seasonBtns.length > 0) {
        seasonBtns[0].click();
      }
    }

    nav.pushBackHandler(closeModal);
    nav.setFocus(modal.querySelector('.btn-modal-play'));
  }

  // --- BOOKMARKS TAB ---
  renderBookmarks(container) {
    const lib = storage.loadLibrary();
    container.innerHTML = '<div style="padding: 0 60px 40px 60px;"><h1 style="font-size:36px;margin-bottom:30px;">Мои закладки</h1></div>';

    if (!lib.entries || lib.entries.length === 0) {
      container.innerHTML += '<div style="padding: 0 60px; color: var(--text-muted);">В закладках пока пусто. Добавляйте фильмы и сериалы из каталога!</div>';
      return;
    }

    const watching = lib.entries.filter(e => e.status === 'watching');
    const planned = lib.entries.filter(e => e.status === 'planned');
    const completed = lib.entries.filter(e => e.status === 'completed');

    if (watching.length > 0) {
      this.renderMediaRow(container, 'Смотрю', watching.map(e => ({ ...e.item, title: e.item.title, poster_path: e.item.posterUrl })));
    }
    if (planned.length > 0) {
      this.renderMediaRow(container, 'Буду смотреть', planned.map(e => ({ ...e.item, title: e.item.title, poster_path: e.item.posterUrl })));
    }
    if (completed.length > 0) {
      this.renderMediaRow(container, 'Просмотрено', completed.map(e => ({ ...e.item, title: e.item.title, poster_path: e.item.posterUrl })));
    }

    const firstCard = container.querySelector('.movie-card');
    if (firstCard) nav.setFocus(firstCard);
  }

  // --- HISTORY TAB ---
  renderHistory(container) {
    const hist = storage.loadHistory();
    container.innerHTML = '<div style="padding: 0 60px 40px 60px;"><h1 style="font-size:36px;margin-bottom:30px;">История просмотров</h1></div>';

    if (!hist.history || hist.history.length === 0) {
      container.innerHTML += '<div style="padding: 0 60px; color: var(--text-muted);">История просмотров пуста.</div>';
      return;
    }

    this.renderHistoryRow(container, 'Недавно воспроизведенное', hist.history);
    const firstCard = container.querySelector('.movie-card');
    if (firstCard) nav.setFocus(firstCard);
  }

  // --- TORRENTS TAB ---
  async renderTorrents(container) {
    container.innerHTML = `
      <div style="padding: 0 60px 40px 60px;">
        <h1 style="font-size:36px;margin-bottom:20px;">TorrServer Раздачи</h1>
        <div style="display:flex;gap:16px;margin-bottom:30px;">
          <button class="btn-primary btn-add-magnet focusable" tabindex="0">+ Добавить magnet-ссылку</button>
          <button class="btn-secondary btn-refresh-torrents focusable" tabindex="0">Обновить</button>
        </div>
        <div class="torrents-list" style="display:flex;flex-direction:column;gap:14px;"></div>
      </div>
    `;

    const listEl = container.querySelector('.torrents-list');
    const refresh = async () => {
      listEl.innerHTML = '<div class="spinner"></div>';
      try {
        const torrents = await torrserver.listTorrents();
        listEl.innerHTML = '';
        if (!torrents || torrents.length === 0) {
          listEl.innerHTML = '<div style="color:var(--text-muted);">На сервере TorrServer нет активных торрентов.</div>';
          return;
        }

        torrents.forEach(t => {
          const el = document.createElement('div');
          el.className = 'episode-item focusable';
          el.tabIndex = 0;
          const sz = (t.torrent_size / (1024 * 1024 * 1024)).toFixed(2);
          el.innerHTML = `
            <div>
              <div style="font-size:22px;font-weight:700;">${t.title || t.name || 'Раздача'}</div>
              <div style="color:var(--text-muted);font-size:16px;margin-top:4px;">Размер: ${sz} ГБ | Пиры: ${t.stat?.connected_seeders || 0}</div>
            </div>
            <button class="btn-primary" style="padding:10px 20px;font-size:16px;">▶ Играть</button>
          `;
          el.onclick = () => {
            const streamUrl = torrserver.getStreamUrl(t.hash, 0);
            player.play({ title: t.title || 'Видео', streamUrl });
          };
          listEl.appendChild(el);
        });
      } catch (e) {
        listEl.innerHTML = `<div style="color:#ff6b6b;">Ошибка подключения к TorrServer: ${e.message}</div>`;
      }
    };

    container.querySelector('.btn-refresh-torrents').onclick = refresh;
    container.querySelector('.btn-add-magnet').onclick = async () => {
      const link = prompt('Введите magnet-ссылку или URL торрента:', '');
      if (link) {
        try {
          await torrserver.addTorrent(link);
          await refresh();
        } catch (e) {
          alert('Ошибка: ' + e.message);
        }
      }
    };

    await refresh();
    nav.setFocus(container.querySelector('.btn-add-magnet'));
  }

  // --- SETTINGS TAB ---
  renderSettings(container) {
    const cfg = storage.config;
    container.innerHTML = `
      <div class="settings-container">
        <h1 style="font-size:36px;margin-bottom:30px;">Настройки</h1>

        <!-- TorrServer -->
        <div class="settings-section">
          <div class="settings-header">TorrServer</div>
          <div class="settings-row">
            <div class="settings-label">Адрес сервера TorrServer</div>
            <input class="settings-input inp-torrserver focusable" tabindex="0" value="${cfg.torrServerUrl}" />
          </div>
          <div style="margin-top:16px;display:flex;gap:14px;">
            <button class="btn-secondary btn-test-torrserver focusable" tabindex="0">Проверить связь</button>
          </div>
        </div>

        <!-- Cloud Sync -->
        <div class="settings-section">
          <div class="settings-header">Облачная синхронизация</div>
          <div class="settings-row">
            <div class="settings-label">Облачный сервис</div>
            <select class="settings-input sel-sync-provider focusable" tabindex="0">
              <option value="none" ${cfg.syncProvider === 'none' ? 'selected' : ''}>Отключено</option>
              <option value="webdav" ${cfg.syncProvider === 'webdav' ? 'selected' : ''}>WebDAV (Яндекс Диск, Mail.ru)</option>
              <option value="gdrive" ${cfg.syncProvider === 'gdrive' ? 'selected' : ''}>Google Drive</option>
            </select>
          </div>
          <div class="settings-row">
            <div class="settings-label">Автоматическая синхронизация</div>
            <input type="checkbox" class="chk-auto-sync focusable" tabindex="0" ${cfg.syncAuto ? 'checked' : ''} style="width:24px;height:24px;" />
          </div>

          <div class="webdav-block" style="display:${cfg.syncProvider === 'webdav' ? 'block' : 'none'};margin-top:20px;">
            <div class="settings-row">
              <div class="settings-label">URL WebDAV сервера</div>
              <input class="settings-input inp-webdav-url focusable" tabindex="0" value="${cfg.syncWebdavUrl}" />
            </div>
            <div class="settings-row">
              <div class="settings-label">Логин / Email</div>
              <input class="settings-input inp-webdav-user focusable" tabindex="0" value="${cfg.syncWebdavUser}" />
            </div>
            <div class="settings-row">
              <div class="settings-label">Пароль приложения</div>
              <input type="password" class="settings-input inp-webdav-pass focusable" tabindex="0" value="${cfg.syncWebdavPass}" />
            </div>
          </div>

          <div style="margin-top:24px;display:flex;gap:16px;">
            <button class="btn-primary btn-sync-now focusable" tabindex="0">Синхронизировать сейчас</button>
            <button class="btn-secondary btn-save-settings focusable" tabindex="0">Сохранить настройки</button>
          </div>
        </div>
      </div>
    `;

    const inpTorr = container.querySelector('.inp-torrserver');
    const selProv = container.querySelector('.sel-sync-provider');
    const chkAuto = container.querySelector('.chk-auto-sync');
    const inpUrl = container.querySelector('.inp-webdav-url');
    const inpUser = container.querySelector('.inp-webdav-user');
    const inpPass = container.querySelector('.inp-webdav-pass');
    const webdavBlock = container.querySelector('.webdav-block');

    selProv.onchange = () => {
      webdavBlock.style.display = selProv.value === 'webdav' ? 'block' : 'none';
    };

    container.querySelector('.btn-test-torrserver').onclick = async () => {
      const res = await torrserver.testConnection(inpTorr.value);
      alert(res.ok ? 'Подключение успешно! Версия: ' + res.version : 'Ошибка: ' + res.error);
    };

    container.querySelector('.btn-save-settings').onclick = () => {
      storage.saveConfig({
        torrServerUrl: inpTorr.value.trim(),
        syncProvider: selProv.value,
        syncAuto: chkAuto.checked,
        syncWebdavUrl: inpUrl.value.trim(),
        syncWebdavUser: inpUser.value.trim(),
        syncWebdavPass: inpPass.value.trim()
      });
      alert('Настройки сохранены!');
    };

    container.querySelector('.btn-sync-now').onclick = async () => {
      try {
        await sync.syncNow();
        alert('Синхронизация успешно завершена!');
      } catch (e) {
        alert('Ошибка синхронизации: ' + e.message);
      }
    };

    nav.setFocus(inpTorr);
  }
}

// Start application on DOM ready
window.addEventListener('DOMContentLoaded', () => {
  new App();
});
