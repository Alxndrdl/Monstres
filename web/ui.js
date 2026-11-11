// Interface over the 3D view: settings menu (gear, top right), frame counter,
// and the info panel of the creature that was tapped or clicked.
(() => {
  'use strict';

  let game = null;
  const whenReady = (f) => {
    if (window.monstres && window.monstres.isReady) f();
    else window.addEventListener('monstres-ready', f, { once: true });
  };
  whenReady(() => {
    game = window.monstres;
    // index.js older than this page (stale cache): say so instead of failing silently.
    if (typeof game.pick !== 'function') {
      console.error('Monstres : le jeu (index.js) est plus ancien que la page. Rechargez la page.');
      const box = document.getElementById('message');
      box.textContent = 'Une mise à jour du jeu est arrivée : rechargez la page.';
      box.hidden = false;
    }
  });

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
    else if (!panel.hidden) select(-1);
  });

  // Saves.
  document.getElementById('export').addEventListener('click', () => game && game.exportSave());
  const fileInput = document.getElementById('import-file');
  document.getElementById('import').addEventListener('click', () => fileInput.click());
  fileInput.addEventListener('change', async () => {
    const file = fileInput.files[0];
    fileInput.value = '';
    if (file && game && !(await game.importSave(file))) {
      alert("Ce fichier n'est pas une sauvegarde Monstres valide.");
    } else if (file) {
      setMenu(false);
      select(-1);
    }
  });

  // ---- Frames per second ----
  const fpsBox = document.getElementById('fps');
  const fpsCheck = document.getElementById('show-fps');
  let fpsFrames = 0;
  let fpsSince = 0;
  let fpsLoop = 0;

  function countFrame(now) {
    if (!fpsSince) fpsSince = now;
    ++fpsFrames;
    if (now - fpsSince >= 500) {
      fpsBox.textContent = `${Math.round((fpsFrames * 1000) / (now - fpsSince))} FPS`;
      fpsFrames = 0;
      fpsSince = now;
    }
    fpsLoop = requestAnimationFrame(countFrame);
  }

  function showFps(on) {
    fpsBox.hidden = !on;
    fpsCheck.checked = on;
    cancelAnimationFrame(fpsLoop);
    if (on) {
      fpsBox.textContent = '… FPS';
      fpsFrames = 0;
      fpsSince = 0;
      fpsLoop = requestAnimationFrame(countFrame);
    }
  }
  fpsCheck.addEventListener('change', () => {
    showFps(fpsCheck.checked);
    prefs.set('showFps', fpsCheck.checked);
  });
  showFps(prefs.get('showFps', false) === true);

  // ---- Creature info ----
  const SPECIES = window.MonstresLore.species;  // web/lore.js

  function activity(c) {
    if (c.species === 'fish') {
      if (!c.onGround) return 'Saute hors de l’eau !';
      return c.mind === 'wander' ? 'Nage' : 'Flotte';
    }
    if (c.species === 'crow') return c.mind === 'fly' ? 'Vole' : 'Perché';
    switch (c.mind) {
      case 'follow': return 'Te suit';
      case 'greet': return 'Te salue';
      case 'flee': return 'S’enfuit';
      case 'battle': return 'Se bat !';
      case 'wander': return 'Se promène';
      default: return c.species === 'companion' ? 'T’attend' : 'Se repose';
    }
  }

  const panel = document.getElementById('creature');
  const fields = {
    name: panel.querySelector('.name'),
    kind: panel.querySelector('.kind'),
    description: panel.querySelector('.description'),
    facts: panel.querySelector('.facts'),
    tags: panel.querySelector('.tags'),
    variants: panel.querySelector('.variants'),
  };
  const LORE = window.MonstresLore;
  let shownLook = null;  // family, elements and variants change only with the creature
  let refresh = 0;

  function update() {
    const c = game && game.selected();
    if (!c) {
      select(-1);
      return;
    }
    const info = SPECIES[c.species] || { name: c.species, kind: '', description: '' };
    fields.name.textContent = info.name;
    fields.kind.textContent = info.kind;
    fields.description.textContent = info.description;
    const look = `${c.index}/${c.look}`;
    if (look !== shownLook) {
      shownLook = look;
      const badge = (text, color) => Object.assign(document.createElement('span'), {
        className: 'badge', textContent: text, style: `background:${color}`,
      });
      fields.tags.replaceChildren(...(c.family ? [badge(LORE.families[c.family] || c.family, 'rgba(255,255,255,0.85)')] : []),
        ...(c.elements || []).map((id) => badge((LORE.elements[id] || { name: id }).name, (LORE.elements[id] || {}).color || '#ccc')));
      // Its variants: the rare forms first, then its colors and shapes.
      const texts = info.variants || {};
      const ids = [...(c.variants || [])].sort((a, b) => (b.includes('.') ? 0 : 1) - (a.includes('.') ? 0 : 1));
      const names = ids.map((id) => (id.includes('.') ? '' : '✦ ') + ((texts[id] || {}).name || id));
      fields.variants.textContent = names.length ? `Variantes : ${names.join(' · ')}` : '';
    }
    const meters = Math.round(c.distance);
    const level = c.level && c.species !== 'villager' ? `Niveau ${c.level} · ` : '';
    fields.facts.textContent = `${level}${activity(c)} · ${meters < 1 ? 'tout près' : `à ${meters} m`}`;
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

  // Tap or click on the view (see controls.js): the creature there, or nothing.
  window.addEventListener('monstres-tap', (event) => {
    if (!game || typeof game.pick !== 'function') return;
    select(game.pick(event.detail.x, event.detail.y));
  });
  document.getElementById('creature-close').addEventListener('click', () => select(-1));
})();
