import { nav } from './spatial_nav.js';
import { storage } from './storage.js';
import { sync } from './sync.js';

/**
 * Fullscreen HTML5 / webOS TV Video Player with Lampa/Horizon style OSD.
 */
export class WebOSPlayer {
  constructor() {
    this.container = null;
    this.video = null;
    this.osd = null;
    this.progressBar = null;
    this.progressFill = null;
    this.timeLabel = null;
    this.endTimeLabel = null;
    this.osdTimer = null;
    this.currentContext = null;
    this.isSeeking = false;
  }

  play({ title, streamUrl, resumePos = 0, item = {}, season = 0, episode = 0 }) {
    this.currentContext = { title, streamUrl, item, season, episode, lastPos: resumePos, duration: 0 };

    this.buildUI();
    this.attachEvents();

    this.video.src = streamUrl;
    this.video.currentTime = resumePos > 15 ? resumePos : 0;
    this.video.play().catch(e => console.warn('[player] autoplay prevented:', e));

    this.showOSD();
    nav.pushBackHandler(() => this.close());
  }

  buildUI() {
    this.container = document.createElement('div');
    this.container.className = 'player-container';
    this.container.innerHTML = `
      <video class="video-element" playsinline></video>
      <div class="player-buffering" style="display:none;">
        <div class="spinner"></div>
        <div class="buffer-text">Буферизация...</div>
      </div>
      <div class="player-osd visible">
        <div class="osd-top-bar">
          <button class="osd-btn btn-back focusable" tabindex="0">
            <span class="icon">‹</span> Назад
          </button>
          <div class="osd-title-group">
            <div class="osd-title">${this.currentContext.title}</div>
            ${this.currentContext.season > 0 ? `<div class="osd-subtitle">Сезон ${this.currentContext.season}, Серия ${this.currentContext.episode}</div>` : ''}
          </div>
          <div class="osd-clocks">
            <span class="clock-now">00:00</span>
            <span class="clock-end">Окончание: 00:00</span>
          </div>
        </div>
        <div class="osd-bottom-bar">
          <div class="osd-timeline focusable" tabindex="0">
            <div class="timeline-bar">
              <div class="timeline-fill"></div>
            </div>
          </div>
          <div class="osd-controls">
            <div class="osd-time-display">00:00 / 00:00</div>
            <div class="osd-btn-group">
              <button class="osd-btn btn-rewind focusable" tabindex="0">-10с</button>
              <button class="osd-btn btn-playpause focusable" tabindex="0">Пауза</button>
              <button class="osd-btn btn-forward focusable" tabindex="0">+30с</button>
              <button class="osd-btn btn-ratio focusable" tabindex="0">Размер: Авто</button>
            </div>
          </div>
        </div>
      </div>
    `;

    document.body.appendChild(this.container);

    this.video = this.container.querySelector('.video-element');
    this.osd = this.container.querySelector('.player-osd');
    this.progressFill = this.container.querySelector('.timeline-fill');
    this.timeLabel = this.container.querySelector('.osd-time-display');
    this.endTimeLabel = this.container.querySelector('.clock-end');
    this.spinner = this.container.querySelector('.player-buffering');

    nav.setFocus(this.container.querySelector('.btn-playpause'));
  }

  attachEvents() {
    const btnPlayPause = this.container.querySelector('.btn-playpause');
    const btnBack = this.container.querySelector('.btn-back');
    const btnRewind = this.container.querySelector('.btn-rewind');
    const btnForward = this.container.querySelector('.btn-forward');
    const btnRatio = this.container.querySelector('.btn-ratio');
    const timeline = this.container.querySelector('.osd-timeline');

    btnBack.onclick = () => this.close();

    btnPlayPause.onclick = () => {
      if (this.video.paused) {
        this.video.play();
        btnPlayPause.textContent = 'Пауза';
      } else {
        this.video.pause();
        btnPlayPause.textContent = 'Пуск';
      }
      this.resetOsdTimer();
    };

    btnRewind.onclick = () => {
      this.video.currentTime = Math.max(0, this.video.currentTime - 10);
      this.resetOsdTimer();
    };

    btnForward.onclick = () => {
      this.video.currentTime = Math.min(this.video.duration || 0, this.video.currentTime + 30);
      this.resetOsdTimer();
    };

    let ratioMode = 0; // 0: contain, 1: cover, 2: fill
    btnRatio.onclick = () => {
      ratioMode = (ratioMode + 1) % 3;
      if (ratioMode === 0) {
        this.video.style.objectFit = 'contain';
        btnRatio.textContent = 'Размер: Авто';
      } else if (ratioMode === 1) {
        this.video.style.objectFit = 'cover';
        btnRatio.textContent = 'Размер: Заполнить';
      } else {
        this.video.style.objectFit = 'fill';
        btnRatio.textContent = 'Размер: Растянуть';
      }
      this.resetOsdTimer();
    };

    this.video.ontimeupdate = () => this.onTimeUpdate();
    this.video.onwaiting = () => { this.spinner.style.display = 'flex'; };
    this.video.onplaying = () => { this.spinner.style.display = 'none'; };

    // Remote playback events
    window.addEventListener('tv:playpause', () => btnPlayPause.click());
    window.addEventListener('tv:fastforward', () => btnForward.click());
    window.addEventListener('tv:rewind', () => btnRewind.click());

    // Show OSD on mouse move or key
    this.container.onmousemove = () => this.showOSD();
    window.addEventListener('keydown', (e) => {
      if (!this.container) return;
      if (!this.osd.classList.contains('visible')) {
        this.showOSD();
      } else {
        this.resetOsdTimer();
      }
    });

    this.updateClock();
    this.clockInterval = setInterval(() => this.updateClock(), 1000);
  }

