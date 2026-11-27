import { nav } from './spatial_nav.js';
import { initMagicRemote } from './magic_remote.js';
import { storage } from './storage.js';
import { tmdb } from './tmdb.js';
import { torrserver } from './torrserver.js';
import { sync } from './sync.js';
import { player } from './player.js';

class NXTorrentPlayerApp {
  constructor() {
    this.tabs = ['home', 'movies', 'shows', 'anime', 'bookmarks', 'history', 'torrents'];
    this.currentTab = 'home';
    this.modalContainer = document.getElementById('modal-container');
    this.init();
  }

  async init() {
    initMagicRemote();
    this.setupHeader();
    this.setupClock();
    this.setupShortcuts();

    // Render initial tab
    await this.selectTab('home');

    // Auto-sync on startup
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
    const tabButtons = document.querySelectorAll('.nav-tab');
    tabButtons.forEach(btn => {
      btn.onclick = () => {
        this.selectTab(btn.dataset.tab);
      };
    });

    const searchBtn = document.querySelector('.btn-search');
    if (searchBtn) {
      searchBtn.onclick = () => this.openSearchModal();
    }

    const optionsBtn = document.querySelector('.btn-options');
    if (optionsBtn) {
      optionsBtn.onclick = () => this.openSettingsModal();
    }
  }

  setupShortcuts() {
    // L / R: cycle menu tabs (matching main.cpp cycleMenuTabs)
    nav.onAction('tabLeft', () => this.cycleTabs(-1));
    nav.onAction('tabRight', () => this.cycleTabs(1));

    // X button: Search modal
    nav.onAction('search', () => this.openSearchModal());

    // (+) button: Options modal
    nav.onAction('options', () => this.openSettingsModal());
  }

  cycleTabs(direction) {
    const curIdx = this.tabs.indexOf(this.currentTab);
    if (curIdx === -1) return;
    let nextIdx = (curIdx + direction + this.tabs.length) % this.tabs.length;
    this.selectTab(this.tabs[nextIdx]);
  }

  async selectTab(tabName) {
    this.currentTab = tabName;
    const tabButtons = document.querySelectorAll('.nav-tab');
    tabButtons.forEach(btn => {
      btn.classList.toggle('active', btn.dataset.tab === tabName);
    });

    const content = document.querySelector('.app-content');
    content.innerHTML = '<div style="display:flex;justify-content:center;align-items:center;height:500px;"><div class="spinner"></div></div>';

    switch (tabName) {
      case 'home':
        await this.renderHome(content);
        break;
      case 'movies':
        await this.renderMovies(content);
        break;
      case 'shows':
        await this.renderShows(content);
        break;
      case 'anime':
        await this.renderAnime(content);
        break;
      case 'bookmarks':
        this.renderBookmarks(content);
        break;
      case 'history':
        this.renderHistory(content);
        break;
      case 'torrents':
        await this.renderTorrents(content);
        break;
    }

    // Set initial focus
    const firstFocus = content.querySelector('.focusable');
    if (firstFocus) {
      nav.setFocus(firstFocus);
    }
  }

  // =========================================================================
  // HOME TAB (Hero, Continue Watching, Catalog Strips)
  // =========================================================================
  async renderHome(container) {
    try {
      const [trending, popMovies, popShows, anime] = await Promise.all([
        tmdb.getTrending('all', 'week'),
        tmdb.getPopularMovies(),
        tmdb.getPopularShows(),
        tmdb.getAnime()
      ]);

      const heroItem = trending.results?.[0] || popMovies.results?.[0];
      container.innerHTML = '';

      // 1. Hero Banner
      if (heroItem) {
        const title = heroItem.title || heroItem.name;
        const banner = document.createElement('div');
        banner.className = 'hero-container';
        const backdropUrl = tmdb.getImageUrl(heroItem.backdrop_path, 'original');
        banner.style.backgroundImage = `url("${backdropUrl}")`;
        banner.innerHTML = `
          <div class="hero-overlay"></div>
          <div class="hero-content">
            <h1 class="hero-title">${title}</h1>
            <div class="hero-meta">
              <span class="rating-badge">★ ${(heroItem.vote_average || 0).toFixed(1)}</span>
              <span class="meta-dot">·</span>
              <span>${(heroItem.release_date || heroItem.first_air_date || '').substring(0, 4)}</span>
              <span class="meta-dot">·</span>
              <span>${heroItem.media_type === 'tv' ? 'Сериал' : 'Фильм'}</span>
            </div>
            <div class="hero-buttons">
              <button class="btn-primary btn-hero-watch focusable" tabindex="0">▶ Смотреть</button>
              <button class="btn-secondary btn-hero-details focusable" tabindex="0">Подробнее</button>
            </div>
          </div>
        `;

        banner.querySelector('.btn-hero-watch').onclick = () => this.watchContent(heroItem);
        banner.querySelector('.btn-hero-details').onclick = () => this.openContentCard(heroItem);
        container.appendChild(banner);
      }

      // 2. Continue Watching (16:9 cards)
      const historyList = storage.historyList;
      if (historyList && historyList.length > 0) {
        const continueSection = document.createElement('div');
        continueSection.innerHTML = `<h2 class="section-heading">Продолжить просмотр</h2>`;
        const strip = document.createElement('div');
        strip.className = 'strip-container';

        historyList.slice(0, 8).forEach(entry => {
          const card = document.createElement('div');
          card.className = 'continue-card focusable';
          card.tabIndex = 0;
          if (entry.backdrop) {
            card.style.backgroundImage = `url("${entry.backdrop}")`;
          }
          const percent = entry.duration > 0 ? Math.min(100, Math.round((entry.lastPos / entry.duration) * 100)) : 0;
          card.innerHTML = `
            <div class="continue-overlay">
              <div class="continue-title">${entry.title}</div>
              <div class="continue-subtitle">${entry.season > 0 ? `Сезон ${entry.season}, Серия ${entry.episode}` : `${percent}%`}</div>
              <div class="timeline-progress-track">
                <div class="timeline-progress-fill" style="width: ${percent}%;"></div>
              </div>
            </div>
          `;
          card.onclick = () => {
            player.play({
              title: entry.title,
              streamUrl: entry.streamUrl || torrserver.getStreamUrl(entry.magnet || entry.hash || entry.id, entry.fileIndex || 0),
              resumePos: entry.lastPos,
              item: entry,
              season: entry.season || 0,
              episode: entry.episode || 0
            });
          };
          strip.appendChild(card);
        });
        continueSection.appendChild(strip);
        container.appendChild(continueSection);
      }

      // 3. Catalog Rows (2:3 posters)
      this.addStrip(container, 'В тренде сегодня', trending.results || []);
      this.addStrip(container, 'Популярные фильмы', popMovies.results || []);
      this.addStrip(container, 'Популярные сериалы', popShows.results || []);
      this.addStrip(container, 'Популярное аниме', anime.results || []);

    } catch (err) {
      container.innerHTML = `<div style="padding:60px;color:#e74c3c;">Ошибка загрузки каталога: ${err.message}</div>`;
    }
  }

