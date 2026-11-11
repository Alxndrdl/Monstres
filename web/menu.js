// The grimoire (button at the top left): character sheet, bestiary and maps.
// Time stands still while it is open. Texts come from web/lore.js, data from
// the game (window.monstres: wizard(), bestiary(), map(), portrait()).
(() => {
  'use strict';

  const LORE = window.MonstresLore;
  let game = null;
  if (window.monstres && window.monstres.isReady) game = window.monstres;
  else window.addEventListener('monstres-ready', () => { game = window.monstres; }, { once: true });

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
        // not remembered, never mind
      }
    },
  };

  const $ = (id) => document.getElementById(id);
  const el = (tag, props = {}, children = []) => {
    const node = Object.assign(document.createElement(tag), props);
    for (const child of children) node.append(child);
    return node;
  };

  // ---- Portraits drawn by the game, kept as images ----
  const portraits = new Map();
  function portraitUrl(species, size = 256) {
    const key = `${species}/${size}`;
    if (!portraits.has(key)) {
      const canvas = el('canvas', { width: size, height: size });
      canvas.getContext('2d').putImageData(game.portrait(species, size), 0, 0);
      portraits.set(key, canvas.toDataURL());
    }
    return portraits.get(key);
  }
  const speciesIndex = (id) => LORE.speciesOrder.indexOf(id);

  // ---- Opening, closing, tabs ----
  const menu = $('game-menu');
  const button = $('menu-button');
  const tabs = [...menu.querySelectorAll('[role="tab"]')];
  let current = prefs.get('menuTab', 'character');

  function isOpen() {
    return !menu.hidden;
  }

  function open(tab = current) {
    if (!game) return;
    menu.hidden = false;
    button.setAttribute('aria-expanded', 'true');
    game.setPaused(true);
    show(tab);
  }

  function close() {
    if (!isOpen()) return;
    menu.hidden = true;
    button.setAttribute('aria-expanded', 'false');
    if (document.activeElement && menu.contains(document.activeElement)) document.activeElement.blur();
    if (game) game.setPaused(false);
  }

  function show(tab) {
    if (!['character', 'bestiary', 'map'].includes(tab)) tab = 'character';
    current = tab;
    prefs.set('menuTab', tab);
    for (const t of tabs) {
      const selected = t.dataset.tab === tab;
      t.setAttribute('aria-selected', String(selected));
      t.tabIndex = selected ? 0 : -1;
      $(t.getAttribute('aria-controls')).hidden = !selected;
    }
    if (tab === 'character') renderCharacter();
    else if (tab === 'bestiary') renderBestiary();
    else renderMap();
  }

  button.addEventListener('click', () => (isOpen() ? close() : open()));
  $('menu-close').addEventListener('click', close);
  menu.addEventListener('pointerdown', (event) => {
    if (event.target === menu) close();  // the dark backdrop around the window
  });
  for (const t of tabs) t.addEventListener('click', () => show(t.dataset.tab));

  // Keyboard: Escape closes; C, B and M open a tab (not while typing a name).
  // By letter, not by key position: M is not at the same place on AZERTY.
  const SHORTCUTS = { c: 'character', b: 'bestiary', m: 'map' };
  window.addEventListener('keydown', (event) => {
    const typing = event.target instanceof HTMLElement && ['INPUT', 'TEXTAREA'].includes(event.target.tagName);
    if (event.key === 'Escape' && isOpen()) {
      event.stopImmediatePropagation();  // not also the settings menu
      close();
    } else if (!typing && !event.ctrlKey && !event.metaKey && !event.altKey && !event.repeat &&
               SHORTCUTS[event.key.toLowerCase()]) {
      const tab = SHORTCUTS[event.key.toLowerCase()];
      if (isOpen() && current === tab) close();
      else open(tab);
    } else if (isOpen() && !typing && (event.key === 'ArrowLeft' || event.key === 'ArrowRight') &&
               tabs.includes(document.activeElement)) {
      const i = tabs.findIndex((t) => t.dataset.tab === current);
      const next = tabs[(i + (event.key === 'ArrowRight' ? 1 : tabs.length - 1)) % tabs.length];
      show(next.dataset.tab);
      next.focus();
    }
  }, true);

  // ---- Character sheet ----
  const STATS = [
    ['hp', 'Points de vie', 60], ['mana', 'Mana', 60], ['power', 'Puissance magique', 30],
    ['wisdom', 'Sagesse', 30], ['agility', 'Agilité', 30], ['endurance', 'Endurance', 30],
  ];
  const nameInput = $('wizard-name');

  function statList(target, entries) {
    target.replaceChildren(...entries.map(([label, value, max]) => el('div', { className: 'stat' }, [
      el('dt', { textContent: label }),
      el('dd', { textContent: String(value) }),
      el('div', { className: 'bar' }, [el('span', { style: `width:${Math.min(100, (100 * value) / max)}%` })]),
    ])));
  }

  function renderCharacter() {
    const w = game.wizard();
    $('wizard-portrait').src = portraitUrl(-1);
    $('companion-portrait').src = portraitUrl(speciesIndex('companion'), 128);
    if (document.activeElement !== nameInput) nameInput.value = w.name;
    $('wizard-rank').textContent = `Apprenti magicien · niveau ${w.level}`;
    $('wizard-xp-fill').style.width = `${Math.min(100, (100 * w.xp) / w.xpToNext)}%`;
    $('wizard-xp-text').textContent = `Expérience : ${w.xp} / ${w.xpToNext}`;
    statList($('wizard-stats'), STATS.map(([key, label, max]) => [label, w[key], max]));
    const zone = game.state().player.zone;
    $('wizard-where').textContent = `Tu te trouves : ${LORE.zones[zone] || zone}.`;
  }

  // The name is saved as it is typed (and when leaving the field).
  let nameTimer = 0;
  const saveName = () => {
    clearTimeout(nameTimer);
    if (game) game.setName(nameInput.value.trim());
  };
  nameInput.addEventListener('input', () => {
    clearTimeout(nameTimer);
    nameTimer = setTimeout(saveName, 400);
  });
  nameInput.addEventListener('change', saveName);
  nameInput.addEventListener('keydown', (event) => {
    if (event.key === 'Enter') nameInput.blur();
  });

  // ---- Bestiary ----
  const MONSTER_STATS = [
    ['hp', 'Points de vie', 50], ['attack', 'Attaque', 15], ['defense', 'Défense', 15],
    ['speed', 'Vitesse', 15], ['magic', 'Magie', 15],
  ];
  let chosen = prefs.get('bestiarySpecies', 'companion');

  function renderBestiary() {
    const entries = game.bestiary();
    if (!entries.some((e) => e.species === chosen)) chosen = entries[0].species;
    const list = $('bestiary-list');
    list.replaceChildren(...entries.map((e) => {
      const info = LORE.species[e.species];
      const item = el('button', { type: 'button', className: 'bestiary-entry' }, [
        el('img', { className: 'portrait', src: portraitUrl(speciesIndex(e.species), 96), alt: '' }),
        el('span', {}, [el('strong', { textContent: info.name }), el('small', { textContent: info.kind })]),
      ]);
      item.dataset.species = e.species;
      if (e.species === chosen) item.setAttribute('aria-current', 'true');
      item.addEventListener('click', () => {
        chosen = e.species;
        prefs.set('bestiarySpecies', chosen);
        renderBestiary();
      });
      return el('li', {}, [item]);
    }));

    const e = entries.find((x) => x.species === chosen);
    const info = LORE.species[e.species];
    const element = LORE.elements[e.element] || { name: e.element, color: '#ccc' };
    const stats = el('dl', { className: 'stats' });
    statList(stats, MONSTER_STATS.map(([key, label, max]) => [label, e[key], max]));
    const count = e.species === 'companion' ? 'Il n’y en a qu’un : le tien.'
      : `Dans ce monde : ${e.count} ${e.count > 1 ? 'individus' : 'individu'}.`;
    $('bestiary-detail').replaceChildren(
      el('img', { className: 'portrait', src: portraitUrl(speciesIndex(e.species)), alt: info.name }),
      el('div', {}, [
        el('h2', { textContent: info.name }),
        el('p', { className: 'kind', textContent: info.kind }),
        el('span', { className: 'badge', textContent: element.name, style: `background:${element.color}` }),
        el('p', { className: 'description', textContent: info.description, style: 'margin-top:10px' }),
        el('p', { className: 'facts', textContent: `Habitat : ${info.habitat}` }),
        el('p', {
          className: 'facts',
          textContent: `Taille : ${e.height.toFixed(1).replace('.', ',')} m · ` +
            `vitesse : ${e.walkSpeed.toFixed(1).replace('.', ',')} à ${e.runSpeed.toFixed(1).replace('.', ',')} m/s`,
        }),
        el('p', { className: 'facts', textContent: count }),
      ]),
      stats,
    );
  }

  // ---- Maps ----
  const canvas = $('map-canvas');
  const PX_PER_M = 4;           // resolution of the drawn terrain
  const LOCAL_PX_PER_M = 7;     // zoom of the local map (CSS pixels per meter)
  let mapMode = prefs.get('mapMode', 'local');
  let base = null;              // { seed key, image (canvas), data }

  for (const b of document.querySelectorAll('[data-map]')) {
    b.addEventListener('click', () => {
      mapMode = b.dataset.map;
      prefs.set('mapMode', mapMode);
      renderMap();
    });
  }
  new ResizeObserver(() => {
    if (isOpen() && current === 'map') renderMap();
  }).observe(canvas);

  const OBJECT_COLORS = {
    tree: 'rgba(38, 92, 44, 0.85)', pine: 'rgba(24, 70, 48, 0.9)', bush: 'rgba(70, 130, 60, 0.9)',
    rock: '#8a8a90', log: '#7a5534', stump: '#7a5534', crate: '#a07040',
    cottage: '#b8553a', chimney: '#56607a', hut: '#d0ab5a', well: '#8a8a90', fence: '#8a6a44',
    sign: '#8a6a44', barrel: '#8a6a44', lamp: '#555',
    tower: '#9a958c', ruinWall: '#8f8a82', pillar: '#a8a39a', fallenPillar: '#a8a39a', rubble: '#7f7a74',
    runeStone: '#7fe0ff',
  };

  function heightAt(data, x, z) {
    const n = data.points;
    if (!n) return 0;
    const fx = Math.min(Math.max(x + data.relief, 0), n - 1.001);
    const fz = Math.min(Math.max(z + data.relief, 0), n - 1.001);
    const i = Math.floor(fx), j = Math.floor(fz), tx = fx - i, tz = fz - j;
    const h = (a, b) => data.heights[a * n + b];
    const top = h(i, j) + (h(i + 1, j) - h(i, j)) * tx;
    const bottom = h(i, j + 1) + (h(i + 1, j + 1) - h(i, j + 1)) * tx;
    return top + (bottom - top) * tz;
  }

  // The terrain, path and objects, drawn once per world.
  function buildBase(data) {
    const R = data.world + 7;
    const size = Math.round(2 * R * PX_PER_M);
    const image = el('canvas', { width: size, height: size });
    const ctx = image.getContext('2d');
    const pixels = ctx.createImageData(size, size);
    const lake = data.lake, village = data.village, ruins = data.ruins;
    for (let py = 0; py < size; ++py) {
      for (let px = 0; px < size; ++px) {
        const x = -R + (px + 0.5) / PX_PER_M, z = -R + (py + 0.5) / PX_PER_M;
        const h = heightAt(data, x, z);
        const t = Math.min(Math.max((h + 4) / 12, 0), 1);
        let r = 78 + 70 * t, g = 128 + 46 * t, b = 62 + 30 * t;
        if (village && Math.hypot(x - village.x, z - village.z) < 7) { r = 168; g = 142; b = 104; }
        if (ruins && Math.max(Math.abs(x - ruins.x), Math.abs(z - ruins.z)) < 8.5) { r = 150; g = 146; b = 136; }
        if (lake) {
          const dx = (x - lake.x) / lake.rx, dz = (z - lake.z) / lake.rz;
          const d = dx * dx + dz * dz;
          if (d < 1.08 && h > data.waterLevel - 0.05) { r = 214; g = 196; b = 140; }  // sandy shore
          if (d < 1 && h < data.waterLevel) { const k = Math.min(1, (data.waterLevel - h) / 1.2); r = 80 - 30 * k; g = 150 - 40 * k; b = 196 - 20 * k; }
        }
        // Light from the north-west, shading the slopes.
        const shade = 1 + 0.9 * ((heightAt(data, x - 0.5, z) - heightAt(data, x + 0.5, z)) +
                                 (heightAt(data, x, z - 0.5) - heightAt(data, x, z + 0.5)));
        const outside = Math.max(Math.abs(x), Math.abs(z)) > data.world ? 0.72 : 1;
        const k = Math.min(Math.max(shade, 0.6), 1.35) * outside;
        const o = (py * size + px) * 4;
        pixels.data[o] = r * k; pixels.data[o + 1] = g * k; pixels.data[o + 2] = b * k; pixels.data[o + 3] = 255;
      }
    }
    ctx.putImageData(pixels, 0, 0);
    const toPx = (v) => (v + R) * PX_PER_M;

    // The path.
    ctx.strokeStyle = 'rgba(176, 140, 96, 0.95)';
    ctx.lineWidth = 2.2 * PX_PER_M;
    ctx.lineCap = ctx.lineJoin = 'round';
    ctx.beginPath();
    for (let i = 0; i < data.path.length; i += 2) {
      const X = toPx(data.path[i]), Y = toPx(data.path[i + 1]);
      if (i === 0) ctx.moveTo(X, Y); else ctx.lineTo(X, Y);
    }
    ctx.stroke();

    // Objects: trees as round crowns, the rest as their outline.
    const order = (kind) => (kind === 'tree' || kind === 'pine' ? 1 : 0);  // crowns over the rest
    const objects = [...data.objects].sort((a, b) => order(a[0]) - order(b[0]));
    for (const [kind, x, z, hx, hz] of objects) {
      ctx.fillStyle = OBJECT_COLORS[kind] || '#777';
      if (kind === 'tree' || kind === 'pine' || kind === 'bush' || kind === 'tower' || kind === 'hut' ||
          kind === 'well' || kind === 'rubble' || kind === 'stump') {
        ctx.beginPath();
        ctx.arc(toPx(x), toPx(z), Math.max(hx, 0.3) * PX_PER_M, 0, Math.PI * 2);
        ctx.fill();
        if (kind === 'tower') {
          ctx.lineWidth = 0.5 * PX_PER_M;
          ctx.strokeStyle = '#4a4650';
          ctx.stroke();
        }
      } else {
        const w = Math.max(hx, 0.25), d = Math.max(hz, 0.25);
        ctx.fillRect(toPx(x - w), toPx(z - d), 2 * w * PX_PER_M, 2 * d * PX_PER_M);
      }
    }
    return { image, R };
  }

  const SPECIES_DOT = {
    companion: '#ff9a3c', slime: '#5fd6b8', mushroom: '#f05a4a', bird: '#ffd84a', villager: '#f2e6d0',
    crow: '#2a2a36', fish: '#4aa8ff',
  };

  function renderMap() {
    if (!game) return;
    const data = game.map();
    const key = `${data.path.length}:${data.path[0]}:${data.path[1]}:${data.objects.length}`;
    if (!base || base.key !== key) base = { key, ...buildBase(data) };
    for (const b of document.querySelectorAll('[data-map]')) {
      b.setAttribute('aria-pressed', String(b.dataset.map === mapMode));
    }

    const s = game.state();
    const p = s.player;
    $('map-where').textContent = `Tu te trouves : ${LORE.zones[p.zone] || p.zone}`;
    const dpr = window.devicePixelRatio || 1;
    const cssW = canvas.clientWidth, cssH = canvas.clientHeight;
    if (!cssW || !cssH) return;
    canvas.width = Math.round(cssW * dpr);
    canvas.height = Math.round(cssH * dpr);
    const ctx = canvas.getContext('2d');
    ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
    ctx.fillStyle = '#1d2a22';
    ctx.fillRect(0, 0, cssW, cssH);

    const global = mapMode === 'global';
    const scale = global ? Math.min(cssW, cssH) / (2 * base.R) : LOCAL_PX_PER_M;  // CSS px per meter
    const cx = global ? 0 : p.x, cz = global ? 0 : p.z;
    const X = (x) => cssW / 2 + (x - cx) * scale;
    const Y = (z) => cssH / 2 + (z - cz) * scale;
    ctx.imageSmoothingEnabled = true;
    ctx.drawImage(base.image, X(-base.R), Y(-base.R), 2 * base.R * scale, 2 * base.R * scale);

    // The edge of the world.
    ctx.strokeStyle = 'rgba(255, 255, 255, 0.35)';
    ctx.setLineDash([6, 6]);
    ctx.lineWidth = 1;
    ctx.strokeRect(X(-data.world), Y(-data.world), 2 * data.world * scale, 2 * data.world * scale);
    ctx.setLineDash([]);

    // Creatures: all of them on the local map, only the companion on the global one.
    for (const c of s.creatures) {
      if (global && c.species !== 'companion') continue;
      ctx.beginPath();
      ctx.arc(X(c.x), Y(c.z), c.species === 'companion' ? 4.5 : 3.5, 0, Math.PI * 2);
      ctx.fillStyle = SPECIES_DOT[c.species] || '#fff';
      ctx.fill();
      ctx.lineWidth = 1;
      ctx.strokeStyle = 'rgba(0, 0, 0, 0.6)';
      ctx.stroke();
    }

    // Places.
    const label = (text, x, z, size) => {
      ctx.font = `600 ${size}px system-ui, sans-serif`;
      ctx.textAlign = 'center';
      ctx.textBaseline = 'middle';
      ctx.lineWidth = 3;
      ctx.strokeStyle = 'rgba(20, 18, 36, 0.8)';
      ctx.strokeText(text, X(x), Y(z));
      ctx.fillStyle = '#fff';
      ctx.fillText(text, X(x), Y(z));
    };
    const labelSize = global ? 13 : 15;
    // Above the village, below the lake: they are side by side.
    if (data.village) label(LORE.zones.village, data.village.x, data.village.z - (global ? 12 : 9), labelSize);
    if (data.lake) label(LORE.zones.lake, data.lake.x, data.lake.z + (global ? data.lake.rz + 5 : 0), labelSize - 1);
    if (data.ruins) label(LORE.zones.ruins, data.ruins.x, data.ruins.z - (global ? 9 : 12), labelSize);

    // The player: an arrow pointing where it goes.
    const angle = Math.atan2(p.facingZ, p.facingX);
    ctx.save();
    ctx.translate(X(p.x), Y(p.z));
    ctx.rotate(angle);
    ctx.beginPath();
    ctx.moveTo(10, 0);
    ctx.lineTo(-7, 6.5);
    ctx.lineTo(-3.5, 0);
    ctx.lineTo(-7, -6.5);
    ctx.closePath();
    ctx.fillStyle = '#f2c94c';
    ctx.fill();
    ctx.lineWidth = 1.5;
    ctx.strokeStyle = '#2a2140';
    ctx.stroke();
    ctx.restore();

    // North.
    ctx.font = '700 13px system-ui, sans-serif';
    ctx.textAlign = 'center';
    ctx.fillStyle = 'rgba(20, 18, 36, 0.7)';
    ctx.beginPath();
    ctx.arc(cssW - 22, 22, 14, 0, Math.PI * 2);
    ctx.fill();
    ctx.fillStyle = '#fff';
    ctx.fillText('N', cssW - 22, 23);
  }

  // For tests and other scripts.
  window.monstresMenu = { open, close, isOpen, show };
})();
