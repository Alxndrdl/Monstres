// The character creation (a new game): the magician's name, its look and
// its gear, the first monster of its team, and the world's seed, with the
// magician drawn on the left (turn it, zoom, move it). At the very first
// visit it comes before anything else; later, from the saves (saves.js),
// for an empty slot. Then the game plays the arrival's flight (skippable).
// The rules are in the game (newGame, src/core/world.cpp).
(() => {
  'use strict';

  const LORE = window.MonstresLore;
  const { $, el, whenReady } = window.MonstresUtil;
  let game = null;

  const root = $('create');
  const view = $('create-view');
  const context = view.getContext('2d');
  const steps = [...root.querySelectorAll('.create-step')];
  const tabs = [...root.querySelectorAll('.create-tab')];
  const back = $('create-back');
  const next = $('create-next');
  const note = $('create-note');

  // What can be chosen (indices into the game's lists, WizardLook).
  const SKINS = ['#f5c79e', '#fcd9ba', '#dba378', '#b38059', '#855c40', '#5c3d2b'];
  const HAIRS = ['#5c381f', '#1a171a', '#382417', '#9e421f', '#e0b866', '#dbdbe0', '#4d6bd1', '#c266b3'];
  const EYES = ['#38261a', '#73451f', '#4d8f42', '#4285e0', '#949eae', '#9452c7'];
  const ROBES = ['#3d4dad', '#ad2e33', '#33804d', '#73409e', '#292936', '#e0e0e6', '#7a5233', '#2e858f'];
  const STARTERS = ['companion', 'spark', 'axolotl'];
  const HAIR_STYLES = ['Courts', 'Longs', 'Chignon', 'Rasés'];
  const HEADWEAR = ['Chapeau', 'Capuche', 'Rien'];
  const STAFF = ['Bâton', 'Baguette'];

  let slot = 0;
  let firstVisit = false;
  let step = 0;
  let look = {};
  let starter = 'companion';
  const camera = { yaw: 0.35, pitch: 0.12, zoom: 0.85, panX: 0, panY: 0 };
  let dirty = true;
  let shownAt = 0;

  const isOpen = () => !root.hidden;
  const randomSeed = () => String(Math.floor(Math.random() * 1e9));

  // ---- The magician, drawn by the game ----
  const scratch = document.createElement('canvas');
  function draw(now) {
    if (!isOpen()) return;
    requestAnimationFrame(draw);
    // Redrawn when something changed, and a few times a second (it breathes).
    if (!dirty && now - shownAt < 250) return;
    dirty = false;
    shownAt = now;
    const box = view.getBoundingClientRect();
    const scale = Math.min(window.devicePixelRatio || 1, 1.5);
    const w = Math.max(1, Math.min(1024, Math.round(box.width * scale)));
    const h = Math.max(1, Math.min(1024, Math.round(box.height * scale)));
    if (view.width !== w || view.height !== h) {
      view.width = w;
      view.height = h;
    }
    // Choosing the hair: without the hat or the hood.
    const image = game.wizardPreview(look, step === 1, camera, now / 1000, w, h);
    scratch.width = w;
    scratch.height = h;
    scratch.getContext('2d').putImageData(image, 0, 0);
    context.clearRect(0, 0, w, h);
    context.drawImage(scratch, 0, 0);
  }
  const redraw = () => { dirty = true; };

  // Turning (drag), zooming (wheel, pinch, buttons), moving (two fingers, right drag, Shift).
  const pointers = new Map();
  let pinch = 0;
  view.addEventListener('pointerdown', (event) => {
    view.setPointerCapture(event.pointerId);
    pointers.set(event.pointerId, { x: event.clientX, y: event.clientY, pan: event.button === 2 || event.shiftKey });
  });
  view.addEventListener('pointermove', (event) => {
    const p = pointers.get(event.pointerId);
    if (!p) return;
    const dx = event.clientX - p.x, dy = event.clientY - p.y;
    p.x = event.clientX;
    p.y = event.clientY;
    const span = Math.max(1, view.clientHeight);
    if (pointers.size === 2) {
      const [a, b] = [...pointers.values()];
      const d = Math.hypot(a.x - b.x, a.y - b.y);
      if (pinch) camera.zoom = Math.min(4, Math.max(0.6, camera.zoom * d / pinch));
      pinch = d;
      camera.panX -= dx / span / camera.zoom * 0.6;
      camera.panY += dy / span / camera.zoom * 0.6;
    } else if (p.pan) {
      camera.panX -= dx / span / camera.zoom * 1.2;
      camera.panY += dy / span / camera.zoom * 1.2;
    } else {
      camera.yaw -= dx * 0.012;
      camera.pitch = Math.min(1.1, Math.max(-0.25, camera.pitch + dy * 0.006));
    }
    camera.panX = Math.min(0.6, Math.max(-0.6, camera.panX));
    camera.panY = Math.min(0.7, Math.max(-0.7, camera.panY));
    redraw();
  });
  const release = (event) => {
    pointers.delete(event.pointerId);
    pinch = 0;
  };
  view.addEventListener('pointerup', release);
  view.addEventListener('pointercancel', release);
  view.addEventListener('contextmenu', (event) => event.preventDefault());
  view.addEventListener('wheel', (event) => {
    event.preventDefault();
    camera.zoom = Math.min(4, Math.max(0.6, camera.zoom * Math.exp(-event.deltaY * 0.0015)));
    redraw();
  }, { passive: false });
  const zoomBy = (k) => () => {
    camera.zoom = Math.min(4, Math.max(0.6, camera.zoom * k));
    redraw();
  };
  $('create-zoom-in').addEventListener('click', zoomBy(1.3));
  $('create-zoom-out').addEventListener('click', zoomBy(1 / 1.3));
  $('create-face').addEventListener('click', () => {  // the face, up close
    Object.assign(camera, { yaw: 0.2, pitch: 0.05, zoom: 2.6, panX: 0, panY: 0.42 });
    redraw();
  });
  $('create-reset').addEventListener('click', () => {
    Object.assign(camera, { yaw: 0.35, pitch: 0.12, zoom: 0.85, panX: 0, panY: 0 });
    redraw();
  });

  // ---- The choices ----
  // A row of round color swatches, or of buttons with a label.
  function swatches(target, colors, key, label) {
    target.replaceChildren(...colors.map((color, i) => {
      const b = el('button', { type: 'button', className: 'swatch', title: `${label} ${i + 1}` });
      b.style.setProperty('--swatch', color);
      b.setAttribute('aria-label', `${label} ${i + 1}`);
      b.setAttribute('aria-pressed', String(look[key] === i));
      b.addEventListener('click', () => {
        look[key] = i;
        render();
      });
      return b;
    }));
  }
  function choices(target, labels, key, values = labels.map((_, i) => i)) {
    target.replaceChildren(...labels.map((label, i) => {
      const b = el('button', { type: 'button', className: 'button choice', textContent: label });
      b.setAttribute('aria-pressed', String(look[key] === values[i]));
      b.addEventListener('click', () => {
        look[key] = values[i];
        render();
      });
      return b;
    }));
  }

  // The three monsters to start with, of the magician's sex, as the most common of their kind.
  function starters() {
    const genes = look.woman ? 1 : 0;  // the sex: the first gene (genes_code)
    $('create-starters').replaceChildren(...STARTERS.map((id) => {
      const info = LORE.species[id];
      const index = LORE.speciesOrder.indexOf(id);
      const portrait = document.createElement('canvas');
      portrait.width = portrait.height = 128;
      portrait.getContext('2d').putImageData(game.portrait(index, 128, genes), 0, 0);
      const element = (LORE.elements[LORE.starterElements[id]] || { name: '', color: '#ccc' });
      const card = el('button', { type: 'button', className: 'starter' }, [
        portrait,
        el('strong', { textContent: info.name }),
        el('span', { className: 'badge', textContent: element.name, style: `background:${element.color}` }),
        el('small', { textContent: info.kind }),
      ]);
      card.dataset.species = id;
      card.setAttribute('aria-pressed', String(starter === id));
      card.addEventListener('click', () => {
        starter = id;
        render();
      });
      return card;
    }));
    const chosen = LORE.species[starter];
    $('create-starter-text').textContent =
      `${chosen.name}, ${look.woman ? 'une femelle' : 'un mâle'} : ${chosen.description}`;
  }

  function render() {
    steps.forEach((s, i) => { s.hidden = i !== step; });
    tabs.forEach((t, i) => t.setAttribute('aria-selected', String(i === step)));
    choices($('create-sex'), ['Homme', 'Femme'], 'woman', [false, true]);
    swatches($('create-skin'), SKINS, 'skin', 'Peau');
    choices($('create-hair'), HAIR_STYLES, 'hair');
    swatches($('create-hair-color'), HAIRS, 'hairColor', 'Cheveux');
    swatches($('create-eyes'), EYES, 'eyes', 'Yeux');
    choices($('create-staff'), STAFF, 'staff');
    swatches($('create-robe'), ROBES, 'robe', 'Robe');
    choices($('create-headwear'), HEADWEAR, 'headwear');
    if (step === 3) starters();
    back.hidden = step === 0;
    next.textContent = step === steps.length - 1 ? 'Commencer l’aventure' : 'Suivant';
    next.classList.toggle('primary', step === steps.length - 1);
    redraw();
  }

  function go(to) {
    if (to > step && step === 0 && !$('create-name').value.trim()) {
      note.textContent = 'Choisis un nom pour ton magicien.';
      $('create-name').focus();
      return;
    }
    note.textContent = '';
    step = Math.max(0, Math.min(steps.length - 1, to));
    render();
    if (step === 0) $('create-name').focus();
  }
  tabs.forEach((t, i) => t.addEventListener('click', () => go(i)));
  back.addEventListener('click', () => go(step - 1));
  next.addEventListener('click', async () => {
    if (step < steps.length - 1) {
      go(step + 1);
      return;
    }
    const name = $('create-name').value.trim();
    if (!name) {
      go(0);
      return;
    }
    const seed = $('create-seed').value.trim() || randomSeed();
    next.disabled = true;
    const ok = await game.newGame(slot, name, seed, look, LORE.speciesOrder.indexOf(starter));
    next.disabled = false;
    if (!ok) {
      note.textContent = 'La partie n’a pas pu être créée.';
      return;
    }
    close(true);
  });
  $('create-name').addEventListener('keydown', (event) => {
    if (event.key === 'Enter') next.click();
  });
  $('create-dice').addEventListener('click', () => {
    $('create-seed').value = randomSeed();
  });
  $('create-cancel').addEventListener('click', () => close(false));

  function open(toSlot = 0, options = {}) {
    if (!game) return;
    slot = toSlot;
    firstVisit = !!options.first;
    look = { woman: false, skin: 0, hair: 0, hairColor: 0, eyes: 0, staff: 0, robe: 0, headwear: 0 };
    starter = 'companion';
    step = 0;
    Object.assign(camera, { yaw: 0.35, pitch: 0.12, zoom: 0.85, panX: 0, panY: 0 });
    $('create-name').value = '';
    $('create-seed').value = randomSeed();
    $('create-cancel').hidden = firstVisit;
    note.textContent = '';
    root.hidden = false;
    document.documentElement.classList.add('creating');
    game.setPaused(true);
    game.setCovered(true);  // the world behind is not drawn meanwhile
    render();
    requestAnimationFrame(draw);
    $('create-name').focus();
  }

  function close(created) {
    if (!isOpen()) return;
    root.hidden = true;
    document.documentElement.classList.remove('creating');
    if (document.activeElement && root.contains(document.activeElement)) document.activeElement.blur();
    if (game) {
      game.setCovered(false);
      game.setPaused(false);
    }
    if (created) window.dispatchEvent(new Event('monstres-created'));
  }

  // Keys typed here are not the game's (no walking while typing a name).
  window.addEventListener('keydown', (event) => {
    if (!isOpen()) return;
    event.stopPropagation();
    if (event.key === 'Escape' && !firstVisit) close(false);
  }, true);
  window.addEventListener('keyup', (event) => { if (isOpen()) event.stopPropagation(); }, true);

  // ---- The arrival's flight: everything else hidden, a button to skip it ----
  const skip = $('skip-intro');
  let introWatch = 0;
  function followIntro() {
    clearInterval(introWatch);
    const update = () => {
      const on = !!game && game.introOn();
      document.documentElement.classList.toggle('intro', on);
      skip.hidden = !on;
      if (!on) clearInterval(introWatch);
    };
    update();
    introWatch = setInterval(update, 150);
  }
  skip.addEventListener('click', () => {
    if (game) game.skipIntro();
    followIntro();
  });
  window.addEventListener('monstres-created', followIntro);

  whenReady((g) => {
    game = g;
    if (typeof game.needsCreation === 'function' && game.needsCreation()) open(0, { first: true });
  });
  window.monstresCreate = { open, close, isOpen };
})();