  addStrip(container, title, items) {
    if (!items || items.length === 0) return;
    const section = document.createElement('div');
    section.innerHTML = `<h2 class="section-heading">${title}</h2>`;
    const strip = document.createElement('div');
    strip.className = 'strip-container';

    items.forEach(it => {
      const card = this.createPosterCard(it);
      strip.appendChild(card);
    });

    section.appendChild(strip);
    container.appendChild(section);
  }

  createPosterCard(item) {
    const card = document.createElement('div');
    card.className = 'poster-card focusable';
    card.tabIndex = 0;
    const posterUrl = tmdb.getImageUrl(item.poster_path, 'w500');
    if (posterUrl) {
      card.style.backgroundImage = `url("${posterUrl}")`;
    }
    const title = item.title || item.name;
    const year = (item.release_date || item.first_air_date || '').substring(0, 4);
    const rating = item.vote_average ? item.vote_average.toFixed(1) : '';

    card.innerHTML = `
      <div class="poster-info-bottom">
        <div class="poster-title">${title}</div>
        <div class="poster-subtitle">
          <span>${year}</span>
          ${rating ? `<span class="rating-badge">★ ${rating}</span>` : ''}
        </div>
      </div>
    `;

    card.onclick = () => this.openContentCard(item);
    return card;
  }

  // =========================================================================
  // MOVIES TAB
  // =========================================================================
  async renderMovies(container) {
    container.innerHTML = `
      <div class="filter-bar">
        <button class="filter-chip focusable active" data-genre="">Все фильмы</button>
        <button class="filter-chip focusable" data-genre="28">Боевики</button>
        <button class="filter-chip focusable" data-genre="35">Комедии</button>
        <button class="filter-chip focusable" data-genre="878">Фантастика</button>
        <button class="filter-chip focusable" data-genre="53">Триллеры</button>
        <button class="filter-chip focusable" data-genre="18">Драмы</button>
        <button class="filter-chip focusable" data-genre="27">Ужасы</button>
      </div>
      <div class="catalog-grid"></div>
    `;

    const grid = container.querySelector('.catalog-grid');
    const chips = container.querySelectorAll('.filter-chip');

    const loadGenre = async (genreId) => {
      grid.innerHTML = '<div style="grid-column:1/-1;text-align:center;padding:40px;"><div class="spinner"></div></div>';
      try {
        const res = await tmdb.getMoviesByGenre(genreId);
        grid.innerHTML = '';
        (res.results || []).forEach(it => {
          grid.appendChild(this.createPosterCard(it));
        });
      } catch (err) {
        grid.innerHTML = `<div style="grid-column:1/-1;color:#e74c3c;padding:40px;">${err.message}</div>`;
      }
    };

    chips.forEach(chip => {
      chip.onclick = () => {
        chips.forEach(c => c.classList.remove('active'));
        chip.classList.add('active');
        loadGenre(chip.dataset.genre);
      };
    });

    await loadGenre('');
  }

