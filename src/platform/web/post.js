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

// ---- The saves: three slots, one of them played (settings menu, web/saves.js) ----

// Calls f with the texts copied into the game's memory (freed afterwards).
function monstresWithStrings(texts, f) {
  const pointers = texts.map((text) => {
    const bytes = lengthBytesUTF8(text) + 1;
    const ptr = _malloc(bytes);
    if (ptr) stringToUTF8(text, ptr, bytes);
    return ptr;
  });
  try {
    return pointers.every((p) => p) ? f(...pointers) : 0;
  } finally {
    pointers.forEach((p) => p && _free(p));
  }
}

// After another world came (another save played, a new game): the page forgets the one before.
function monstresWorldChanged() {
  window.dispatchEvent(new Event('monstres-world'));
}

// A magician's look as the game's number (look_code in src/core/world.cpp).
function monstresLookCode(l = {}) {
  const b = (v, bits, shift) => ((Number(v) || 0) & ((1 << bits) - 1)) << shift;
  return b(l.woman ? 1 : 0, 1, 0) | b(l.skin, 3, 1) | b(l.hair, 2, 4) | b(l.hairColor, 3, 6) | b(l.eyes, 3, 9) |
    b(l.staff, 1, 12) | b(l.robe, 3, 13) | b(l.headwear, 2, 16);
}

const monstresSaves = () => JSON.parse(UTF8ToString(_monstres_saves_json()));
const monstresCurrentSlot = () => (monstresSaves().find((s) => s.current) || { slot: 0 }).slot;

