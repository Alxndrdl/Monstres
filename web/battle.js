// Battles, on the page: the context menu on the right (fight, flee, capture),
// the hit points of both sides, the battle buttons around the dodge (the jump
// button), damage numbers and the outcome. The rules are in the game
// (src/core/battle.cpp); this only shows them and sends the presses.
(() => {
  'use strict';

  const LORE = window.MonstresLore;
  const $ = (id) => document.getElementById(id);
  const el = (tag, props = {}, children = []) => {
    const node = Object.assign(document.createElement(tag), props);
    for (const child of children) node.append(child);
    return node;
  };
  const speciesName = (id) => (LORE.species[id] || { name: id }).name;

  let game = null;
  let selected = -1;  // creature touched (ui.js)
  window.addEventListener('monstres-selection', (event) => { selected = event.detail.index; });

  // ---- Buttons ----
  const root = document.documentElement;
  const jump = $('jump');
  const jumpHint = $('jump-hint');
  const attackButtons = [1, 2, 3].map((slot) => $(`attack-${slot}`));
  const parryButton = $('parry');
  const battleButtons = [...attackButtons, parryButton];

  const press = (button, action) => {
    button.addEventListener('pointerdown', (event) => {
      event.preventDefault();  // no focus, no text selection
      button.classList.add('pressed');
      if (game) action();
    });
    for (const type of ['pointerup', 'pointercancel', 'pointerleave']) {
      button.addEventListener(type, () => button.classList.remove('pressed'));
    }
    button.addEventListener('click', (event) => event.preventDefault());
  };
  attackButtons.forEach((button, i) => press(button, () => game.input.attack(i + 1)));
  press(parryButton, () => game.input.parry());

  // Keyboard: 1, 2, 3 attack, F parries (Space dodges, see controls.js).
  const typing = (event) => event.target instanceof HTMLElement &&
    (event.target.isContentEditable || ['INPUT', 'TEXTAREA', 'SELECT'].includes(event.target.tagName));
  const KEYS = { Digit1: 1, Digit2: 2, Digit3: 3, Numpad1: 1, Numpad2: 2, Numpad3: 3 };
  window.addEventListener('keydown', (event) => {
    if (!game || !fighting || event.repeat || event.ctrlKey || event.metaKey || event.altKey || typing(event)) return;
    if (KEYS[event.code]) {
      event.preventDefault();
      game.input.attack(KEYS[event.code]);
      flash(attackButtons[KEYS[event.code] - 1]);
    } else if (event.code === 'KeyF') {
      event.preventDefault();
      game.input.parry();
      flash(parryButton);
    }
  });
  function flash(button) {
    button.classList.add('pressed');
    setTimeout(() => button.classList.remove('pressed'), 120);
  }

  // ---- Context menu ----
  const context = $('context');
  const contextTitle = context.querySelector('.context-title');
  const contextActions = context.querySelector('.context-actions');
  const contextNote = context.querySelector('.context-note');
  let contextKey = '';

  const ICONS = {
    fight: '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" ' +
      'stroke-linejoin="round"><path d="M14.5 17.5L3 6V3h3l11.5 11.5M13 19l6-6M16 16l4 4M19 21l2-2"/>' +
      '<path d="M9.5 6.5L13 3h3v3l-3.5 3.5M5 14l-2 2M7 17l-2 2"/></svg>',
    flee: '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" ' +
      'stroke-linejoin="round"><path d="M10 17l-5-5 5-5M5 12h14"/></svg>',
    capture: '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" ' +
      'stroke-linejoin="round"><circle cx="12" cy="12" r="8"/><path d="M4 12h5M15 12h5"/><circle cx="12" cy="12" r="3"/></svg>',
    talk: '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" ' +
      'stroke-linejoin="round"><path d="M21 12a8 8 0 0 1-11.5 7.2L4 21l1.8-5.5A8 8 0 1 1 21 12z"/></svg>',
  };

  const action = (id, label, enabled, onClick) => {
    const button = el('button', { type: 'button', className: id, id: `action-${id}`, disabled: !enabled });
    button.setAttribute('role', 'menuitem');
    button.innerHTML = ICONS[id];
    button.append(label);
    button.addEventListener('click', () => {
      if (game && !button.disabled) onClick();
    });
    return button;
  };

  const ENGAGE_NOTES = {
    tooFar: 'Trop loin : approche-toi à moins de 15 m.',
    inBattle: 'Un combat est déjà en cours.',
    noCompanion: 'Invoque d’abord un monstre de ton équipe (à gauche).',
  };
  // Under the capture button: why it cannot be used yet.
  const CAPTURE_NOTES = {
    notWeak: (b) => `PV < ${b.captureHpPercent} %`,
    teamFull: () => 'Équipe complète',
    ok: () => 'Il est affaibli !',
  };

  function renderContext(b) {
    let key;
    let build;
    if (b.phase === 'fight') {
      const name = speciesName(b.enemy.species);
      key = `fight/${b.enemy.index}/${b.capture}`;
      build = () => {
        contextTitle.replaceChildren(`Combat · ${name} `, el('small', { textContent: `niv. ${b.enemy.level}` }));
        const capture = action('capture', 'Capturer', b.capture === 'ok', () => game.capture());
        capture.append(el('small', { textContent: (CAPTURE_NOTES[b.capture] || (() => ''))(b) }));
        capture.title = b.capture === 'notWeak'
          ? `Affaiblis-le d’abord : moins de ${b.captureHpPercent} % de ses PV (le trait doré sur sa barre).` : '';
        contextActions.replaceChildren(action('flee', 'Fuir', true, () => game.flee()), capture);
        contextNote.textContent = '';
      };
    } else if (b.phase === 'flee') {
      key = 'flee';
      build = () => {
        contextTitle.textContent = 'Fuite…';
        contextActions.replaceChildren();
        contextNote.textContent = `${speciesName(b.ally.species)} revient vers toi.`;
      };
    } else {
      const c = selected >= 0 && game.selected();
      if (!c || c.index === 0) {
        key = 'none';
      } else if (c.species === 'villager') {
        key = `villager/${c.index}`;
        build = () => {
          contextTitle.textContent = 'Villageois';
          contextActions.replaceChildren(action('talk', 'Parler', false, () => {}));
          contextNote.textContent = 'Bientôt : parler aux villageois.';
        };
      } else {
        key = `monster/${c.index}/${c.engage}`;
        build = () => {
          contextTitle.replaceChildren(`${speciesName(c.species)} `, el('small', { textContent: `niv. ${c.level}` }));
          contextActions.replaceChildren(action('fight', 'Combattre', c.engage === 'ok', () => game.engage(c.index)));
          contextNote.textContent = ENGAGE_NOTES[c.engage] || '';
        };
      }
    }
    if (key === contextKey) return;
    contextKey = key;
    context.hidden = !build;
    context.classList.toggle('in-battle', b.phase !== 'none');
    if (build) build();
  }

  // ---- Hit points ----
  const hud = $('battle-hud');
  const sides = { enemy: hud.querySelector('.enemy'), ally: hud.querySelector('.ally') };
  const captureMark = el('span', { className: 'capture-mark' });
  sides.enemy.querySelector('.hp-bar').append(captureMark);

  function showSide(box, name, level, hp, maxHp) {
    box.querySelector('strong').textContent = name;
    box.querySelector('.level').textContent = `niv. ${level}`;
    const ratio = maxHp ? hp / maxHp : 0;
    const bar = box.querySelector('.hp-bar');
    bar.firstElementChild.style.width = `${100 * ratio}%`;
    bar.classList.toggle('mid', ratio <= 0.5 && ratio > 0.25);
    bar.classList.toggle('low', ratio <= 0.25);
    box.querySelector('.hp-text').textContent = `PV ${hp} / ${maxHp}`;
  }

  let tagsFor = '';
  function renderHud(b) {
    hud.hidden = b.phase === 'none';
    if (hud.hidden) return;
    showSide(sides.enemy, speciesName(b.enemy.species), b.enemy.level, b.enemy.hp, b.enemy.maxHp);
    showSide(sides.ally, speciesName(b.ally.species), b.ally.level, b.ally.hp, b.ally.maxHp);
    captureMark.style.left = `${b.captureHpPercent}%`;
    sides.enemy.classList.toggle('windup', b.enemy.fighter.state === 'windup');
    if (tagsFor !== `${b.enemy.index}`) {
      tagsFor = `${b.enemy.index}`;
      sides.enemy.querySelector('.tags').replaceChildren(...b.enemy.elements.map((id) => {
        const e = LORE.elements[id] || { name: id, color: '#ccc' };
        return el('span', { className: 'badge', textContent: e.name, style: `background:${e.color}` });
      }));
    }
  }

  // ---- Battle buttons: element colors, cooldowns ----
  let buttonsFor = '';
  let modeShown = null;
  function renderButtons(b) {
    if (modeShown !== fighting) {  // the jump becomes the dodge
      modeShown = fighting;
      for (const button of battleButtons) button.hidden = !fighting;
      jump.setAttribute('aria-label', fighting ? 'Esquiver' : 'Sauter');
      jumpHint.replaceChildren(el('kbd', { textContent: 'Espace' }), fighting ? ' esquiver · ' : ' pour sauter');
      if (fighting) jumpHint.append(el('kbd', { textContent: 'F' }), ' parer');
    }
    if (!fighting) return;
    const moves = b.ally.moves;
    const key = moves.map((m) => m.id + m.element).join();
    if (key !== buttonsFor) {
      buttonsFor = key;
      moves.forEach((m, i) => {
        const text = LORE.moves[m.id] || { name: m.id };
        const name = (text.byElement && text.byElement[m.element]) || text.name;
        attackButtons[i].setAttribute('aria-label', `${i === 0 ? 'Attaque rapide' : 'Attaque spéciale'} : ${name}`);
        attackButtons[i].title = name;
        attackButtons[i].style.setProperty('--element', (LORE.elements[m.element] || {}).color || '#fff');
      });
    }
    const f = b.ally.fighter;
    attackButtons.forEach((button, i) => {
      const left = f.cooldowns[i];
      button.style.setProperty('--cd', f.cooldownTotals[i] ? left / f.cooldownTotals[i] : 0);
      button.classList.toggle('waiting', left > 0);
    });
    parryButton.style.setProperty('--cd', Math.min(1, f.parryCooldown));  // about 1 s
  }

  // ---- Damage numbers and the outcome ----
  const layer = $('damage-layer');
  const seen = { enemyHit: 0, allyHit: 0, enemyParried: 0, allyParried: 0, outcome: -1 };

  function floatText(index, text, className) {
    const at = game.creatureOnScreen(index);
    if (!at) return;
    const n = el('div', { className: `damage ${className}`, textContent: text });
    n.style.left = `${at.x + (Math.random() - 0.5) * 24}px`;
    n.style.top = `${at.y - 30}px`;
    layer.append(n);
    setTimeout(() => n.remove(), 950);
  }

  function renderHits(b) {
    if (b.phase !== 'fight') return;
    const e = b.enemy.fighter;
    const a = b.ally.fighter;
    if (e.hitTick > seen.enemyHit) floatText(b.enemy.index, `${e.lastDamage}`, 'enemy');
    if (a.hitTick > seen.allyHit) floatText(0, `${a.lastDamage}`, 'ally');
    if (a.parriedTick > seen.allyParried) floatText(0, 'Paré !', 'blocked');
    if (e.parriedTick > seen.enemyParried) floatText(b.enemy.index, 'Paré !', 'blocked');
    seen.enemyHit = e.hitTick;
    seen.allyHit = a.hitTick;
    seen.allyParried = a.parriedTick;
    seen.enemyParried = e.parriedTick;
  }

  const banner = $('battle-banner');
  let bannerTimer = 0;
  function renderOutcome(b) {
    const o = b.outcome;
    if (seen.outcome < 0) seen.outcome = o.tick;  // what happened before the page was opened
    if (o.result === 'none' || o.tick <= seen.outcome) return;
    seen.outcome = o.tick;
    const foe = speciesName(o.species);
    const companion = speciesName((game.team().find((m) => m.active) || {}).species || 'companion');
    const xp = o.xp ? ` · +${o.xp} XP` : '';
    const texts = {
      victory: ['Victoire !', `${foe} s’enfuit${xp}`],
      captured: [`${foe} capturé !`, `Il rejoint ton équipe${xp}`],
      defeat: [`${companion} est K.O.`, 'Il se repose près de toi.'],
      fled: ['Tu as pris la fuite', ''],
    }[o.result];
    if (!texts) return;
    banner.replaceChildren(texts[0], ...(texts[1] ? [el('small', { textContent: texts[1] })] : []));
    banner.dataset.result = o.result;
    banner.hidden = false;
    clearTimeout(bannerTimer);
    bannerTimer = setTimeout(() => { banner.hidden = true; }, 2800);
  }

  // ---- Every frame ----
  let fighting = false;
  function frame() {
    requestAnimationFrame(frame);
    if (!game || typeof game.battle !== 'function') return;
    const b = game.battle();
    fighting = b.phase === 'fight';
    root.classList.toggle('battle', fighting);
    renderButtons(b);
    renderHud(b);
    renderHits(b);
    renderOutcome(b);
    renderContext(b);
  }

  const start = () => {
    game = window.monstres;
    requestAnimationFrame(frame);
  };
  if (window.monstres && window.monstres.isReady) start();
  else window.addEventListener('monstres-ready', start, { once: true });
})();