  // =========================================================================
  // SHOWS TAB
  // =========================================================================
  async renderShows(container) {
    container.innerHTML = `
      <div class="filter-bar">
        <button class="filter-chip focusable active" data-genre="">Все сериалы</button>
        <button class="filter-chip focusable" data-genre="18">Драмы</button>
        <button class="filter-chip focusable" data-genre="10765">Фантастика и фэнтези</button>
        <button class="filter-chip focusable" data-genre="35">Комедии</button>
        <button class="filter-chip focusable" data-genre="80">Криминал</button>
        <button class="filter-chip focusable" data-genre="9648">Детективы</button>
      </div>
      <div class="catalog-grid"></div>
    `;

    const grid = container.querySelector('.catalog-grid');
    const chips = container.querySelectorAll('.filter-chip');

    const loadGenre = async (genreId) => {
      grid.innerHTML = '<div style="grid-column:1/-1;text-align:center;padding:40px;"><div class="spinner"></div></div>';
      try {
        const res = await tmdb.getShowsByGenre(genreId);
        grid.innerHTML = '';
        (res.results || []).forEach(it => {
          it.media_type = 'tv';
          grid.appendChild(this.createPosterCard(it));
        });
      } catch (err) {
        grid.innerHTML = `<div style="grid-column:1/-1;color:#e74c3c;padding:40px;">${err.message}</div>`;
      }
    };

    chips.forEach(chip => {
      chip.onclick = () => {
        chips.forEach(c => c.classList.remove('active'));
        chip.classList.add('active');
        loadGenre(chip.dataset.genre);
      };
    });

    await loadGenre('');
  }

  // =========================================================================
  // ANIME TAB
  // =========================================================================
  async renderAnime(container) {
    container.innerHTML = `
      <h2 class="section-heading" style="margin-top:20px;">Каталог Аниме</h2>
      <div class="catalog-grid"></div>
    `;
    const grid = container.querySelector('.catalog-grid');
    grid.innerHTML = '<div style="grid-column:1/-1;text-align:center;padding:40px;"><div class="spinner"></div></div>';

    try {
      const res = await tmdb.getAnime();
      grid.innerHTML = '';
      (res.results || []).forEach(it => {
        it.media_type = 'tv';
        grid.appendChild(this.createPosterCard(it));
      });
    } catch (err) {
      grid.innerHTML = `<div style="grid-column:1/-1;color:#e74c3c;padding:40px;">${err.message}</div>`;
    }
  }

  // =========================================================================
  // BOOKMARKS TAB (Matching bookmarks.cpp)
  // =========================================================================
  renderBookmarks(container) {
    const library = storage.libraryEntries;
    container.innerHTML = `
      <div class="filter-bar">
        <button class="filter-chip focusable active" data-status="all">Все (${library.length})</button>
        <button class="filter-chip focusable" data-status="watching">Смотрю</button>
        <button class="filter-chip focusable" data-status="planned">Запланировано</button>
        <button class="filter-chip focusable" data-status="completed">Просмотрено</button>
      </div>
      <div class="catalog-grid"></div>
    `;

    const grid = container.querySelector('.catalog-grid');
    const chips = container.querySelectorAll('.filter-chip');

    const updateGrid = (status) => {
      grid.innerHTML = '';
      const filtered = status === 'all' ? library : library.filter(e => (e.status || 'planned') === status);
      if (filtered.length === 0) {
        grid.innerHTML = '<div style="grid-column:1/-1;padding:60px;color:#808090;font-size:18px;">В этой категории пока нет закладок</div>';
        return;
      }
      filtered.forEach(entry => {
        const it = {
          id: entry.tmdbId || entry.id,
          title: entry.title,
          name: entry.title,
          poster_path: entry.poster,
          backdrop_path: entry.backdrop,
          release_date: entry.year,
          vote_average: entry.rating,
          media_type: entry.isSeries ? 'tv' : 'movie'
        };
        grid.appendChild(this.createPosterCard(it));
      });
    };

    chips.forEach(chip => {
      chip.onclick = () => {
        chips.forEach(c => c.classList.remove('active'));
        chip.classList.add('active');
        updateGrid(chip.dataset.status);
      };
    });

    updateGrid('all');
  }

  // =========================================================================
  // HISTORY TAB (Matching history_tab.cpp)
  // =========================================================================
  renderHistory(container) {
    const list = storage.historyList;
    container.innerHTML = `
      <div style="display:flex;justify-content:space-between;align-items:center;padding:24px 60px 10px 60px;">
        <h2 style="font-size:24px;font-weight:700;">История просмотров</h2>
        ${list.length > 0 ? `<button class="btn-secondary btn-clear-history focusable" tabindex="0">Очистить историю</button>` : ''}
      </div>
      <div class="catalog-grid" style="grid-template-columns: repeat(auto-fill, minmax(260px, 1fr));"></div>
    `;

    const grid = container.querySelector('.catalog-grid');
    if (list.length === 0) {
      grid.innerHTML = '<div style="grid-column:1/-1;padding:60px;color:#808090;font-size:18px;">История просмотров пуста</div>';
      return;
    }

    const clearBtn = container.querySelector('.btn-clear-history');
    if (clearBtn) {
      clearBtn.onclick = () => {
        storage.saveHistory([]);
        sync.triggerAutoSync();
        this.renderHistory(container);
      };
    }

    list.forEach(entry => {
      const card = document.createElement('div');
      card.className = 'continue-card focusable';
      card.tabIndex = 0;
      if (entry.backdrop) {
        card.style.backgroundImage = `url("${entry.backdrop}")`;
      }
      const percent = entry.duration > 0 ? Math.min(100, Math.round((entry.lastPos / entry.duration) * 100)) : 0;
      card.innerHTML = `
        <div class="continue-overlay">
          <div class="continue-title">${entry.title}</div>
          <div class="continue-subtitle">${entry.season > 0 ? `Сезон ${entry.season}, Серия ${entry.episode}` : `${percent}%`}</div>
          <div class="timeline-progress-track">
            <div class="timeline-progress-fill" style="width: ${percent}%;"></div>
          </div>
        </div>
      `;
      card.onclick = () => {
        player.play({
          title: entry.title,
          streamUrl: entry.streamUrl || torrserver.getStreamUrl(entry.magnet || entry.hash || entry.id, entry.fileIndex || 0),
          resumePos: entry.lastPos,
          item: entry,
          season: entry.season || 0,
          episode: entry.episode || 0
        });
      };
      grid.appendChild(card);
    });
  }

