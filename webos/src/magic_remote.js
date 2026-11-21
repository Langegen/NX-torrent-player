import { nav } from './spatial_nav.js';

/**
 * LG Magic Remote pointer & wheel support.
 */
export function initMagicRemote() {
  let isPointerActive = false;

  // Track pointer movements
  window.addEventListener('mousemove', (e) => {
    isPointerActive = true;
    document.body.classList.add('magic-pointer-mode');

    const target = e.target.closest('.focusable, button, [tabindex="0"]');
    if (target && target !== nav.currentFocus) {
      nav.setFocus(target, false); // Don't scroll while moving mouse
    }
  });

  // Switch back to D-pad mode on keypress
  window.addEventListener('keydown', () => {
    if (isPointerActive) {
      isPointerActive = false;
      document.body.classList.remove('magic-pointer-mode');
    }
  });

  // Horizontal mouse wheel scrolling for strips
  window.addEventListener('wheel', (e) => {
    const strip = e.target.closest('.cards-strip, .horizontal-scroll');
    if (strip) {
      e.preventDefault();
      strip.scrollLeft += e.deltaY * 2;
    }
  }, { passive: false });
}
