import { sound } from './sound.js';

/**
 * Spatial Navigation engine for webOS TV and D-pad remotes with Nintendo Switch button mapping.
 */
class SpatialNavigator {
  constructor() {
    this.currentFocus = null;
    this.backHandlers = [];
    this.actionHandlers = {}; // { 'tabLeft', 'tabRight', 'search', 'bookmark', 'options' }
    this.enabled = true;
    this.init();
  }

  init() {
    window.addEventListener('keydown', (e) => this.handleKeyDown(e));
  }

  onAction(action, handler) {
    this.actionHandlers[action] = handler;
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

    // Do not interfere with text inputs (e.g. search box, settings)
    if (e.target && (e.target.tagName === 'INPUT' || e.target.tagName === 'TEXTAREA')) {
      if (code === 27 || code === 461) { // Esc or Back from input
        e.target.blur();
        e.preventDefault();
        return;
      }
      return;
    }

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
      case 13: // Enter / OK (Switch A button)
        e.preventDefault();
        sound.playConfirm();
        if (this.currentFocus) {
          this.currentFocus.click();
        }
        break;
      case 461: // webOS Back
      case 27:  // Esc / Switch B button
      case 8:   // Backspace
        e.preventDefault();
        sound.playBack();
        if (this.backHandlers.length > 0) {
          const handler = this.backHandlers[this.backHandlers.length - 1];
          if (handler()) {
            return;
          }
        }
        // Fallback: return focus to active header tab
        const activeTab = document.querySelector('.nav-tab.active');
        if (activeTab && this.currentFocus !== activeTab) {
          this.setFocus(activeTab);
        }
        break;

      // Switch L button (Tab Left): [ (219), Q (81), Red button (403), Channel Down (34)
      case 219:
      case 81:
      case 403:
      case 34:
        e.preventDefault();
        sound.playTab();
        if (this.actionHandlers['tabLeft']) this.actionHandlers['tabLeft']();
        break;

      // Switch R button (Tab Right): ] (221), E (69), Green button (404), Channel Up (33)
      case 221:
      case 69:
      case 404:
      case 33:
        e.preventDefault();
        sound.playTab();
        if (this.actionHandlers['tabRight']) this.actionHandlers['tabRight']();
        break;

      // Switch X button (Search): X (88), Blue button (406)
      case 88:
      case 406:
        e.preventDefault();
        if (this.actionHandlers['search']) this.actionHandlers['search']();
        break;

      // Switch Y button (Bookmark): Y (89), Yellow button (405)
      case 89:
      case 405:
        e.preventDefault();
        if (this.actionHandlers['bookmark']) this.actionHandlers['bookmark']();
        break;

      // Switch (+) button (Options): + (187, 61), O (79), Menu / Info
      case 187:
      case 61:
      case 79:
        e.preventDefault();
        if (this.actionHandlers['options']) this.actionHandlers['options']();
        break;

      default:
        break;
    }
  }

  setFocus(elem, playAudio = true) {
    if (!elem) return;
    const changed = this.currentFocus !== elem;
    if (this.currentFocus && changed) {
      this.currentFocus.classList.remove('focus');
    }
    this.currentFocus = elem;
    elem.classList.add('focus');
    try {
      elem.focus({ preventScroll: true });
    } catch (_) {}

    if (changed && playAudio) {
      sound.playMove();
    }

    // Smoothly scroll container into view
    elem.scrollIntoView({
      behavior: 'smooth',
      block: 'nearest',
      inline: 'center'
    });
  }

  move(direction) {
    const focusables = Array.from(document.querySelectorAll('.focusable:not([disabled])'))
      .filter(el => {
        const style = window.getComputedStyle(el);
        const rect = el.getBoundingClientRect();
        return style.display !== 'none' && style.visibility !== 'hidden' && rect.width > 0 && rect.height > 0;
      });

    if (focusables.length === 0) return;

    if (!this.currentFocus || !focusables.includes(this.currentFocus)) {
      this.setFocus(focusables[0]);
      return;
    }

    const currentRect = this.currentFocus.getBoundingClientRect();
    const currentCenter = {
      x: currentRect.left + currentRect.width / 2,
      y: currentRect.top + currentRect.height / 2
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

      const dx = center.x - currentCenter.x;
      const dy = center.y - currentCenter.y;

      // Check geometric cone
      let isValidDirection = false;
      if (direction === 'left' && dx < -5) isValidDirection = true;
      else if (direction === 'right' && dx > 5) isValidDirection = true;
      else if (direction === 'up' && dy < -5) isValidDirection = true;
      else if (direction === 'down' && dy > 5) isValidDirection = true;

      if (!isValidDirection) continue;

      // Priority along primary axis
      let dist = 0;
      if (direction === 'left' || direction === 'right') {
        dist = Math.abs(dx) + Math.abs(dy) * 2.5;
      } else {
        dist = Math.abs(dy) + Math.abs(dx) * 2.5;
      }

      if (dist < minDistance) {
        minDistance = dist;
        bestCandidate = el;
      }
    }

    if (bestCandidate) {
      this.setFocus(bestCandidate);
    }
  }
}

export const nav = new SpatialNavigator();