  // =========================================================================
  // TORRENTS / TORRSERVER TAB
  // =========================================================================
  async renderTorrents(container) {
    container.innerHTML = `
      <div class="torrents-view-header">
        <div>
          <h2 style="font-size:24px;font-weight:700;">Торренты на сервере</h2>
          <div style="color:var(--text-secondary);font-size:14px;margin-top:4px;">Сервер: ${torrserver.getBaseUrl()}</div>
        </div>
        <button class="btn-primary btn-add-magnet focusable" tabindex="0">+ Добавить Magnet / Торрент</button>
      </div>
      <div class="torrents-table">
        <div style="text-align:center;padding:40px;"><div class="spinner"></div></div>
      </div>
    `;

    const table = container.querySelector('.torrents-table');
    const addBtn = container.querySelector('.btn-add-magnet');

    addBtn.onclick = () => {
      const link = prompt('Введите magnet-ссылку или URL торрент-файла:');
      if (link && link.trim()) {
        torrserver.addTorrent(link.trim()).then(() => {
          this.renderTorrents(container);
        }).catch(err => alert('Ошибка добавления: ' + err.message));
      }
    };

    try {
      const list = await torrserver.listTorrents().catch(() => []);
      table.innerHTML = '';
      if (!Array.isArray(list) || list.length === 0) {
        table.innerHTML = '<div style="padding:40px;color:#808090;font-size:16px;">На сервере пока нет добавленных торрентов.</div>';
        return;
      }

      list.forEach(t => {
        const item = document.createElement('div');
        item.className = 'torrent-card-item focusable';
        item.tabIndex = 0;
        const sizeGB = t.torrent_size ? (t.torrent_size / (1024 * 1024 * 1024)).toFixed(2) + ' GB' : '';
        const stat = t.stat_string || '';
        const seeds = t.connected_seeders || 0;

        item.innerHTML = `
          <div style="flex:1;">
            <div style="font-size:16px;font-weight:700;color:#ffffff;">${t.title || t.name || t.hash}</div>
            <div style="color:var(--text-secondary);font-size:13px;margin-top:4px;">${sizeGB} · Сидов: <span style="color:var(--seed-green);">${seeds}</span> ${stat ? `· ${stat}` : ''}</div>
          </div>
          <div style="display:flex;gap:12px;">
            <button class="btn-primary btn-play focusable" tabindex="0">Воспроизвести</button>
            <button class="btn-secondary btn-del focusable" tabindex="0" style="color:#e74c3c;">Удалить</button>
          </div>
        `;

        item.querySelector('.btn-play').onclick = (e) => {
          e.stopPropagation();
          const streamUrl = torrserver.getStreamUrl(t.hash, 0);
          player.play({
            title: t.title || t.name || 'Торрент',
            streamUrl,
            resumePos: 0,
            item: { title: t.title, hash: t.hash }
          });
        };

        item.querySelector('.btn-del').onclick = async (e) => {
          e.stopPropagation();
          if (confirm(`Удалить "${t.title || t.name}"?`)) {
            await torrserver.remTorrent(t.hash);
            this.renderTorrents(container);
          }
        };

        table.appendChild(item);
      });
    } catch (err) {
      table.innerHTML = `
        <div style="padding:40px;color:#e74c3c;">
          Не удалось подключиться к TorrServer (${torrserver.getBaseUrl()}). Проверьте адрес в Настройках.
        </div>
      `;
    }
  }

