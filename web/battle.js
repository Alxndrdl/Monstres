// Battles, on the page: the context menu on the right (fight, flee, capture,
// forfeit), the hit points of both sides, the foes' icons at the top (tap one
// to aim at it; tap the one aimed at to move freely), the rules before a
// trainer's battle, the battle buttons around the dodge (the jump button),
// damage numbers and the outcome. The rules are in the game
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
  const npcName = (n) => (LORE.npcs[n] || { name: 'Dresseur' }).name;

  let game = null;
  let selected = -1;  // creature touched (ui.js)
  let object = -1;    // teleporter touched (ui.js)
  window.addEventListener('monstres-selection', (event) => {
    selected = event.detail.index;
    if (selected >= 0) object = -1;
  });
  window.addEventListener('monstres-object', (event) => { object = event.detail.index; });

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
    } else if (event.code === 'KeyT') {  // the next foe, then no one (moving freely)
      event.preventDefault();
      const b = game.battle();
      const n = (b.foes || []).length;
      if (b.phase === 'fight' && n) game.target(b.target < 0 ? 0 : b.target + 1 < n ? b.target + 1 : -1);
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
    train: '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" ' +
      'stroke-linejoin="round"><circle cx="12" cy="12" r="8"/><circle cx="12" cy="12" r="4"/><path d="M12 2v3M12 19v3M2 12h3M19 12h3"/></svg>',
    stop: '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" ' +
      'stroke-linejoin="round"><rect x="6" y="6" width="12" height="12" rx="2"/></svg>',
    forfeit: '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" ' +
      'stroke-linejoin="round"><path d="M5 21V4M5 4h11l-2 4 2 4H5"/></svg>',
    teleport: '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" ' +
      'stroke-linejoin="round"><ellipse cx="12" cy="18" rx="8" ry="3"/><path d="M12 15V3M8 7l4-4 4 4"/></svg>',
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
    knockedOut: 'K.O. : il se repose. Laisse-le tranquille un moment.',
    owned: 'Il appartient à quelqu’un.',
    companionDown: 'Ton monstre est K.O. : soigne-le (sort de soin) ou va voir Odette au village.',
  };
  // Under the capture button: why it cannot be used yet.
  const CAPTURE_NOTES = {
    notWeak: (b) => `PV < ${b.captureHpPercent} %`,
    teamFull: () => 'Équipe complète',
    noTarget: () => 'Vise un monstre',
    ok: () => 'Il est affaibli !',
  };

  function renderContext(b) {
    let key;
    let build;
    if (b.phase === 'prepare') {
      key = 'prepare';  // the rules panel says it all
    } else if (b.phase === 'fight' && b.rival) {
      const out = !!b.ally;
      key = `rival/${out}`;
      build = () => {
        contextTitle.replaceChildren(`Combat contre ${npcName(b.rival.npc)}`);
        contextActions.replaceChildren(action('forfeit', 'Déclarer forfait', true, () => game.forfeit()));
        contextNote.textContent = out ? ''
          : 'Ton monstre est K.O. : choisis-en un autre dans ton équipe (à gauche).';
      };
    } else if (b.phase === 'fight') {
      const target = b.target >= 0 ? b.enemy : null;
      const name = target ? speciesName(target.species) : 'mode libre';
      key = `fight/${target ? target.index : -1}/${b.capture}/${b.foes.length}/${!!b.ally}`;
      build = () => {
        contextTitle.replaceChildren(`Combat · ${name} `, el('small', { textContent: target ? `niv. ${target.level}` : '' }));
        const capture = action('capture', 'Capturer', b.capture === 'ok', () => game.capture());
        capture.append(el('small', { textContent: (CAPTURE_NOTES[b.capture] || (() => ''))(b) }));
        capture.title = b.capture === 'notWeak'
          ? `Affaiblis-le d’abord : moins de ${b.captureHpPercent} % de ses PV (le trait doré sur sa barre).` : '';
        contextActions.replaceChildren(action('flee', 'Fuir', true, () => game.flee()), capture);
        contextNote.textContent = !b.ally ? 'Ton monstre est K.O. : choisis-en un autre dans ton équipe (à gauche).'
          : target ? '' : 'Touche un adversaire (ou son icône) pour le viser.';
      };
    } else if (b.phase === 'training') {
      const name = speciesName(b.ally.species);
      key = 'training';
      build = () => {
        contextTitle.replaceChildren(`Entraînement · ${name}`);
        contextActions.replaceChildren(action('stop', 'Arrêter l’entraînement', true, () => game.stopTraining()));
        contextNote.textContent = 'Ton magicien se protège avec un bouclier : attaque-le pour essayer tes attaques.';
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
      const t = !c && object >= 0 ? game.object(object) : null;
      if (t && t.kind === 'teleporter') {
        const reachable = game.teleporters().list.filter((x) => x.reachable).length;
        key = `teleporter/${t.id}/${t.known}/${t.here}/${reachable}`;
        build = () => {
          contextTitle.replaceChildren('Téléporteur ', el('small', { textContent: LORE.teleporters[t.place] }));
          contextActions.replaceChildren(action('teleport', 'Se téléporter', t.here && reachable > 0,
            () => window.monstresMenu.open('map', { teleport: true })));
          contextNote.textContent = !t.known ? 'Un étrange cercle de pierres à runes. Approche-toi…'
            : !t.here ? 'Mets-toi au milieu du cercle de pierres.'
              : reachable ? 'Choisis où aller sur la carte.' : 'Il ne mène nulle part… Trouve un autre cercle de pierres.';
        };
      } else if (!c) {
        key = 'none';
      } else if (c.index === 0) {  // the companion
        const down = c.mind === 'knockedOut';
        key = `companion/${c.train}/${down}`;
        build = () => {
          contextTitle.replaceChildren(`${speciesName(c.species)} `, el('small', { textContent: `niv. ${c.level}` }));
          contextActions.replaceChildren(action('train', 'S’entraîner', c.train, () => game.train()));
          contextNote.textContent = down ? 'Il est K.O. : soigne-le (sort de soin) ou va voir Odette au village.'
            : 'Contrôle-le comme en combat, contre ton magicien.';
        };
      } else if (c.tag === 'rival') {
        key = `rival-monster/${c.index}`;
        build = () => {
          contextTitle.replaceChildren(`${speciesName(c.species)} `, el('small', { textContent: `niv. ${c.level}` }));
          contextActions.replaceChildren(action('fight', 'Combattre', false, () => {}));
          contextNote.textContent = 'C’est le monstre de Bastien : parle-lui pour le défier.';
        };
      } else if (c.species === 'villager') {
        key = `villager/${c.index}/${c.talk}`;
        build = () => {
          contextTitle.textContent = (LORE.npcs[c.npc] || { name: 'Villageois' }).name;
          contextActions.replaceChildren(action('talk', 'Parler', !!c.talk, () => window.monstresDialogue.open(game.selected())));
          contextNote.textContent = c.talk ? '' : 'Approche-toi pour lui parler.';
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
    context.dataset.phase = b.phase;
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
  let trainer = null;  // the magician's name and level, read when a training starts
  function renderHud(b) {
    hud.hidden = b.phase === 'none' || b.phase === 'prepare';
    renderFoeIcons(b);
    if (hud.hidden) {
      trainer = null;
      return;
    }
    if (b.phase === 'training') {
      // As against a monster: the one trained against at the top (the magician,
      // behind its shield: no hit points), the companion's hit points at the bottom.
      if (!trainer) {
        const w = typeof game.wizard === 'function' ? game.wizard() : {};
        trainer = { name: (w.name || '').trim() || 'Ton magicien', level: w.level || 1 };
      }
      sides.enemy.classList.add('free');
      sides.enemy.classList.remove('windup');
      sides.enemy.querySelector('strong').textContent = trainer.name;
      sides.enemy.querySelector('.level').textContent = `niv. ${trainer.level}`;
      sides.enemy.querySelector('.hp-text').textContent = 'Entraînement : son bouclier encaisse tes attaques.';
      sides.enemy.querySelector('.tags').replaceChildren();
      tagsFor = '';
      captureMark.hidden = true;
      sides.ally.hidden = !b.ally;
      if (b.ally) showSide(sides.ally, speciesName(b.ally.species), b.ally.level, b.ally.hp, b.ally.maxHp);
      return;
    }
    trainer = null;
    const aimed = b.target >= 0 && b.enemy;
    sides.enemy.classList.toggle('free', !aimed);
    if (aimed) {
      showSide(sides.enemy, speciesName(b.enemy.species), b.enemy.level, b.enemy.hp, b.enemy.maxHp);
    } else {
      sides.enemy.querySelector('strong').textContent = b.rival && !(b.foes || []).length
        ? `${npcName(b.rival.npc)} envoie son prochain monstre…` : 'Mode libre';
      sides.enemy.querySelector('.level').textContent = '';
      sides.enemy.querySelector('.hp-text').textContent = (b.foes || []).length
        ? 'Touche un adversaire, ou son icône, pour le viser.' : '';
      sides.enemy.querySelector('.hp-bar').firstElementChild.style.width = '0';
    }
    sides.ally.hidden = !b.ally;
    if (b.ally) showSide(sides.ally, speciesName(b.ally.species), b.ally.level, b.ally.hp, b.ally.maxHp);
    captureMark.style.left = `${b.captureHpPercent}%`;
    captureMark.hidden = !!b.rival || !aimed;
    sides.enemy.classList.toggle('windup', (b.foes || []).some((f) => f.fighter.state === 'windup'));
    const tags = aimed ? `${b.enemy.index}` : '';
    if (tagsFor !== tags) {
      tagsFor = tags;
      sides.enemy.querySelector('.tags').replaceChildren(...(aimed ? b.enemy.elements : []).map((id) => {
        const e = LORE.elements[id] || { name: id, color: '#ccc' };
        return el('span', { className: 'badge', textContent: e.name, style: `background:${e.color}` });
      }));
    }
  }

  // ---- The foes' icons: tap one to aim at it, tap the one aimed at to move freely ----
  const icons = $('foe-icons');
  let iconsFor = '';
  function toggleTarget(b, slot) {
    game.target(b.target === slot ? -1 : slot);
  }
  function renderFoeIcons(b) {
    const show = b.phase === 'fight' || b.phase === 'prepare' || b.phase === 'training';
    icons.hidden = !show;
    if (!show) {
      iconsFor = '';
      return;
    }
    const foes = b.foes || [];
    // A trainer's team: the ones not seen yet are a "?", the beaten ones crossed out.
    const list = b.phase === 'training'
      ? [{ key: 'wizard', species: 'wizard', look: 0, level: 0, ko: false, slot: 0, foe: null, wizard: true }]
      : b.rival
      ? b.rival.team.map((m) => {
        const foe = m.out ? foes[0] : null;
        return { key: `${m.slot}/${m.seen}/${m.ko}/${m.out}`, species: m.seen ? m.species : null, look: m.look,
          level: m.level, ko: m.ko, slot: foe ? foe.slot : -1, foe };
      })
      : foes.map((f) => ({ key: `${f.index}`, species: f.species, look: f.look, level: f.level, ko: false, slot: f.slot, foe: f }));
    const key = `${b.phase}/${b.target}/` + list.map((x) => x.key).join();
    if (key !== iconsFor) {
      iconsFor = key;
      icons.classList.toggle('rival', !!b.rival);
      icons.replaceChildren(...(b.rival ? [el('span', { className: 'foe-icons-title', textContent: npcName(b.rival.npc) })] : []),
        ...list.map((x) => {
          const button = el('button', { type: 'button', className: 'foe-icon' });
          button.dataset.slot = x.slot;
          if (x.species) button.append(el('img', { src: portrait(x.species, x.look), alt: '' }));
          else button.append(el('span', { className: 'foe-unknown', textContent: '?' }));
          if (!x.wizard) button.append(el('span', { className: 'foe-hp' }, [el('span')]));
          if (x.ko) button.append(el('span', { className: 'foe-ko', textContent: '✕' }));
          button.classList.toggle('ko', x.ko);
          const live = b.phase === 'fight' || b.phase === 'training';
          button.classList.toggle('targeted', x.slot >= 0 && b.target === x.slot && live);
          const name = x.wizard ? 'Ton magicien' : x.species ? `${speciesName(x.species)} · niv. ${x.level}` : 'Monstre inconnu';
          const clickable = x.slot >= 0 && live;
          button.disabled = !clickable;
          button.title = !clickable ? name : b.target === x.slot ? `${name} — viser personne (mode libre)` : `${name} — le viser`;
          button.setAttribute('aria-label', button.title);
          button.addEventListener('pointerdown', (event) => event.preventDefault());
          button.addEventListener('click', () => {
            if (game && !button.disabled) toggleTarget(game.battle(), x.slot);
          });
          return button;
        }));
    }
    // Their hit points.
    list.forEach((x, i) => {
      const button = icons.querySelectorAll('.foe-icon')[i];
      if (!button) return;
      const ratio = x.foe ? x.foe.hp / x.foe.maxHp : x.ko ? 0 : 1;
      const fill = button.querySelector('.foe-hp > span');
      if (!fill) return;
      fill.style.width = `${100 * ratio}%`;
      fill.style.background = ratio > 0.5 ? '#8fd16a' : ratio > 0.25 ? '#f2c94c' : '#ff7a6b';
      button.classList.toggle('windup', !!x.foe && x.foe.fighter.state === 'windup');
    });
  }
  // A tap on a foe in the view (ui.js): aim at it, or at no one if it was aimed at.
  window.addEventListener('monstres-foe-tap', (event) => {
    if (!game) return;
    const b = game.battle();
    if (b.phase !== 'fight') return;
    const foe = (b.foes || []).find((f) => f.index === event.detail.index);
    if (foe) toggleTarget(b, foe.slot);
  });
  // Training: a tap on the magician aims at it, or at no one.
  window.addEventListener('monstres-wizard-tap', () => {
    if (!game) return;
    const b = game.battle();
    if (b.phase === 'training') toggleTarget(b, 0);
  });

  // ---- Too far from the foes (or the magician): come back ----
  const away = $('away-warning');
  function renderAway(b) {
    const show = (b.phase === 'fight' || b.phase === 'training') && b.away !== undefined;
    away.hidden = !show;
    if (!show) return;
    const left = Math.ceil(b.away);
    away.textContent = b.phase === 'training'
      ? `Trop loin ! Reviens vers ton magicien : fin de l’entraînement dans ${left} s`
      : `Trop loin ! Reviens à portée : fin du combat dans ${left} s`;
  }

  // ---- Before a trainer's battle: the rules, and "ready" ----
  const rules = $('rules');
  let rulesFor = '';
  function renderRules(b) {
    const show = b.phase === 'prepare';
    rules.hidden = !show;
    if (!show) {
      rulesFor = '';
      return;
    }
    const key = `${b.rival.npc}/${b.rival.team.length}/${b.rival.wins}`;
    if (key === rulesFor) return;
    rulesFor = key;
    const name = npcName(b.rival.npc);
    const n = b.rival.team.length;
    const lines = [
      `${name} a ${n} monstres, toi ${b.rival.mine.length}. Un seul monstre de chaque côté se bat à la fois.`,
      'Un monstre K.O. est éliminé : son dresseur envoie le suivant (tu choisis le tien dans ton équipe, à gauche).',
      'Le dresseur qui n’a plus de monstre en état de se battre perd.',
      'Tu vois combien de monstres a ton adversaire, mais pas lesquels avant qu’ils entrent en jeu.',
      'Touche le monstre adverse, ou son icône, pour le viser ou te déplacer librement.',
      'Pas de capture : ce sont ses monstres. Tu peux déclarer forfait à tout moment, il gagne alors.',
      b.rival.wins ? 'Tu l’as déjà battu : cette fois, pas d’expérience.' : 'Le premier combat gagné rapporte de l’expérience.',
    ];
    rules.querySelector('.rules-title').textContent = `Combat contre ${name}`;
    rules.querySelector('.rules-list').replaceChildren(...lines.map((t) => el('li', { textContent: t })));
  }
  $('rules-ready').addEventListener('click', () => { if (game) game.readyToFight(); });
  $('rules-forfeit').addEventListener('click', () => { if (game) game.forfeit(); });

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
    if (!fighting || !b.ally) return;
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
  const seen = { allyHit: 0, allyParried: 0, shield: 0, outcome: -1, mercy: -1 };
  const seenFoes = new Map();  // creature index -> { hit, parried }

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
    if (b.phase === 'training') {
      // What the attack would have done, on the magician's shield.
      if (b.shield.parriedTick > seen.shield) floatText(-1, `🛡 ${b.shield.lastDamage}`, 'shield');
      seen.shield = b.shield.parriedTick;
      const a = b.ally.fighter;
      if (a.parriedTick > seen.allyParried) floatText(0, 'Paré !', 'blocked');
      seen.allyParried = a.parriedTick;
      return;
    }
    if (b.phase !== 'fight') return;
    // The Mercy spell held a blow back: the foe left at 1 hit point.
    const mercy = game.spells().mercyTick;
    const held = mercy > seen.mercy && seen.mercy >= 0;
    seen.mercy = mercy;
    for (const f of b.foes || []) {
      const e = f.fighter;
      const was = seenFoes.get(f.index) || { hit: e.hitTick, parried: e.parriedTick };
      if (held && f.hp === 1 && e.hitTick === mercy) floatText(f.index, 'Retenu ! 1 PV', 'mercy');
      else if (e.hitTick > was.hit) floatText(f.index, `${e.lastDamage}`, 'enemy');
      if (e.parriedTick > was.parried) floatText(f.index, 'Paré !', 'blocked');
      seenFoes.set(f.index, { hit: e.hitTick, parried: e.parriedTick });
    }
    if (!b.ally) return;
    const a = b.ally.fighter;
    if (a.hitTick > seen.allyHit) floatText(0, `${a.lastDamage}`, 'ally');
    if (a.parriedTick > seen.allyParried) floatText(0, 'Paré !', 'blocked');
    seen.allyHit = a.hitTick;
    seen.allyParried = a.parriedTick;
  }

  const banner = $('battle-banner');
  let bannerTimer = 0;
  const portraits = new Map();
  function portrait(species, look) {
    const key = `${species}/${look}`;
    if (!portraits.has(key)) {
      const canvas = el('canvas', { width: 96, height: 96 });
      canvas.getContext('2d').putImageData(game.portrait(LORE.speciesOrder.indexOf(species), 96, look), 0, 0);
      portraits.set(key, canvas.toDataURL());
    }
    return portraits.get(key);
  }

  // One monster's experience: its bar fills up, level after level.
  function xpCard(g) {
    const fill = el('span');
    const level = el('strong', { className: 'xp-level' });
    const card = el('div', { className: 'xp-card' }, [
      el('img', { src: portrait(g.species, g.look), alt: '' }),
      el('div', { className: 'xp-body' }, [
        el('div', { className: 'xp-head' }, [el('span', { textContent: speciesName(g.species) }), level]),
        el('div', { className: 'bar xp-bar' }, [fill]),
        el('small', { className: 'xp-gain', textContent: `+${g.xp} XP` }),
      ]),
    ]);
    const steps = g.steps || [];
    let k = 0;
    const show = (step, ratio) => {
      level.textContent = `Niv. ${step[0]}`;
      fill.style.width = `${100 * ratio}%`;
    };
    if (steps.length) {
      fill.style.transition = 'none';
      show(steps[0], steps[0][1] / steps[0][3]);
    }
    const next = () => {
      if (k >= steps.length) return;
      const [lvl, , to, need] = steps[k];
      fill.style.transition = 'width 0.6s ease-out';
      show([lvl], to / need);
      if (to >= need && k + 1 < steps.length) {  // full: one level up
        setTimeout(() => {
          ++k;
          card.classList.remove('level-up');
          void card.offsetWidth;  // restart the animation
          card.classList.add('level-up');
          fill.style.transition = 'none';
          show(steps[k], steps[k][1] / steps[k][3]);
          requestAnimationFrame(() => requestAnimationFrame(next));
        }, 650);
      }
    };
    setTimeout(() => requestAnimationFrame(next), 350);
    return card;
  }

  function renderOutcome(b) {
    const o = b.outcome;
    if (seen.outcome < 0) seen.outcome = o.tick;  // what happened before the page was opened
    if (o.result === 'none' || o.tick <= seen.outcome) return;
    seen.outcome = o.tick;
    const foe = speciesName(o.species);
    const companion = speciesName((game.team().find((m) => m.active) || {}).species || 'companion');
    const trainer = o.rival ? npcName(o.rival) : '';
    const texts = o.rival ? {
      victory: ['Victoire !', `Tu as battu ${trainer} !`],
      defeat: [`${trainer} a gagné`, 'Tes monstres sont épuisés.'],
      forfeit: ['Tu as déclaré forfait', `${trainer} remporte le combat.`],
    }[o.result] : {
      victory: ['Victoire !', o.count > 1 ? `${o.count} monstres sont K.O.` : `${foe} est K.O.`],
      captured: [`${foe} capturé !`, 'Il rejoint ton équipe'],
      defeat: [`${companion} est K.O.`, 'Soigne-le d’un sort, ou va voir Odette au village.'],
      fled: ['Tu as pris la fuite', ''],
    }[o.result];
    if (!texts) return;
    const won = o.result === 'victory' || o.result === 'captured';
    const gains = (o.gains || []).filter((g) => g.species);  // also those beaten before a defeat
    const NO_XP = {
      beaten: o.count > 1 ? 'Déjà battus : pas d’expérience cette fois.' : 'Déjà battu : pas d’expérience cette fois.',
      released: 'Il avait été relâché : pas d’expérience cette fois.',
      trainer: `${trainer} ne donne de l’expérience qu’au premier combat gagné.`,
    };
    let note = '';
    if (won && !o.xp) note = NO_XP[o.noXp] || '';
    else if (o.wizardXp) note = `Ton magicien gagne ${o.wizardXp} XP`;
    banner.replaceChildren(el('span', { className: 'banner-title', textContent: texts[0] }),
      ...(texts[1] ? [el('small', { textContent: texts[1] })] : []),
      ...(gains.length ? [el('div', { className: 'xp-cards' }, gains.map(xpCard))] : []),
      ...(note ? [el('small', { className: 'banner-note', textContent: note })] : []));
    banner.dataset.result = o.result;
    banner.classList.remove('show');
    void banner.offsetWidth;
    banner.classList.add('show');
    banner.hidden = false;
    clearTimeout(bannerTimer);
    bannerTimer = setTimeout(() => { banner.hidden = true; }, gains.length ? 4200 : 2800);
  }

  // ---- Every frame ----
  let fighting = false;
  function frame() {
    requestAnimationFrame(frame);
    if (!game || typeof game.battle !== 'function') return;
    const b = game.battle();
    fighting = b.phase === 'fight' || b.phase === 'training';
    root.classList.toggle('battle', fighting);
    root.classList.toggle('training', b.phase === 'training');
    renderButtons(b);
    renderHud(b);
    renderHits(b);
    renderOutcome(b);
    renderRules(b);
    renderAway(b);
    renderContext(b);
  }

  const start = () => {
    game = window.monstres;
    requestAnimationFrame(frame);
  };
  if (window.monstres && window.monstres.isReady) start();
  else window.addEventListener('monstres-ready', start, { once: true });
})();
