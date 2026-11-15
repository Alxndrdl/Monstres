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

  window.MonstresUtil = { $, el, whenReady, prefs, setText };
})();