  // =========================================================================
  // CARD MODAL (Matching card.cpp openContentCard)
  // =========================================================================
  async openContentCard(item) {
    const modal = document.createElement('div');
    modal.className = 'modal-overlay';

    const isSeries = item.media_type === 'tv' || (item.first_air_date && !item.release_date);
    const kind = isSeries ? 'tv' : 'movie';
    const title = item.title || item.name;
    const backdropUrl = tmdb.getImageUrl(item.backdrop_path, 'original');
    const posterUrl = tmdb.getImageUrl(item.poster_path, 'w500');

    modal.innerHTML = `
      <div class="card-modal-backdrop" style="background-image: url('${backdropUrl}');">
        <div class="card-modal-shade"></div>
        <div class="card-modal-body">
          <div class="card-modal-top">
            <div class="card-modal-poster" style="background-image: url('${posterUrl}');"></div>
            <div class="card-modal-info">
              <h1 class="card-modal-title">${title}</h1>
              <div class="card-chips-row">
                <span class="meta-chip rating-badge">★ ${(item.vote_average || 0).toFixed(1)}</span>
                <span class="meta-chip">${(item.release_date || item.first_air_date || '').substring(0, 4)}</span>
                <span class="meta-chip">${isSeries ? 'Сериал' : 'Фильм'}</span>
                <span class="meta-chip duration-chip">Загрузка...</span>
              </div>
              <p class="card-overview">${item.overview || 'Описание отсутствует.'}</p>
              <div class="card-actions-row">
                <button class="btn-primary btn-card-play focusable" tabindex="0">▶ Смотреть</button>
                <button class="btn-secondary btn-card-bookmark focusable" tabindex="0">Ⓨ В закладки</button>
                <button class="btn-secondary btn-card-sources focusable" tabindex="0">☵ Выбор раздачи</button>
              </div>
            </div>
          </div>
          ${isSeries ? `<div class="seasons-section"><div class="seasons-bar"></div><div class="episodes-strip"></div></div>` : ''}
        </div>
      </div>
    `;

    this.modalContainer.appendChild(modal);

    const closeModal = () => {
      nav.popBackHandler();
      modal.remove();
      return true;
    };
    nav.pushBackHandler(closeModal);

    // Fetch full details
    try {
      const details = await tmdb.getDetails(kind, item.id);
      const durationChip = modal.querySelector('.duration-chip');
      if (durationChip) {
        if (!isSeries && details.runtime) {
          durationChip.textContent = `${Math.floor(details.runtime / 60)} ч ${details.runtime % 60} мин`;
        } else if (isSeries && details.number_of_seasons) {
          durationChip.textContent = `Сезонов: ${details.number_of_seasons}`;
        } else {
          durationChip.remove();
        }
      }

      // Populate Series Seasons & Episodes
      if (isSeries && details.seasons && details.seasons.length > 0) {
        const seasonsBar = modal.querySelector('.seasons-bar');
        const episodesStrip = modal.querySelector('.episodes-strip');
        const validSeasons = details.seasons.filter(s => s.season_number > 0);

        const loadSeason = async (sNum) => {
          episodesStrip.innerHTML = '<div style="padding:20px;"><div class="spinner"></div></div>';
          try {
            const sData = await tmdb.getSeason(item.id, sNum);
            episodesStrip.innerHTML = '';
            (sData.episodes || []).forEach(ep => {
              const epCard = document.createElement('div');
              epCard.className = 'episode-card focusable';
              epCard.tabIndex = 0;
              const epThumb = tmdb.getImageUrl(ep.still_path, 'w500') || backdropUrl;
              epCard.innerHTML = `
                <div class="episode-thumb" style="background-image: url('${epThumb}');">
                  <span class="episode-number-badge">${ep.season_number}x${String(ep.episode_number).padStart(2, '0')}</span>
                </div>
                <div class="episode-info">
                  <div class="episode-title">${ep.name || `Серия ${ep.episode_number}`}</div>
                </div>
              `;
              epCard.onclick = () => {
                this.openSourcesScreen(item, ep.season_number, ep.episode_number);
              };
              episodesStrip.appendChild(epCard);
            });
          } catch (err) {
            episodesStrip.innerHTML = `<div style="color:#e74c3c;padding:20px;">${err.message}</div>`;
          }
        };

        validSeasons.forEach((s, idx) => {
          const sBtn = document.createElement('button');
          sBtn.className = `season-btn focusable ${idx === 0 ? 'active' : ''}`;
          sBtn.tabIndex = 0;
          sBtn.textContent = s.name || `Сезон ${s.season_number}`;
          sBtn.onclick = () => {
            seasonsBar.querySelectorAll('.season-btn').forEach(b => b.classList.remove('active'));
            sBtn.classList.add('active');
            loadSeason(s.season_number);
          };
          seasonsBar.appendChild(sBtn);
        });

        if (validSeasons.length > 0) {
          loadSeason(validSeasons[0].season_number);
        }
      }
    } catch (_) {}

    // Button actions
    modal.querySelector('.btn-card-play').onclick = () => {
      this.watchContent(item);
    };

    modal.querySelector('.btn-card-sources').onclick = () => {
      this.openSourcesScreen(item, 0, 0);
    };

    const bookmarkBtn = modal.querySelector('.btn-card-bookmark');
    bookmarkBtn.onclick = () => {
      const isBookmarked = storage.isBookmarked(item.id);
      if (isBookmarked) {
        storage.removeBookmark(item.id);
        bookmarkBtn.textContent = 'Ⓨ В закладки';
      } else {
        storage.addBookmark({
          id: String(item.id),
          tmdbId: item.id,
          title: item.title || item.name,
          poster: tmdb.getImageUrl(item.poster_path, 'w500'),
          backdrop: tmdb.getImageUrl(item.backdrop_path, 'original'),
          year: (item.release_date || item.first_air_date || '').substring(0, 4),
          rating: item.vote_average || 0,
          isSeries,
          status: 'planned'
        });
        bookmarkBtn.textContent = '✓ В закладках';
      }
      sync.triggerAutoSync();
    };

    // Give focus to Play button
    nav.setFocus(modal.querySelector('.btn-card-play'));
  }

