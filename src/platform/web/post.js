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
    jump: () => _monstres_jump(),  // in a battle: dodge
    attack: (slot) => _monstres_attack(slot),  // battle: 1 quick, 2 and 3 special
    parry: () => _monstres_parry(),
  },
  // Context menu (web/battle.js): start a battle against creature `index`,
  // flee, capture. Applied with the next tick.
  engage: (index) => _monstres_command(1, index),
  flee: () => _monstres_command(2, 0),
  capture: () => _monstres_command(3, 0),
  // Team bar (web/team.js): team member `slot` out of the crystal, where the
  // companion is (it goes back in); the one already out goes back in.
  summon: (slot) => _monstres_command(4, slot),
  // Team sheet (web/menu.js): let team[slot] go back to the wild.
  release: (slot) => _monstres_command(5, slot),
  // Context menu on the companion: train against the magician (stopTraining ends it).
  train: () => _monstres_command(6, 0),
  stopTraining: () => _monstres_command(2, 0),
  // Debug (settings menu): time of day in hours; hold: it stands still.
  setHour: (hour) => _monstres_set_hour(hour),
  holdHour: (hold) => _monstres_hold_hour(hold ? 1 : 0),
  team: () => JSON.parse(UTF8ToString(_monstres_team_json())),
  battle: () => JSON.parse(UTF8ToString(_monstres_battle_json())),
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
  // The signpost or teleporter at a point of the canvas (CSS pixels): its
  // index, or -1; and what it is (a signpost: what it says).
  pickObject: (clientX, clientY) => {
    const rect = Module.canvas.getBoundingClientRect();
    const x = ((clientX - rect.left) / rect.width) * 2 - 1;
    const y = 1 - ((clientY - rect.top) / rect.height) * 2;
    return _monstres_pick_object(x, y);
  },
  object: (index) => JSON.parse(UTF8ToString(_monstres_object_json(index))),
  // Teleporters (known, reachable) and quests; what the player does with them.
  teleporters: () => JSON.parse(UTF8ToString(_monstres_teleporters_json())),
  quests: () => JSON.parse(UTF8ToString(_monstres_quests_json())),
  teleportTo: (id) => _monstres_command(7, id),
  acceptQuest: (quest) => _monstres_command(8, quest),
  giveMonster: (slot) => _monstres_command(9, slot),
  completeQuest: (quest) => _monstres_command(10, quest),
  // Frame profiler (settings menu): where the time of the frames goes, every half second.
  setProfiling: (on) => _monstres_set_profiling(on ? 1 : 0),
  profile: () => JSON.parse(UTF8ToString(_monstres_profile_json())),

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
  // magician (species -1), as ImageData of size x size pixels. look: the
  // genes of a monster ("look" in the bestiary and creature JSON), 0 = the
  // most common one.
  portrait: (species, size = 256, look = 0) => {
    const ptr = _monstres_portrait(species, look, size);
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
  // Debug: the player (and its companion) at (x, z).
  teleport: (x, z) => _monstres_debug_teleport(x, z),
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
