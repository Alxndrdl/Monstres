// JavaScript side of the game (emcc --post-js). Same scope as the generated
// code, so FS, HEAPU8 and the _monstres_* exports are reachable directly.

// ---- Saving to IndexedDB: one sync at a time, coalescing extra requests ----
let monstresSyncing = null;
let monstresSyncAgain = false;

function monstresSync() {
  if (monstresSyncing) {
    monstresSyncAgain = true;
    return monstresSyncing;
  }
  monstresSyncing = new Promise((resolve) => {
    FS.syncfs(false, (err) => {
      if (err) console.error('Sauvegarde impossible', err);
      resolve(!err);
    });
  }).then((ok) => {
    monstresSyncing = null;
    if (monstresSyncAgain) {
      monstresSyncAgain = false;
      return monstresSync();
    }
    return ok;
  });
  return monstresSyncing;
}
Module.monstresSync = monstresSync;

// ---- Page hooks called from C++ ----
Module.monstresFatal = (message) => {
  const box = document.getElementById('message');
  if (box) {
    box.textContent = message;
    box.hidden = false;
  }
  console.error(message);
};

// ---- Export / import of a save file ----
async function monstresExportSave(options = {}) {
  const len = _monstres_export_prepare();
  const ptr = _monstres_export_data();
  const blob = new Blob([HEAPU8.slice(ptr, ptr + len)], { type: 'application/octet-stream' });
  const name = 'monstres.sav';

  // Real file on disk (Chrome / Edge desktop).
  if (window.showSaveFilePicker && !options.forceDownload) {
    try {
      const handle = await window.showSaveFilePicker({
        suggestedName: name,
        types: [{ description: 'Sauvegarde Monstres', accept: { 'application/octet-stream': ['.sav'] } }],
      });
      const writable = await handle.createWritable();
      await writable.write(blob);
      await writable.close();
      return 'file';
    } catch (e) {
      if (e.name === 'AbortError') return 'cancelled';
      console.warn('showSaveFilePicker a échoué, téléchargement à la place', e);
    }
  }

  // Fallback everywhere else: a regular download.
  const a = document.createElement('a');
  a.href = URL.createObjectURL(blob);
  a.download = name;
  document.body.appendChild(a);
  a.click();
  a.remove();
  setTimeout(() => URL.revokeObjectURL(a.href), 10000);
  return 'download';
}

async function monstresImportSave(file) {
  const bytes = new Uint8Array(await file.arrayBuffer());
  const ptr = _malloc(bytes.length);
  HEAPU8.set(bytes, ptr);  // read HEAPU8 after _malloc: memory may have grown
  const ok = _monstres_import_save(ptr, bytes.length);
  _free(ptr);
  if (ok) await monstresSync();
  return !!ok;
}

// ---- Public API (web/controls.js, buttons in index.html, Playwright tests) ----
let monstresReadyResolve;
window.monstres = {
  // Resolved by main() once the game is loaded and the first frame is scheduled.
  ready: new Promise((resolve) => {
    monstresReadyResolve = resolve;
  }),
  isReady: false,

  // Controls. move: x right / y forward in -1..1. look: radians.
  input: {
    move: (x, y) => _monstres_set_move(x, y),
    look: (deltaYaw, deltaPitch) => _monstres_look(deltaYaw, deltaPitch),
    jump: () => _monstres_jump(),
  },
  setCamera: (yaw, pitch) => _monstres_set_camera(yaw, pitch),

  // Creature info panel. pick: point of the canvas in CSS pixels; returns the
  // index of the creature there (now selected) or -1 (selection cleared).
  pick: (clientX, clientY) => {
    const rect = Module.canvas.getBoundingClientRect();
    const x = ((clientX - rect.left) / rect.width) * 2 - 1;
    const y = 1 - ((clientY - rect.top) / rect.height) * 2;
    return _monstres_pick(x, y);
  },
  select: (index) => _monstres_select(index),

  // Game menus (web/menu.js).
  setPaused: (paused) => _monstres_set_paused(paused ? 1 : 0),
  wizard: () => JSON.parse(UTF8ToString(_monstres_wizard_json())),
  setName: (name) => {
    const bytes = lengthBytesUTF8(name) + 1;
    const ptr = _malloc(bytes);
    stringToUTF8(name, ptr, bytes);
    _monstres_set_name(ptr);
    _free(ptr);
    return monstresSync();
  },
  bestiary: () => JSON.parse(UTF8ToString(_monstres_bestiary_json())),
  // Map data, plus the heights of the ground as a Float32Array (a copy).
  map: () => {
    const data = JSON.parse(UTF8ToString(_monstres_map_json()));
    const n = data.points * data.points;
    const ptr = _monstres_heights() >> 2;
    data.heights = n ? HEAPF32.slice(ptr, ptr + n) : new Float32Array(0);
    return data;
  },
  // Portrait of a species (index in the bestiary order of Species) or of the
  // magician (species -1), as ImageData of size x size pixels.
  portrait: (species, size = 256, variant = 0) => {
    const ptr = _monstres_portrait(species, variant, size);
    return new ImageData(new Uint8ClampedArray(HEAPU8.slice(ptr, ptr + size * size * 4)), size, size);
  },
  selected: () => JSON.parse(UTF8ToString(_monstres_selected_json())),
  // Where creature `index` is drawn, in CSS pixels of the page, or null.
  creatureOnScreen: (index) => {
    const p = JSON.parse(UTF8ToString(_monstres_creature_screen_json(index)));
    if (!p) return null;
    const rect = Module.canvas.getBoundingClientRect();
    return { x: rect.left + ((p.x + 1) / 2) * rect.width, y: rect.top + ((1 - p.y) / 2) * rect.height };
  },

  state: () => JSON.parse(UTF8ToString(_monstres_state_json())),
  // Runs ticks with the current input; useful with ?frozen in tests.
  step: (ticks = 1) => _monstres_debug_step(ticks),
  saveNow: () => {
    _monstres_save_now();
    return monstresSync();
  },
  exportSave: monstresExportSave,
  importSave: monstresImportSave,
};

Module.monstresReady = () => {
  window.monstres.isReady = true;
  monstresReadyResolve();
  window.dispatchEvent(new Event('monstres-ready'));
};