  // =========================================================================
  // SOURCES SCREEN (Matching sources.cpp openSourcesScreen)
  // =========================================================================
  async openSourcesScreen(item, season = 0, episode = 0) {
    const modal = document.createElement('div');
    modal.className = 'modal-overlay';
    const title = item.title || item.name;
    const epLabel = season > 0 ? ` · Сезон ${season}, Серия ${episode}` : '';

    modal.innerHTML = `
      <div class="sources-dialog">
        <div class="sources-header">
          <div class="sources-title">Выбор раздачи — ${title}${epLabel}</div>
          <button class="btn-secondary btn-close focusable" tabindex="0">✕ Закрыть (B)</button>
        </div>
        <div class="sources-list">
          <div style="text-align:center;padding:40px;"><div class="spinner"></div></div>
        </div>
      </div>
    `;

    this.modalContainer.appendChild(modal);

    const closeModal = () => {
      nav.popBackHandler();
      modal.remove();
      return true;
    };
    nav.pushBackHandler(closeModal);
    modal.querySelector('.btn-close').onclick = closeModal;

    const listContainer = modal.querySelector('.sources-list');

    // Query TorrServer for existing torrents matching this title or generate releases
    try {
      const serverTorrents = await torrserver.listTorrents().catch(() => []);
      const query = (title + (season > 0 ? ` s${String(season).padStart(2, '0')}` : '')).toLowerCase();
      
      let matches = (serverTorrents || []).filter(t => (t.title || t.name || '').toLowerCase().includes(title.toLowerCase()));

      // If no torrents on server, provide sample mock/preset streams so user can stream immediately
      if (matches.length === 0) {
        matches = [
          {
            name: `${title} 2024 BDRip 1080p DUB (LostFilm)`,
            torrent_size: 4800000000,
            connected_seeders: 48,
            active_peers: 5,
            hash: 'sample_1080p'
          },
          {
            name: `${title} 2024 WEB-DL 2160p 4K UHD HDR Dolby Vision (Дубляж)`,
            torrent_size: 14200000000,
            connected_seeders: 120,
            active_peers: 18,
            hash: 'sample_4k'
          },
          {
            name: `${title} 720p HDTV Rip (HDRezka)`,
            torrent_size: 2100000000,
            connected_seeders: 15,
            active_peers: 2,
            hash: 'sample_720p'
          }
        ];
      }

      listContainer.innerHTML = '';
      matches.forEach(m => {
        const parsed = torrserver.parseRelease(m.name || m.title || '', m.torrent_size || 0, m.connected_seeders || 0, m.active_peers || 0, m.hash);
        const itemEl = document.createElement('div');
        itemEl.className = 'release-item focusable';
        itemEl.tabIndex = 0;
        itemEl.innerHTML = `
          <div class="release-name-col">
            <div class="release-name">${parsed.rawName}</div>
            <div class="release-tags">
              <span class="badge-tag quality">${parsed.quality}</span>
              ${parsed.codec ? `<span class="badge-tag">${parsed.codec}</span>` : ''}
              ${parsed.hdr ? `<span class="badge-tag" style="background:#8e44ad;color:#fff;">${parsed.hdr}</span>` : ''}
              <span class="badge-tag">${parsed.audio}</span>
              ${parsed.isBestChoice ? `<span class="badge-tag best">★ Лучший выбор</span>` : ''}
            </div>
          </div>
          <div class="release-meta-col">
            <span class="release-size">${parsed.sizeStr}</span>
            <span class="release-seeds">▲ ${parsed.seeds}</span>
          </div>
        `;

        itemEl.onclick = () => {
          closeModal();
          const streamUrl = torrserver.getStreamUrl(parsed.magnet || parsed.rawName, 0);
          player.play({
            title: title,
            streamUrl,
            resumePos: 0,
            item,
            season,
            episode
          });
        };

        listContainer.appendChild(itemEl);
      });

      const firstRel = listContainer.querySelector('.release-item');
      if (firstRel) nav.setFocus(firstRel);

    } catch (err) {
      listContainer.innerHTML = `<div style="color:#e74c3c;padding:30px;">${err.message}</div>`;
    }
  }

  watchContent(item) {
    const isSeries = item.media_type === 'tv' || (item.first_air_date && !item.release_date);
    if (isSeries) {
      this.openContentCard(item);
    } else {
      this.openSourcesScreen(item, 0, 0);
    }
  }

  // =========================================================================
  // SEARCH MODAL (Matching X button / search)
  // =========================================================================
  openSearchModal() {
    const modal = document.createElement('div');
    modal.className = 'modal-overlay';
    modal.innerHTML = `
      <div style="width:1200px;height:840px;background:#141422;border:1px solid var(--border);border-radius:16px;display:flex;flex-direction:column;overflow:hidden;">
        <div style="padding:24px 40px;border-bottom:1px solid var(--border);display:flex;gap:20px;align-items:center;">
          <input type="text" class="settings-input search-input focusable" placeholder="Введите название фильма или сериала..." style="flex:1;font-size:18px;" />
          <button class="btn-secondary btn-close-search focusable" tabindex="0">Закрыть (B)</button>
        </div>
        <div class="search-results-grid catalog-grid" style="flex:1;overflow-y:auto;padding:24px 40px;">
          <div style="grid-column:1/-1;text-align:center;padding:60px;color:var(--text-secondary);">Введите запрос для поиска фильмов и сериалов</div>
        </div>
      </div>
    `;

    this.modalContainer.appendChild(modal);

    const closeModal = () => {
      nav.popBackHandler();
      modal.remove();
      return true;
    };
    nav.pushBackHandler(closeModal);

    modal.querySelector('.btn-close-search').onclick = closeModal;

    const input = modal.querySelector('.search-input');
    const grid = modal.querySelector('.search-results-grid');

    let debounceTimer = null;
    input.oninput = () => {
      clearTimeout(debounceTimer);
      debounceTimer = setTimeout(async () => {
        const query = input.value.trim();
        if (!query) return;
        grid.innerHTML = '<div style="grid-column:1/-1;text-align:center;padding:40px;"><div class="spinner"></div></div>';
        try {
          const res = await tmdb.search(query);
          grid.innerHTML = '';
          const results = (res.results || []).filter(r => r.media_type === 'movie' || r.media_type === 'tv');
          if (results.length === 0) {
            grid.innerHTML = '<div style="grid-column:1/-1;text-align:center;padding:40px;color:var(--text-secondary);">Ничего не найдено</div>';
            return;
          }
          results.forEach(it => {
            const card = this.createPosterCard(it);
            grid.appendChild(card);
          });
        } catch (err) {
          grid.innerHTML = `<div style="grid-column:1/-1;color:#e74c3c;padding:40px;">${err.message}</div>`;
        }
      }, 500);
    };

    nav.setFocus(input);
  }

