// Interface over the 3D view: settings menu (gear, top right), frame counter,
// and the info panel of the creature that was tapped or clicked.
(() => {
  'use strict';

  const { whenReady, prefs, setText } = window.MonstresUtil;
  let game = null;
  whenReady((g) => {
    game = g;
    // index.js older than this page (stale cache): say so instead of failing silently.
    if (typeof game.pick !== 'function') {
      console.error('Monstres : le jeu (index.js) est plus ancien que la page. Rechargez la page.');
      const box = document.getElementById('message');
      box.textContent = 'Une mise à jour du jeu est arrivée : rechargez la page.';
      box.hidden = false;
    }
  });

  // ---- Settings menu ----
  const button = document.getElementById('settings-button');
  const menu = document.getElementById('settings');

  function setMenu(open) {
    menu.hidden = !open;
    button.setAttribute('aria-expanded', String(open));
  }
  button.addEventListener('click', () => setMenu(menu.hidden));
  // Close when touching anything else, or with Escape.
  document.addEventListener('pointerdown', (event) => {
    if (!menu.hidden && !menu.contains(event.target) && !button.contains(event.target)) setMenu(false);
  });
  window.addEventListener('keydown', (event) => {
    if (event.key !== 'Escape') return;
    if (!menu.hidden) setMenu(false);
    else if (!signPanel.hidden) showSign(-1);
    else if (!panel.hidden) select(-1);
  });

  // The saves have their own window (saves.js): the menu gives way to it.
  document.getElementById('saves-open').addEventListener('click', () => setMenu(false));
  // Another world is played: nothing of the one before stays selected.
  window.addEventListener('monstres-world', () => {
    setMenu(false);
    select(-1);
    showSign(-1);
  });

  // ---- Debug: time of day ----
  const hourInput = document.getElementById('hour');
  const hourValue = document.getElementById('hour-value');
  const holdHour = document.getElementById('hold-hour');
  const clockText = (hour) => {
    const minutes = Math.floor(hour * 60) % (24 * 60);
    return `${Math.floor(minutes / 60)}:${String(minutes % 60).padStart(2, '0')}`;
  };
  let dragging = false;
  hourInput.addEventListener('input', () => {
    dragging = true;
    hourValue.textContent = clockText(Number(hourInput.value));
    if (game) game.setHour(Number(hourInput.value));
  });
  hourInput.addEventListener('change', () => { dragging = false; });
  holdHour.addEventListener('change', () => game && game.holdHour(holdHour.checked));
  // While the menu is open, the slider follows the time going by.
  setInterval(() => {
    if (menu.hidden || dragging || !game || typeof game.state !== 'function') return;
    const s = game.state();
    hourInput.value = String(s.hour);
    hourValue.textContent = clockText(s.hour);
    if (s.weather) weatherValue.textContent = weatherText(s.weather.choice + 1, s.weather.kind);
  }, 500);

  // ---- Debug: the weather ----
  // The slider: 0 the world's own weather, then each kind, then a storm afar
  // (setWeather: -1, 0..6, 7).
  const WEATHER = ['clear', 'fewClouds', 'overcast', 'rain', 'storm', 'fog', 'snow'];
  const WEATHER_NAMES = {
    clear: 'Ensoleillé', fewClouds: 'Quelques nuages', overcast: 'Couvert', rain: 'Pluie',
    storm: 'Tempête, orage', fog: 'Brouillard', snow: 'Neige',
  };
  const weatherInput = document.getElementById('weather');
  const weatherValue = document.getElementById('weather-value');
  const weatherText = (position, now) => {
    const k = Number(position);
    if (k === 0) return `Naturelle${now ? ` · ${(WEATHER_NAMES[now] || now).toLowerCase()}` : ''}`;
    return k <= WEATHER.length ? WEATHER_NAMES[WEATHER[k - 1]] : 'Orage au loin';
  };
  weatherInput.addEventListener('input', () => {
    weatherValue.textContent = weatherText(weatherInput.value);
    if (game) game.setWeather(Number(weatherInput.value) - 1);
  });

  // ---- Frames per second, and where their time goes ----
  const fpsBox = document.getElementById('fps');
  const fpsCheck = document.getElementById('show-fps');
  const detailCheck = document.getElementById('show-profile');
  let fpsFrames = 0;
  let fpsSince = 0;
  let fpsLoop = 0;
  let fpsText = '… FPS';
  let detail = false;

  function countFrame(now) {
    if (!fpsSince) fpsSince = now;
    ++fpsFrames;
    if (now - fpsSince >= 500) {
      fpsText = `${Math.round((fpsFrames * 1000) / (now - fpsSince))} FPS`;
      fpsFrames = 0;
      fpsSince = now;
      if (detail) renderProfile();
      else fpsBox.textContent = fpsText;
    }
    fpsLoop = requestAnimationFrame(countFrame);
  }

  const ms = (v) => `${v < 10 ? v.toFixed(1) : Math.round(v)} ms`;
  const thousands = (n) => (n >= 10000 ? `${Math.round(n / 1000)} k` : n >= 1000 ? `${(n / 1000).toFixed(1)} k` : `${n}`);
  const PARTS = [
    ['sim', 'Logique du jeu', 'simulation : déplacements, monstres, combats'],
    ['scene', 'Scène', 'personnages, animations et effets, refaits à chaque image'],
    ['view', 'Caméra et lumières', ''],
    ['draw', 'Envoi au GPU', 'commandes WebGL, données des instances'],
    ['landscape', 'Paysage', 'reconstruit quand tu avances de 16 m'],
    ['save', 'Sauvegarde', 'toutes les 5 s'],
  ];
  const GROUPS = { terrain: 'Sol, chemin, eau', vegetation: 'Herbe, fleurs', scenery: 'Décor (arbres, maisons…)',
    characters: 'Personnages', effects: 'Ombres, effets', sky: 'Nuages, montagnes' };

  function row(label, value, ratio, title, worst) {
    const bar = document.createElement('span');
    bar.className = 'profile-bar';
    bar.style.width = `${Math.min(100, Math.max(0, ratio * 100))}%`;
    const line = document.createElement('div');
    line.className = 'profile-row';
    line.title = title || '';
    const name = document.createElement('span');
    name.textContent = label;
    const v = document.createElement('b');
    v.textContent = value;
    const w = document.createElement('small');
    w.textContent = worst || '';
    line.append(name, v, w, bar);
    return line;
  }

  // The last half second, measured by the game (see monstres_profile_json).
  function renderProfile() {
    const p = game && typeof game.profile === 'function' ? game.profile() : null;
    const head = document.createElement('div');
    head.className = 'profile-head';
    head.textContent = fpsText;
    if (!p) {
      fpsBox.replaceChildren(head, document.createTextNode('Mesure en cours…'));
      return;
    }
    const frame = p.avg.interval;
    head.textContent = `${fpsText} · ${ms(frame)} par image (pire ${ms(p.worst.interval)})`;
    const rows = [];
    const section = (text) => Object.assign(document.createElement('div'), { className: 'profile-section', textContent: text });
    rows.push(section(`Processeur (jeu, WebAssembly) : ${ms(p.avg.wasm)}`));
    for (const [key, label, title] of PARTS) {
      let extra = '';
      if (key === 'sim') extra = ` · ${(p.ticks / p.frames).toFixed(1)} pas/image`;
      if (key === 'landscape' && p.landscapeBuilds) extra = ` · ${p.landscapeBuilds}×`;
      rows.push(row(label, ms(p.avg[key]), frame ? p.avg[key] / frame : 0, title, extra.replace(/^ · /, '')));
    }
    const gpu = p.gpuMs;
    rows.push(section(gpu === null ? 'Carte graphique : temps non mesurable sur ce navigateur' : `Carte graphique : ${ms(gpu)}`));
    if (gpu !== null) rows.push(row('Dessin (GPU)', ms(gpu), frame ? gpu / frame : 0, 'sommets et pixels'));
    const rest = Math.max(0, frame - p.avg.wasm);
    rows.push(row('Hors du jeu', ms(rest), frame ? rest / frame : 0,
      'attente de l’écran (vsync), du GPU, et le reste de la page (interface, navigateur)'));
    rows.push(section(`Triangles : ${thousands(p.triangles)} · ${p.drawCalls} appels · ${thousands(p.instances)} objets`));
    const most = Math.max(1, ...Object.values(p.trianglesBy));
    for (const [key, label] of Object.entries(GROUPS)) {
      rows.push(row(label, thousands(p.trianglesBy[key] || 0), (p.trianglesBy[key] || 0) / most));
    }
    const [w, h] = p.pixels;
    rows.push(section('Écran'));
    rows.push(row('Pixels', `${w}×${h}`, 0, 'taille du dessin (écran × densité)', `×${(+p.dpr).toFixed(2)} · ${thousands(w * h)}`));
    const scale = p.renderScale ?? 1;
    rows.push(row('Résolution', `${Math.round(scale * 100)} %`, scale, 'baissée automatiquement quand le jeu ralentit (paramètres)'));
    rows.push(row('Sol détaillé', `${p.tilesBuilt} tuiles`, 0, 'tuiles du sol construites en avance depuis le début',
      `${p.grassChunks} morceaux d’herbe`));
    rows.push(row('Lumières', `${p.lights}`, p.lights / 12, 'lanternes, cristaux… (12 au plus)'));
    rows.push(row('Créatures', `${p.creatures}`, 0, 'simulées autour de toi', `${p.objects} objets`));
    rows.push(row('Envoyé au GPU', `${p.uploadedKiB} Kio`, 0, 'données des instances (personnages, effets ; paysage et tuiles du sol quand ils changent)'));
    // What limits the frame rate, in a few words.
    let hint;
    if (p.avg.interval < 18) hint = 'Fluide : rien ne freine.';
    else if (gpu !== null && gpu > p.avg.wasm) hint = 'Limité par la carte graphique : moins de pixels ou de triangles aideraient.';
    else if (p.avg.wasm > rest) hint = `Limité par le processeur : surtout « ${PARTS.reduce((a, b) => (p.avg[b[0]] > p.avg[a[0]] ? b : a))[1]} ».`;
    else hint = 'Le jeu attend : sans doute la carte graphique (pixels, triangles) ou le navigateur.';
    rows.push(Object.assign(document.createElement('div'), { className: 'profile-hint', textContent: hint }));
    fpsBox.replaceChildren(head, ...rows);
  }

  function showFps(on) {
    fpsBox.hidden = !on;
    fpsCheck.checked = on;
    detailCheck.disabled = !on;
    cancelAnimationFrame(fpsLoop);
    setDetail(on && detailCheck.checked);
    if (on) {
      fpsBox.textContent = '… FPS';
      fpsFrames = 0;
      fpsSince = 0;
      fpsLoop = requestAnimationFrame(countFrame);
    }
  }
  function setDetail(on) {
    detail = on;
    fpsBox.classList.toggle('detail', on);
    whenReady(() => {
      if (typeof window.monstres.setProfiling === 'function') window.monstres.setProfiling(on);
    });
  }
  fpsCheck.addEventListener('change', () => {
    showFps(fpsCheck.checked);
    prefs.set('showFps', fpsCheck.checked);
  });
  detailCheck.addEventListener('change', () => {
    setDetail(detailCheck.checked && fpsCheck.checked);
    prefs.set('showProfile', detailCheck.checked);
  });
  detailCheck.checked = prefs.get('showProfile', false) === true;

  // ---- Resolution lowered when frames are slow (on by default) ----
  const adaptiveCheck = document.getElementById('adaptive-resolution');
  const setAdaptive = (on) => whenReady(() => {
    if (typeof window.monstres.setAdaptiveResolution === 'function') window.monstres.setAdaptiveResolution(on);
  });
  adaptiveCheck.checked = prefs.get('adaptiveResolution', true) !== false;
  setAdaptive(adaptiveCheck.checked);
  adaptiveCheck.addEventListener('change', () => {
    setAdaptive(adaptiveCheck.checked);
    prefs.set('adaptiveResolution', adaptiveCheck.checked);
  });
  showFps(prefs.get('showFps', false) === true);

  // ---- Creature info ----
  const SPECIES = window.MonstresLore.species;  // web/lore.js
  const LORE = window.MonstresLore;

  function activity(c) {
    if (c.species === 'fish') {
      if (!c.onGround) return 'Saute hors de l’eau !';
      return c.mind === 'wander' ? 'Nage' : 'Flotte';
    }
    if (c.species === 'crow') return c.mind === 'fly' ? 'Vole' : 'Perché';
    switch (c.mind) {
      case 'follow': return c.tag === 'rival' ? `Suit ${(LORE.npcs[c.owner] || { name: 'son maître' }).name}` : 'Te suit';
      case 'greet': return 'Te salue';
      case 'flee': return 'S’enfuit';
      case 'battle': return 'Se bat !';
      case 'knockedOut': return 'K.O.';
      case 'wander': return 'Se promène';
      default: return c.index === 0 ? 'T’attend' : 'Se repose';
    }
  }

  const panel = document.getElementById('creature');
  const fields = {
    name: panel.querySelector('.name'),
    kind: panel.querySelector('.kind'),
    facts: panel.querySelector('.facts'),
    tags: panel.querySelector('.tags'),
    variants: panel.querySelector('.variants'),
  };
  let shownLook = null;  // family, elements and variants change only with the creature
  let refresh = 0;

  function update() {
    const c = game && game.selected();
    // A battle or a training that starts closes it.
    if (!c || game.battle().phase !== 'none') {
      select(-1);
      return;
    }
    // The essentials: a villager's name and what it is; a monster's name,
    // its level and whose it is ("niv. 5 (à toi)", as in the actions' menu).
    const info = SPECIES[c.species] || { name: c.species };
    const person = c.species === 'villager';
    if (person) {
      setText(fields.name, c.npc && LORE.npcs[c.npc] ? LORE.npcs[c.npc].name : info.name);
    } else {
      const owner = c.owner === 0 ? ' (à toi)' : c.owner > 0 ? ` (à ${(LORE.npcs[c.owner] || { name: '?' }).name})` : '';
      const level = c.level ? `niv. ${c.level}${owner}` : '';
      if (fields.name.dataset.shown !== `${info.name}/${level}`) {
        fields.name.dataset.shown = `${info.name}/${level}`;
        fields.name.replaceChildren(`${info.name} `, ...(level ? [Object.assign(document.createElement('small'), { textContent: level })] : []));
      }
    }
    if (person) delete fields.name.dataset.shown;
    setText(fields.kind, !person ? '' : c.wanderer ? (c.aggressive ? 'Dresseur agressif' : 'Dresseur de passage') : 'Villageois');
    const look = `${c.index}/${c.look}`;
    if (look !== shownLook) {
      shownLook = look;
      const badge = (text, color) => Object.assign(document.createElement('span'), {
        className: 'badge', textContent: text, style: `background:${color}`,
      });
      fields.tags.replaceChildren(...(c.family ? [badge(LORE.families[c.family] || c.family, 'rgba(255,255,255,0.85)')] : []),
        ...(c.elements || []).map((id) => badge((LORE.elements[id] || { name: id }).name, (LORE.elements[id] || {}).color || '#ccc')));
      // Its variants: its sex as a sign (blue, pink), the rare forms, then its
      // colors and shapes, each in the color of its rarity.
      const texts = info.variants || {};
      const entry = (c.variants || []).length ? (game.bestiary().find((e) => e.species === c.species) || { variants: [] })
        : { variants: [] };
      const ids = [...(c.variants || [])].sort((a, b) => (b.includes('.') ? 0 : 1) - (a.includes('.') ? 0 : 1));
      const build = (id) => LORE.builds[id] && `${LORE.builds[id].sign} ${LORE.builds[id].name}`;
      fields.variants.replaceChildren(...ids.map((id) => {
        if (id === 'sex.male' || id === 'sex.female') {
          const female = id === 'sex.female';
          return Object.assign(document.createElement('span'), {
            className: `sex ${female ? 'female' : 'male'}`, textContent: female ? '♀' : '♂', title: female ? 'Femelle' : 'Mâle',
          });
        }
        const v = entry.variants.find((x) => x.id === id) || {};
        const span = document.createElement('span');
        span.textContent = (id.includes('.') ? '' : '✦ ') + ((texts[id] || {}).name || build(id) || id);
        span.title = (LORE.rarities[v.rarity] || { name: '' }).name;
        span.style.setProperty('--rarity', (LORE.rarities[v.rarity] || { color: '#c9c4d8' }).color);
        return span;
      }));
    }
    const meters = Math.round(c.distance);
    const released = c.released ? ' · relâché' : '';
    setText(fields.facts, `${activity(c)}${released} · ${meters < 1 ? 'tout près' : `à ${meters} m`}`);
  }

  function select(index) {
    clearInterval(refresh);
    shownLook = null;
    if (game) game.select(index);
    panel.hidden = index < 0;
    window.dispatchEvent(new CustomEvent('monstres-selection', { detail: { index } }));
    if (index >= 0) {
      update();
      refresh = setInterval(update, 250);  // activity and distance change
    }
  }

  // ---- Signposts: what they say ----
  const signPanel = document.getElementById('sign');
  const ARROWS = ['↑', '↗', '→', '↘', '↓', '↙', '←', '↖'];
  const COMPASS = ['nord', 'nord-est', 'est', 'sud-est', 'sud', 'sud-ouest', 'ouest', 'nord-ouest'];
  let signWatch = 0;

  function showSign(index) {
    clearInterval(signWatch);
    signPanel.hidden = index < 0;
    if (index < 0) return;
    const sign = game.object(index);
    if (!sign || sign.kind !== 'sign') return;
    const here = sign.zone === 'village' ? LORE.signs.village : LORE.signs.other;
    const lines = sign.places.filter((p) => !(p.zone === sign.zone && p.distance < 25)).map((p) => {
      // North is -Z (the top of the map).
      const angle = Math.atan2(p.dx, -p.dz);
      const k = ((Math.round(angle / (Math.PI / 4)) % 8) + 8) % 8;
      const li = document.createElement('li');
      const arrow = document.createElement('span');
      arrow.className = 'sign-arrow';
      arrow.textContent = ARROWS[k];
      arrow.title = COMPASS[k];
      const name = document.createElement('span');
      name.textContent = LORE.zones[p.zone] || p.zone;
      const far = document.createElement('small');
      far.textContent = `${Math.round(p.distance / 5) * 5} m`;
      li.append(arrow, name, far);
      return li;
    });
    signPanel.querySelector('.sign-title').textContent = here;
    signPanel.querySelector('.sign-places').replaceChildren(...lines);
    // Walking away puts it back.
    signWatch = setInterval(() => {
      const s = game.state().player;
      if (Math.hypot(s.x - sign.x, s.z - sign.z) > 30) showSign(-1);
    }, 500);
  }

  // Tap or click on the view (see controls.js): the creature there, a
  // signpost or a teleporter (its menu: battle.js), or nothing.
  window.addEventListener('monstres-tap', (event) => {
    if (!game || typeof game.pick !== 'function') return;
    const phase = game.battle().phase;
    if (phase === 'training') {  // a tap on the magician aims at it, or not
      const index = game.pick(event.detail.x, event.detail.y);
      game.select(-1);
      const p = game.creatureOnScreen(-1);
      if (index < 0 && p && Math.hypot(p.x - event.detail.x, p.y - event.detail.y) < 60) {
        window.dispatchEvent(new CustomEvent('monstres-wizard-tap'));
      }
      return;
    }
    if (phase === 'fight' || phase === 'prepare') {
      // In a battle: a tap on a foe aims at it (battle.js), nothing else.
      const foe = game.pick(event.detail.x, event.detail.y);
      game.select(-1);
      if (foe >= 0) window.dispatchEvent(new CustomEvent('monstres-foe-tap', { detail: { index: foe } }));
      return;
    }
    const index = game.pick(event.detail.x, event.detail.y);
    select(index);
    const object = index < 0 && typeof game.pickObject === 'function' ? game.pickObject(event.detail.x, event.detail.y) : -1;
    const info = object >= 0 ? game.object(object) : null;
    showSign(info && info.kind === 'sign' ? object : -1);
    window.dispatchEvent(new CustomEvent('monstres-object', {
      detail: { index: info && info.kind === 'teleporter' ? object : -1 },
    }));
  });
  document.getElementById('creature-close').addEventListener('click', () => select(-1));
  document.getElementById('sign-close').addEventListener('click', () => showSign(-1));
})();