// options.slot: which save (the one played by default).
async function monstresExportSave(options = {}) {
  const slot = options.slot === undefined ? monstresCurrentSlot() : options.slot;
  const len = _monstres_export_prepare(slot);
  if (!len) return 'none';
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

// A save is a few megabytes at most: a bigger file is not one (and is not
// read into memory at all).
const MONSTRES_MAX_SAVE_BYTES = 8 * 1024 * 1024;

// Into the empty slot `slot` (not played yet). False if the file is not a
// save, or the slot is taken.
async function monstresImportSave(file, slot) {
  if (!file || file.size === 0 || file.size > MONSTRES_MAX_SAVE_BYTES) return false;
  const bytes = new Uint8Array(await file.arrayBuffer());
  const ptr = _malloc(bytes.length);
  if (!ptr) return false;
  HEAPU8.set(bytes, ptr);  // read HEAPU8 after _malloc: memory may have grown
  const ok = _monstres_import_save(ptr, bytes.length, slot);
  _free(ptr);
  if (ok) await monstresSync();
  return !!ok;
}

// ---- Commands: sent by name, the numbers are the game's (Command) ----
let monstresCommands = null;
function monstresCommand(name, target = 0) {
  if (!monstresCommands) {
    monstresCommands = new Map(JSON.parse(UTF8ToString(_monstres_command_names_json())).map((n, i) => [n, i]));
  }
  const id = monstresCommands.get(name);
  if (id === undefined) throw new Error(`Commande inconnue : ${name}`);
  _monstres_command(id, target);
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
    jumpHeld: (held) => _monstres_jump_held(held ? 1 : 0),  // a flying companion climbs while it is held
    attack: (slot) => _monstres_attack(slot),  // battle: 1 quick, 2 and 3 special
    parry: () => _monstres_parry(),
  },
  // Context menu (web/battle.js): start a battle against creature `index`,
  // flee, capture. Applied with the next tick.
  engage: (index) => monstresCommand('engage', index),
  flee: () => monstresCommand('flee', 0),
  capture: () => monstresCommand('capture', 0),
  // Capturing (the circle of runes, web/battle.js): rune `k` touched.
  rune: (k) => monstresCommand('rune', k),
  // Team bar (web/team.js): team member `slot` out of the crystal, where the
  // companion is (it goes back in); the one already out goes back in.
  summon: (slot) => monstresCommand('summon', slot),
  // Team sheet (web/menu.js): let team[slot] go back to the wild.
  release: (slot) => monstresCommand('release', slot),
  // Context menu on the companion: train against the magician (stopTraining ends it).
  train: () => monstresCommand('train', 0),
  stopTraining: () => monstresCommand('flee', 0),
  // Battle: aim at foes[slot] (-1: at no one, the companion moves freely).
  target: (slot) => monstresCommand('target', slot < 0 ? 65535 : slot),
  // Trainers: challenge (npc), readyToFight once the rules are read, forfeit.
  challenge: (npc) => monstresCommand('challenge', npc),
  readyToFight: () => monstresCommand('ready', 0),
  forfeit: () => monstresCommand('flee', 0),
  // Spells (web/spells.js): cast slot 0 or 1 (combat spells in a battle, else
  // adventure ones); equip a spell (its value, 0: none) in slot 0..3 (0, 1:
  // combat; 2, 3: adventure), out of battles; what they are.
  cast: (slot) => monstresCommand('cast', slot),
  equipSpell: (slot, spell) => monstresCommand('equipSpell', slot * 16 + spell),
  spells: () => JSON.parse(UTF8ToString(_monstres_spells_json())),
  // The healer (Odette): heals the team.
  healTeam: (npc) => monstresCommand('healTeam', npc),
  // Through the door the player stands at (in, out, up, down: interior.hpp).
  door: () => monstresCommand('door', 0),
  // The bag (web/menu.js): its items; drink a mana potion; throw some away
  // (count 0: all; never a quest's).
  bag: () => JSON.parse(UTF8ToString(_monstres_bag_json())),
  useItem: (item) => monstresCommand('useItem', item),
  discard: (item, count = 0) => monstresCommand('discard', item + 256 * count),
  // The merchant (Anselme, once his cart stands again): what he sells; buy one.
  shop: () => JSON.parse(UTF8ToString(_monstres_shop_json())),
  buy: (item) => monstresCommand('buy', item),
  // The farm (Capucine): the team and the monsters kept there; swap team[slot]
  // with farm[kept] (slot -1: take it back; kept -1: leave team[slot] there).
  farm: () => JSON.parse(UTF8ToString(_monstres_farm_json())),
  farmSwap: (slot, kept) => monstresCommand('farmSwap', (slot < 0 ? 7 : slot) + 8 * (kept < 0 ? 8191 : kept)),
  // Debug (settings menu): time of day in hours; hold: it stands still.
  setHour: (hour) => _monstres_set_hour(hour),
  holdHour: (hold) => _monstres_hold_hour(hold ? 1 : 0),
  // Debug: the weather around the player: -1 the world's own, 0..6 clear,
  // fewClouds, overcast, rain, storm, fog, snow; 7 fine here and a storm
  // ahead. What it is now: state().weather.
  setWeather: (choice) => _monstres_set_weather(choice),
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
  // The villagers with a quest to take or going on, healing, battles (the map's icons).
  marks: () => JSON.parse(UTF8ToString(_monstres_marks_json())),
  teleportTo: (id) => monstresCommand('teleport', id),
  acceptQuest: (quest) => monstresCommand('questAccept', quest),
  giveMonster: (slot) => monstresCommand('questGive', slot),
  completeQuest: (quest) => monstresCommand('questComplete', quest),
  // Frame profiler (settings menu): where the time of the frames goes, every half second.
  setProfiling: (on) => _monstres_set_profiling(on ? 1 : 0),
  profile: () => JSON.parse(UTF8ToString(_monstres_profile_json())),
  // Settings menu: lower the resolution when frames are slow (on by default).
  setAdaptiveResolution: (on) => _monstres_set_adaptive_resolution(on ? 1 : 0),
  // Settings menu, debug: forward or deferred shading, to compare (bit 1: deferred shading).
  setRenderOptions: (bits) => _monstres_set_render_options(bits),

  // Game menus (web/menu.js).
  setPaused: (paused) => _monstres_set_paused(paused ? 1 : 0),
  wizard: () => JSON.parse(UTF8ToString(_monstres_wizard_json())),
  bestiary: () => JSON.parse(UTF8ToString(_monstres_bestiary_json())),
  // Map data, plus the heights of the ground as a Float32Array (a copy).
  map: () => {
    const data = JSON.parse(UTF8ToString(_monstres_map_json()));
    const n = data.points * data.points;
    const ptr = _monstres_heights() >> 2;
    data.heights = n ? HEAPF32.slice(ptr, ptr + n) : new Float32Array(0);
    return data;
  },
  // The continent around the playable area, plus its heights as a
  // Float32Array (a copy, about 1 MB: ask once per world).
  continent: () => {
    const data = JSON.parse(UTF8ToString(_monstres_continent_json()));
    const n = data.ready ? data.pointsX * data.pointsZ : 0;
    const ptr = _monstres_continent_heights() >> 2;
    data.heights = n ? HEAPF32.slice(ptr, ptr + n) : new Float32Array(0);
    return data;
  },
  // Portrait of a species (index in the bestiary order of Species) or of the
  // magician (species -1), as ImageData of size x size pixels. look: the
  // genes of a monster ("look" in the bestiary and creature JSON), 0 = the
  // most common one.
  portrait: (species, size = 256, look = 0) => {
    size = Math.max(1, Math.min(1024, Math.floor(size) || 1));  // as the game clamps it
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
  // The saves (web/saves.js): the three slots; play another one; remove one
  // (not the one played); a new game in an empty slot, with the magician's
  // name and the world's seed (a text, '' for a world at random).
  saves: monstresSaves,
  playSave: async (slot) => {
    if (!_monstres_play_slot(slot)) return false;
    monstresWorldChanged();
    await monstresSync();
    return true;
  },
  deleteSave: async (slot) => {
    if (!_monstres_delete_slot(slot)) return false;
    await monstresSync();
    return true;
  },
  // look: { woman, skin, hair, hairColor, eyes, staff, robe, headwear } (indices);
  // starter: the species' index (Species), one of the three starters.
  newGame: async (slot, name, seed = '', look = {}, starter = 0) => {
    const code = monstresLookCode(look);
    const ok = monstresWithStrings([String(name), String(seed)], (n, s) => _monstres_new_game(slot, n, s, code, starter));
    if (!ok) return false;
    monstresWorldChanged();
    await monstresSync();
    return true;
  },
  // The character creation: needed first (the very first visit, no save);
  // the magician as it would look (look as for newGame; bareHead: without
  // its hat or hood), seen from cam { yaw, pitch, zoom, panX, panY }, as
  // ImageData of width x height; the arrival's flight after a new game.
  needsCreation: () => !!_monstres_needs_creation(),
  wizardPreview: (look, bareHead, cam = {}, time = 0, width = 256, height = 256) => {
    width = Math.max(1, Math.min(1024, Math.floor(width) || 1));
    height = Math.max(1, Math.min(1024, Math.floor(height) || 1));
    const ptr = _monstres_wizard_preview(monstresLookCode(look), bareHead ? 1 : 0, cam.yaw || 0, cam.pitch || 0,
      cam.zoom || 1, cam.panX || 0, cam.panY || 0, time, width, height);
    return new ImageData(new Uint8ClampedArray(HEAPU8.slice(ptr, ptr + width * height * 4)), width, height);
  },
  setCovered: (covered) => _monstres_set_covered(covered ? 1 : 0),
  introOn: () => !!_monstres_intro_on(),
  skipIntro: () => _monstres_skip_intro(),
  exportSave: monstresExportSave,
  importSave: monstresImportSave,
};

Module.monstresReady = () => {
  window.monstres.isReady = true;
  monstresReadyResolve();
  window.dispatchEvent(new Event('monstres-ready'));
};