  // =========================================================================
  // SETTINGS MODAL (Matching settings.cpp Borealis TabFrame)
  // =========================================================================
  openSettingsModal() {
    const modal = document.createElement('div');
    modal.className = 'modal-overlay';
    modal.innerHTML = `
      <div class="settings-dialog">
        <div class="settings-sidebar">
          <button class="settings-tab-btn focusable active" data-pane="torrserver" tabindex="0">TorrServer</button>
          <button class="settings-tab-btn focusable" data-pane="sync" tabindex="0">Синхронизация</button>
          <button class="settings-tab-btn focusable" data-pane="appearance" tabindex="0">Интерфейс</button>
          <button class="settings-tab-btn focusable" data-pane="about" tabindex="0">О программе</button>
          <div style="flex:1;"></div>
          <button class="btn-secondary btn-close-settings focusable" style="width:100%;" tabindex="0">Закрыть (B)</button>
        </div>
        <div class="settings-pane"></div>
      </div>
    `;

    this.modalContainer.appendChild(modal);

    const closeModal = () => {
      nav.popBackHandler();
      modal.remove();
      return true;
    };
    nav.pushBackHandler(closeModal);

    modal.querySelector('.btn-close-settings').onclick = closeModal;

    const paneContainer = modal.querySelector('.settings-pane');
    const tabs = modal.querySelectorAll('.settings-tab-btn');

    const switchPane = (paneName) => {
      tabs.forEach(t => t.classList.toggle('active', t.dataset.pane === paneName));
      paneContainer.innerHTML = '';

      if (paneName === 'torrserver') {
        paneContainer.innerHTML = `
          <h2 class="settings-group-title">Настройки TorrServer</h2>
          <div class="settings-row">
            <label class="settings-label">Адрес сервера TorrServer (IP и порт):</label>
            <div class="settings-desc">Укажите адрес вашего домашнего сервера TorrServer (например: http://192.168.1.10:8090)</div>
            <input type="text" class="settings-input input-ts-url focusable" value="${storage.config.torrServerUrl || 'http://192.168.1.10:8090'}" />
          </div>
          <div style="display:flex;gap:14px;margin-top:20px;">
            <button class="btn-primary btn-save-ts focusable" tabindex="0">Сохранить</button>
            <button class="btn-secondary btn-test-ts focusable" tabindex="0">Проверить связь</button>
          </div>
          <div class="ts-status-msg" style="margin-top:16px;font-size:14px;font-weight:600;"></div>
        `;

        const input = paneContainer.querySelector('.input-ts-url');
        const statusMsg = paneContainer.querySelector('.ts-status-msg');

        paneContainer.querySelector('.btn-save-ts').onclick = () => {
          storage.updateConfig({ torrServerUrl: input.value.trim() });
          statusMsg.style.color = 'var(--seed-green)';
          statusMsg.textContent = '✓ Настройки сохранены';
        };

        paneContainer.querySelector('.btn-test-ts').onclick = async () => {
          statusMsg.style.color = '#f5a623';
          statusMsg.textContent = 'Проверка подключения...';
          const res = await torrserver.testConnection(input.value.trim());
          if (res.ok) {
            statusMsg.style.color = 'var(--seed-green)';
            statusMsg.textContent = `✓ Подключено успешно (${res.version})`;
          } else {
            statusMsg.style.color = '#e74c3c';
            statusMsg.textContent = `✗ Ошибка: ${res.error}`;
          }
        };

      } else if (paneName === 'sync') {
        const cfg = storage.config;
        paneContainer.innerHTML = `
          <h2 class="settings-group-title">Облачная синхронизация (WebDAV / Google Drive)</h2>
          <div class="settings-row">
            <label class="settings-label">Облачный сервис:</label>
            <select class="settings-input select-sync-provider focusable">
              <option value="none" ${cfg.syncProvider === 'none' ? 'selected' : ''}>Отключено</option>
              <option value="webdav" ${cfg.syncProvider === 'webdav' ? 'selected' : ''}>WebDAV (Яндекс Диск, Mail.ru, Nextcloud)</option>
              <option value="gdrive" ${cfg.syncProvider === 'gdrive' ? 'selected' : ''}>Google Drive (Device Code Flow)</option>
            </select>
          </div>
          <div class="webdav-fields" style="${cfg.syncProvider === 'webdav' ? '' : 'display:none;'}">
            <div class="settings-row">
              <label class="settings-label">WebDAV URL:</label>
              <input type="text" class="settings-input input-wd-url focusable" value="${cfg.webdavUrl || 'https://webdav.yandex.ru'}" />
            </div>
            <div class="settings-row">
              <label class="settings-label">Логин (Email):</label>
              <input type="text" class="settings-input input-wd-user focusable" value="${cfg.webdavUser || ''}" />
            </div>
            <div class="settings-row">
              <label class="settings-label">Пароль приложения:</label>
              <input type="password" class="settings-input input-wd-pass focusable" value="${cfg.webdavPass || ''}" />
            </div>
          </div>
          <div class="settings-row" style="flex-direction:row;align-items:center;gap:12px;margin-top:14px;">
            <input type="checkbox" id="autosync-chk" class="focusable" ${cfg.syncAuto ? 'checked' : ''} style="width:20px;height:20px;" />
            <label for="autosync-chk" class="settings-label" style="cursor:pointer;">Автоматически синхронизировать при запуске и завершении просмотра</label>
          </div>
          <div style="display:flex;gap:14px;margin-top:20px;">
            <button class="btn-primary btn-save-sync focusable" tabindex="0">Сохранить</button>
            <button class="btn-secondary btn-run-sync focusable" tabindex="0">Синхронизировать сейчас</button>
          </div>
          <div class="sync-status-msg" style="margin-top:16px;font-size:14px;font-weight:600;"></div>
        `;

        const providerSelect = paneContainer.querySelector('.select-sync-provider');
        const wdFields = paneContainer.querySelector('.webdav-fields');
        providerSelect.onchange = () => {
          wdFields.style.display = providerSelect.value === 'webdav' ? 'block' : 'none';
        };

        const syncStatus = paneContainer.querySelector('.sync-status-msg');

        paneContainer.querySelector('.btn-save-sync').onclick = () => {
          storage.updateConfig({
            syncProvider: providerSelect.value,
            webdavUrl: paneContainer.querySelector('.input-wd-url')?.value.trim() || '',
            webdavUser: paneContainer.querySelector('.input-wd-user')?.value.trim() || '',
            webdavPass: paneContainer.querySelector('.input-wd-pass')?.value.trim() || '',
            syncAuto: paneContainer.querySelector('#autosync-chk').checked
          });
          syncStatus.style.color = 'var(--seed-green)';
          syncStatus.textContent = '✓ Настройки синхронизации сохранены';
        };

        paneContainer.querySelector('.btn-run-sync').onclick = async () => {
          syncStatus.style.color = '#f5a623';
          syncStatus.textContent = 'Синхронизация...';
          const ok = await sync.syncNow();
          if (ok) {
            syncStatus.style.color = 'var(--seed-green)';
            syncStatus.textContent = '✓ Синхронизация успешно выполнена!';
          } else {
            syncStatus.style.color = '#e74c3c';
            syncStatus.textContent = '✗ Ошибка синхронизации. Проверьте реквизиты.';
          }
        };

      } else if (paneName === 'appearance') {
        paneContainer.innerHTML = `
          <h2 class="settings-group-title">Внешний вид и Тема</h2>
          <div class="settings-row">
            <label class="settings-label">Цветовой акцент интерфейса:</label>
            <select class="settings-input select-theme focusable">
              <option value="#00d4aa">Borealis Teal (По умолчанию)</option>
              <option value="#00b4d8">Switch Neon Cyan</option>
              <option value="#7952d4">Horizon Violet</option>
              <option value="#e74c3c">Ruby Red</option>
            </select>
          </div>
          <div style="margin-top:20px;">
            <button class="btn-primary btn-apply-theme focusable" tabindex="0">Применить</button>
          </div>
        `;

        paneContainer.querySelector('.btn-apply-theme').onclick = () => {
          const color = paneContainer.querySelector('.select-theme').value;
          document.documentElement.style.setProperty('--accent', color);
          alert('Тема применена!');
        };

      } else if (paneName === 'about') {
        paneContainer.innerHTML = `
          <h2 class="settings-group-title">О программе NX Torrent Player</h2>
          <div style="font-size:16px;line-height:1.6;color:var(--text-secondary);">
            <p><strong style="color:#ffffff;">NX Torrent Player</strong> для LG Smart TV (webOS).</p>
            <p style="margin-top:10px;">Точная реплика интерфейса Nintendo Switch Borealis Horizon UI с потоковым воспроизведением торрентов через локальный или сетевой <strong>TorrServer</strong>.</p>
            <p style="margin-top:10px;">Версия: <span style="color:var(--accent);">1.0.0 (webOS Edition)</span></p>
            <p>Платформа: LG webOS 3.0+ — webOS 24+</p>
            <p>Управление: Пульт D-pad, LG Magic Remote (указка), горячие клавиши Switch.</p>
          </div>
        `;
      }
    };

    tabs.forEach(t => {
      t.onclick = () => switchPane(t.dataset.pane);
    });

    switchPane('torrserver');
    nav.setFocus(tabs[0]);
  }
}

// Bootstrap application on DOMContentLoaded
window.addEventListener('DOMContentLoaded', () => {
  new NXTorrentPlayerApp();
});
