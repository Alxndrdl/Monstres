// Service Worker: makes the game work offline.
//
// Strategy: network first, cache as fallback. Online, players always get the
// latest build (no stale .wasm after a deploy); offline, or when the server
// answers with an error (404 for a site taken down, 5xx), the last copy is
// used. Only a file never downloaded gets the server's error.
//
// Every request bypasses the browser's HTTP cache (or revalidates with the
// server): a simple server sends no caching headers, and the browser may then
// keep an old index.js / index.wasm next to a new index.html and ui.js, which
// do not work together.

const CACHE = 'monstres-v29';
const CORE_FILES = [
  './',
  'index.html',
  'index.js',
  'index.wasm',
  'index.data',
  'util.js',
  'lore.js',
  'controls.js',
  'ui.js',
  'saves.js',
  'create.js',
  'menu.js',
  'battle.js',
  'team.js',
  'dialogue.js',
  'spells.js',
  'manifest.webmanifest',
  'icons/icon-192.png',
  'icons/icon-512.png',
];

self.addEventListener('install', (event) => {
  event.waitUntil(caches.open(CACHE).then((cache) =>
    cache.addAll(CORE_FILES.map((url) => new Request(url, { cache: 'reload' })))));
  self.skipWaiting();
});

self.addEventListener('activate', (event) => {
  event.waitUntil(
    caches.keys()
      .then((keys) => Promise.all(keys.filter((k) => k !== CACHE).map((k) => caches.delete(k))))
      .then(() => self.clients.claim()),
  );
});

self.addEventListener('fetch', (event) => {
  const { request } = event;
  if (request.method !== 'GET' || new URL(request.url).origin !== self.location.origin) return;

  event.respondWith(
    fetch(request, { cache: 'no-cache' })
      .then((response) => {
        if (response.ok) {
          // Kept without the page's parameters (?seed=..., read with ignoreSearch
          // below): one copy per file, not one per address ever opened.
          const key = new URL(request.url);
          key.search = '';
          const copy = response.clone();
          event.waitUntil(caches.open(CACHE).then((cache) => cache.put(key.href, copy)));
          return response;
        }
        return caches.match(request, { ignoreSearch: true }).then((cached) => cached || response);
      })
      .catch(() => caches.match(request, { ignoreSearch: true })),
  );
});