  formatTime(secs) {
    if (isNaN(secs) || secs < 0) return '00:00';
    const h = Math.floor(secs / 3600);
    const m = Math.floor((secs % 3600) / 60);
    const s = Math.floor(secs % 60);
    const mm = m < 10 ? '0' + m : m;
    const ss = s < 10 ? '0' + s : s;
    if (h > 0) {
      return `${h}:${mm}:${ss}`;
    }
    return `${mm}:${ss}`;
  }

  updateClock() {
    const now = new Date();
    const clockEl = this.container?.querySelector('.clock-now');
    if (clockEl) {
      const hh = String(now.getHours()).padStart(2, '0');
      const mm = String(now.getMinutes()).padStart(2, '0');
      clockEl.textContent = `${hh}:${mm}`;
    }

    if (this.endTimeLabel && this.video && this.video.duration) {
      const remaining = Math.max(0, this.video.duration - this.video.currentTime);
      const end = new Date(now.getTime() + remaining * 1000);
      const eh = String(end.getHours()).padStart(2, '0');
      const em = String(end.getMinutes()).padStart(2, '0');
      this.endTimeLabel.textContent = `Окончание: ${eh}:${em}`;
    }
  }

  onTimeUpdate() {
    if (!this.video || isNaN(this.video.duration)) return;
    const cur = this.video.currentTime;
    const dur = this.video.duration;
    const pct = (cur / dur) * 100;

    this.progressFill.style.width = `${pct}%`;
    this.timeLabel.textContent = `${this.formatTime(cur)} / ${this.formatTime(dur)}`;
    this.currentContext.lastPos = cur;
    this.currentContext.duration = dur;

    // Record progress periodically (every ~5s)
    if (Math.floor(cur) % 5 === 0) {
      this.saveProgress();
    }
  }

  saveProgress() {
    if (!this.currentContext) return;
    const { title, streamUrl, item, season, episode, lastPos, duration } = this.currentContext;
    const isDone = duration > 0 && lastPos >= duration * 0.95;

    // Save in history
    storage.recordHistory({
      title,
      source: streamUrl,
      sourceType: 'TorrServer',
      positionSec: lastPos,
      durationSec: duration,
      completed: isDone,
      tmdbId: item.tmdbId || 0,
      series: season > 0,
      season,
      episode
    });

    // Save in library
    storage.recordLibraryProgress(item, season, episode, lastPos, duration, isDone, streamUrl, 0);
  }

  showOSD() {
    if (!this.osd) return;
    this.osd.classList.add('visible');
    this.resetOsdTimer();
  }

  hideOSD() {
    if (!this.osd || this.video?.paused) return;
    this.osd.classList.remove('visible');
  }

  resetOsdTimer() {
    clearTimeout(this.osdTimer);
    this.osdTimer = setTimeout(() => this.hideOSD(), 4000);
  }

  close() {
    if (!this.container) return true;

    this.saveProgress();
    clearInterval(this.clockInterval);
    clearTimeout(this.osdTimer);

    this.video.pause();
    this.video.src = '';
    this.container.remove();
    this.container = null;

    // Trigger cloud auto-sync on exit
    sync.triggerAutoSync();

    // Re-focus previous UI element
    const firstFocus = nav.getFocusableElements()[0];
    if (firstFocus) nav.setFocus(firstFocus);

    return true;
  }
}

export const player = new WebOSPlayer();
