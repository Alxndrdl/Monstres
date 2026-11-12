// Service Worker: makes the game work offline.
//
// Strategy: network first, cache as fallback. Online, players always get the
// latest build (no stale .wasm after a deploy); offline, the last copy is used.
//
// Every request bypasses the browser's HTTP cache (or revalidates with the
// server): a simple server sends no caching headers, and the browser may then
// keep an old index.js / index.wasm next to a new index.html and ui.js, which
// do not work together.

const CACHE = 'monstres-v14';
const CORE_FILES = [
  './',
  'index.html',
  'index.js',
  'index.wasm',
  'index.data',
  'lore.js',
  'controls.js',
  'ui.js',
  'menu.js',
  'battle.js',
  'team.js',
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
          const copy = response.clone();
          caches.open(CACHE).then((cache) => cache.put(request, copy));
        }
        return response;
      })
      .catch(() => caches.match(request, { ignoreSearch: true })),
  );
});
