// Loaded first: what the game needs before it starts, and the small helpers
// shared by the page's scripts (window.MonstresUtil).

// Read by the game's own script (index.js).
var Module = { canvas: document.getElementById('canvas') };  // eslint-disable-line no-var, no-unused-vars

if ('serviceWorker' in navigator) {
  navigator.serviceWorker.register('sw.js').catch((e) => console.warn('Service Worker :', e));
}

(() => {
  'use strict';

  const $ = (id) => document.getElementById(id);

  // A new element: its properties, then its children (nodes or texts).
  const el = (tag, props = {}, children = []) => {
    const node = Object.assign(document.createElement(tag), props);
    for (const child of children) node.append(child);
    return node;
  };

  // Calls f with the game (window.monstres) once it is ready.
  const whenReady = (f) => {
    if (window.monstres && window.monstres.isReady) f(window.monstres);
    else window.addEventListener('monstres-ready', () => f(window.monstres), { once: true });
  };

  // Preferences of this browser only. Storage may be unavailable (private
  // mode, blocked site data): the game works the same, it just forgets.
  const prefs = {
    get(key, fallback) {
      try {
        const v = localStorage.getItem(`monstres.${key}`);
        return v === null ? fallback : JSON.parse(v);
      } catch {
        return fallback;
      }
    },
    set(key, value) {
      try {
        localStorage.setItem(`monstres.${key}`, JSON.stringify(value));
      } catch {
        // not saved, never mind
      }
    },
  };

  // For what is shown again every frame: the page is touched only when the
  // text changes (writing the same text again still costs a layout).
  const setText = (node, text) => {
    const t = String(text);
    if (node.textContent !== t) node.textContent = t;
  };

  // Full screen on phones and tablets: the status bar and the system buttons
  // hide. Installed on Android, the manifest already opens the game that way;
  // in a browser tab, the first touch asks for it (a browser grants it only
  // right after a gesture), then the screen is held in landscape. Once the
  // player leaves full screen, it is not asked again. Not on iPhones, which
  // have no full screen for a page, nor under automated tests. Where the
  // screen can be held in landscape, the message shown in portrait says that
  // a touch is enough, even with the phone's rotation locked.
  const root = document.documentElement;
  if (root.requestFullscreen && !navigator.webdriver) {
    let wanted = true;
    const rotateText = $('rotate-text');
    const turnText = (canTurn) => {
      if (rotateText) {
        rotateText.textContent = canTurn ? 'Touche l’écran pour jouer.' : 'Tournez votre téléphone pour jouer.';
      }
    };
    const installed = matchMedia('(display-mode: fullscreen)').matches;  // already full screen
    turnText(!installed && !!(screen.orientation && screen.orientation.lock));
    const onTouch = (e) => {
      if (!wanted || e.pointerType === 'mouse' || document.fullscreenElement) return;
      if (installed) return;
      root.requestFullscreen({ navigationUI: 'hide' })
        .then(() => screen.orientation && screen.orientation.lock && screen.orientation.lock('landscape'))
        .catch(() => turnText(false));  // refused or no lock: the player turns the phone
    };
    window.addEventListener('pointerup', onTouch, { capture: true, passive: true });
    let wasFull = false;
    document.addEventListener('fullscreenchange', () => {
      if (wasFull && !document.fullscreenElement && !document.hidden) {
        wanted = false;
        turnText(false);
      }
      wasFull = !!document.fullscreenElement;
    });
  }

  window.MonstresUtil = { $, el, whenReady, prefs, setText };
})();
