/**
 * Spatial Navigation engine for webOS TV and D-pad remotes.
 */
class SpatialNavigator {
  constructor() {
    this.currentFocus = null;
    this.backHandlers = [];
    this.enabled = true;
    this.init();
  }

  init() {
    window.addEventListener('keydown', (e) => this.handleKeyDown(e));
  }

  pushBackHandler(handler) {
    this.backHandlers.push(handler);
  }

  popBackHandler() {
    return this.backHandlers.pop();
  }

  handleKeyDown(e) {
    if (!this.enabled) return;

    const code = e.keyCode || e.which;

    switch (code) {
      case 37: // ArrowLeft
        e.preventDefault();
        this.move('left');
        break;
      case 38: // ArrowUp
        e.preventDefault();
        this.move('up');
        break;
      case 39: // ArrowRight
        e.preventDefault();
        this.move('right');
        break;
      case 40: // ArrowDown
        e.preventDefault();
        this.move('down');
        break;
      case 13: // Enter / OK
        e.preventDefault();
        if (this.currentFocus) {
          this.currentFocus.click();
        }
        break;
      case 461: // webOS Back
      case 27:  // Esc
      case 8:   // Backspace (outside input)
        if (e.target && (e.target.tagName === 'INPUT' || e.target.tagName === 'TEXTAREA')) {
          return;
        }
        e.preventDefault();
        if (this.backHandlers.length > 0) {
          const handler = this.backHandlers[this.backHandlers.length - 1];
          if (handler()) {
            this.backHandlers.pop();
          }
        }
        break;
      case 415: // MediaPlay
      case 19:  // MediaPause
      case 179: // MediaPlayPause
        window.dispatchEvent(new CustomEvent('tv:playpause'));
        break;
      case 417: // FastForward
        window.dispatchEvent(new CustomEvent('tv:fastforward'));
        break;
      case 412: // Rewind
        window.dispatchEvent(new CustomEvent('tv:rewind'));
        break;
    }
  }

  getFocusableElements(container = document) {
    const list = Array.from(container.querySelectorAll('.focusable, button:not([disabled]), [tabindex="0"]'));
    return list.filter(el => {
      if (el.disabled || el.getAttribute('aria-hidden') === 'true') return false;
      const rect = el.getBoundingClientRect();
      return rect.width > 0 && rect.height > 0 && window.getComputedStyle(el).display !== 'none' && window.getComputedStyle(el).visibility !== 'hidden';
    });
  }

  setFocus(el, smoothScroll = true) {
    if (!el) return;
    if (this.currentFocus && this.currentFocus !== el) {
      this.currentFocus.classList.remove('focused');
      this.currentFocus.blur();
    }
    this.currentFocus = el;
    this.currentFocus.classList.add('focused');
    this.currentFocus.focus();

    if (smoothScroll) {
      el.scrollIntoView({
        behavior: 'smooth',
        block: 'nearest',
        inline: 'center'
      });
    }
  }

  move(direction) {
    const focusables = this.getFocusableElements();
    if (focusables.length === 0) return;

    if (!this.currentFocus || !document.body.contains(this.currentFocus)) {
      this.setFocus(focusables[0]);
      return;
    }

    const curRect = this.currentFocus.getBoundingClientRect();
    const curCenter = {
      x: curRect.left + curRect.width / 2,
      y: curRect.top + curRect.height / 2
    };

    let bestCandidate = null;
    let minDistance = Infinity;

    for (const el of focusables) {
      if (el === this.currentFocus) continue;
      const rect = el.getBoundingClientRect();
      const center = {
        x: rect.left + rect.width / 2,
        y: rect.top + rect.height / 2
      };

      const dx = center.x - curCenter.x;
      const dy = center.y - curCenter.y;

      let validDirection = false;
      if (direction === 'left' && dx < -10) validDirection = true;
      else if (direction === 'right' && dx > 10) validDirection = true;
      else if (direction === 'up' && dy < -10) validDirection = true;
      else if (direction === 'down' && dy > 10) validDirection = true;

      if (!validDirection) continue;

      // Distance calculation with directional alignment weight
      let mainAxis = 0;
      let crossAxis = 0;

      if (direction === 'left' || direction === 'right') {
        mainAxis = Math.abs(dx);
        crossAxis = Math.abs(dy);
      } else {
        mainAxis = Math.abs(dy);
        crossAxis = Math.abs(dx);
      }

      // Orthogonal penalty so straight-line items win over diagonal ones
      const distance = mainAxis + crossAxis * 2.5;

      if (distance < minDistance) {
        minDistance = distance;
        bestCandidate = el;
      }
    }

    if (bestCandidate) {
      this.setFocus(bestCandidate);
    }
  }
}

export const nav = new SpatialNavigator();
