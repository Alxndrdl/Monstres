// Battles, on the page: the context menu on the right (fight, flee, capture,
// forfeit), the hit points of both sides, the foes' icons at the top (tap one
// to aim at it; tap the one aimed at to move freely), the rules before a
// trainer's battle, the battle buttons around the dodge (the jump button),
// damage numbers and the outcome. The rules are in the game
// (src/core/battle.cpp); this only shows them and sends the presses.
(() => {
  'use strict';

  const LORE = window.MonstresLore;
  const { $, el, whenReady, setText, onTap } = window.MonstresUtil;
  const speciesName = (id) => (LORE.species[id] || { name: id }).name;
  const npcName = (n) => (LORE.npcs[n] || { name: 'Dresseur' }).name;
  // A monster's name, then its level and whose it is: "Zapou niv. 5 (à toi)".
  const ownerText = (c) => (c.owner === 0 ? ' (à toi)' : c.owner > 0 ? ` (à ${npcName(c.owner)})` : '');
  const monsterTitle = (c) => [`${speciesName(c.species)} `, el('small', { textContent: `niv. ${c.level}${ownerText(c)}` })];

  let game = null;
  let selected = -1;  // creature touched (ui.js)
  let object = -1;    // teleporter touched (ui.js)
  window.addEventListener('monstres-selection', (event) => {
    selected = event.detail.index;
    if (selected >= 0) object = -1;
    quiet = false;
  });
  window.addEventListener('monstres-object', (event) => {
    object = event.detail.index;
    quiet = false;
  });

  // ---- Buttons ----
  const root = document.documentElement;
  const jump = $('jump');
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
    if (!game || !fighting || sealStage !== 'none' || event.repeat || event.ctrlKey || event.metaKey || event.altKey || typing(event)) return;
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
    door: '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" ' +
      'stroke-linejoin="round"><path d="M6 21V5a2 2 0 0 1 2-2h8a2 2 0 0 1 2 2v16M3 21h18"/><circle cx="14.5" cy="12.5" r="1"/></svg>',
    // The villagers' icons (as over their heads): a quest, healing, the shop, the farm.
    quest: '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="3.2" stroke-linecap="round">' +
      '<path d="M12 4v10"/><circle cx="12" cy="19.5" r="0.6" fill="currentColor"/></svg>',
    heal: '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="3.4" stroke-linecap="round">' +
      '<path d="M12 5v14M5 12h14"/></svg>',
    shop: '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" ' +
      'stroke-linejoin="round"><circle cx="12" cy="12" r="8.5"/><circle cx="12" cy="12" r="5"/><path d="M12 9.5v5"/></svg>',
    farm: '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" ' +
      'stroke-linejoin="round"><path d="M3 11l9-7 9 7M5 9.5V21h14V9.5M10 21v-6h4v6"/></svg>',
  };

  // ---- Doors (interior.hpp): in a fade to black of 0.3 s ----
  const fade = $('fade');
  let fading = false;
  function goThrough() {
    if (fading) return;
    fading = true;
    fade.classList.add('on');
    setTimeout(() => {
      if (game) game.door();
      // The new place is set up with the next frame: then the light comes back.
      requestAnimationFrame(() => requestAnimationFrame(() => {
        fade.classList.remove('on');
        setTimeout(() => { fading = false; }, 150);
      }));
    }, 150);
  }
  // What the panel at a door says: where it leads (or where one is), and what to do.
  function doorTitle(d) {
    const doors = LORE.doors;
    if (d.place === 'cave') return doors.cave;
    if (d.place === 'tower') return d.action === 'climb' ? doors.roof : doors.tower;
    if (d.place === 'hollowTree' || d.place === 'lighthouse') {
      return d.action === 'climb' ? doors.roofs[d.place] : doors[d.place];
    }
    if (d.place === 'burrow') return doors.burrow;
    return doors.house[d.building] || doors.house.cottage;
  }

  // icon: another one than the action's (a talk that gives a quest: its "!").
  const action = (id, label, enabled, onClick, icon = id) => {
    const button = el('button', { type: 'button', className: icon === id ? id : `${id} ${icon}`, id: `action-${id}`, disabled: !enabled });
    button.setAttribute('role', 'menuitem');
    button.innerHTML = ICONS[icon === 'newQuest' ? 'quest' : icon];
    button.append(label);
    onTap(button, () => { if (game) onClick(); });  // also while the other hand moves the monster
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
    notWeak: () => '',  // the golden mark on its bar says it (and the button's title)
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
        const note = (CAPTURE_NOTES[b.capture] || (() => ''))(b);
        if (note) capture.append(el('small', { textContent: note }));
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
      const t = !c && object >= 0 ? game.object(object) : null;
      if (b.door && !c) {
        const d = b.door;
        key = `door/${d.action}/${d.place}/${d.building}`;
        build = () => {
          contextTitle.replaceChildren(doorTitle(d));
          contextActions.replaceChildren(action('door', LORE.doors.actions[d.action] || 'Entrer', true, goThrough));
          contextNote.textContent = '';
        };
      } else if (t && t.kind === 'teleporter') {
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
          contextTitle.replaceChildren(...monsterTitle(c));
          contextActions.replaceChildren(action('train', 'S’entraîner', c.train, () => game.train()));
          contextNote.textContent = down ? 'Il est K.O. : soigne-le (sort de soin) ou va voir Odette au village.' : '';
        };
      } else if (c.tag === 'rival') {
        key = `rival-monster/${c.index}`;
        build = () => {
          contextTitle.replaceChildren(...monsterTitle(c));
          contextActions.replaceChildren(action('fight', 'Combattre', false, () => {}));
          contextNote.textContent = `C’est le monstre de ${npcName(c.owner || 4)} : parle-lui pour le défier.`;
        };
      } else if (c.species === 'villager') {
        const marks = c.marks || [];
        // A healer: something to heal (the team, the magician's mana)?
        const w = marks.includes('heal') ? game.wizard() : null;
        const heal = !!w && c.heal && (w.manaNow < w.mana || game.team().some((m) => m.hp < m.maxHp));
        key = `villager/${c.index}/${c.talk}/${marks.join()}/${heal}/${c.challenge}`;
        build = () => {
          contextTitle.textContent = (LORE.npcs[c.npc] || { name: 'Villageois' }).name;
          contextActions.replaceChildren(...villagerActions(c, marks, heal));
          contextNote.textContent = !c.talk ? 'Approche-toi pour lui parler.'
            : marks.includes('heal') && !heal ? 'Tes monstres sont en pleine forme, ton mana est plein.'
              : marks.includes('fight') && !c.challenge ? 'Fais d’abord sortir un monstre en forme de ton cristal.' : '';
        };
      } else {
        key = `monster/${c.index}/${c.engage}`;
        build = () => {
          contextTitle.replaceChildren(...monsterTitle(c));
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

  // A villager's actions: the icons over its head, each one its button (a
  // quest: talking, with its "!"; healing, the shop, a battle, the farm: done
  // at once); talking for the others.
  function villagerActions(c, marks, heal) {
    const near = !!c.talk;
    const talk = () => window.monstresDialogue.open(game.selected());
    const buttons = [];
    const quest = marks.includes('quest') ? 'quest' : marks.includes('newQuest') ? 'newQuest' : '';
    if (quest) buttons.push(action('talk', 'Parler', near, talk, quest));
    if (marks.includes('heal')) {
      buttons.push(action('heal', 'Soigner', near && heal, () => game.healTeam(c.npc)));
    }
    if (marks.includes('shop')) buttons.push(action('shop', 'Boutique', near, () => window.monstresDialogue.open(game.selected(), 'shop')));
    if (marks.includes('fight')) buttons.push(action('fight', 'Combattre', near && c.challenge, () => game.challenge(c.npc)));
    if (marks.includes('farm')) buttons.push(action('farm', 'Mes monstres', near, () => window.monstresDialogue.openFarm()));
    if (!buttons.length) buttons.push(action('talk', 'Parler', near, talk));
    return buttons;
  }

  // ---- Hit points ----
  const hud = $('battle-hud');
  const sides = { enemy: hud.querySelector('.enemy'), ally: hud.querySelector('.ally') };
  const captureMark = el('span', { className: 'capture-mark' });
  sides.enemy.querySelector('.hp-bar').append(captureMark);

  // The parts of a fighter's box, looked up once.
  const partsOf = (box) => ({
    name: box.querySelector('strong'), level: box.querySelector('.level'), bar: box.querySelector('.hp-bar'),
    text: box.querySelector('.hp-text'), tags: box.querySelector('.tags'),
  });
  const parts = { enemy: partsOf(sides.enemy), ally: partsOf(sides.ally) };
  // Every frame: the page is touched only when something changed (setText).
  const setWidth = (node, width) => { if (node.style.width !== width) node.style.width = width; };

  function showSide(side, name, level, hp, maxHp) {
    const p = parts[side];
    setText(p.name, name);
    setText(p.level, `niv. ${level}`);
    const ratio = maxHp ? hp / maxHp : 0;
    setWidth(p.bar.firstElementChild, `${100 * ratio}%`);
    p.bar.classList.toggle('mid', ratio <= 0.5 && ratio > 0.25);
    p.bar.classList.toggle('low', ratio <= 0.25);
    setText(p.text, `PV ${hp} / ${maxHp}`);
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
      setText(parts.enemy.name, trainer.name);
      setText(parts.enemy.level, `niv. ${trainer.level}`);
      setText(parts.enemy.text, 'Entraînement : son bouclier encaisse tes attaques.');
      if (tagsFor !== '') parts.enemy.tags.replaceChildren();
      tagsFor = '';
      captureMark.hidden = true;
      sides.ally.hidden = !b.ally;
      if (b.ally) showSide('ally', speciesName(b.ally.species), b.ally.level, b.ally.hp, b.ally.maxHp);
      return;
    }
    trainer = null;
    const aimed = b.target >= 0 && b.enemy;
    sides.enemy.classList.toggle('free', !aimed);
    if (aimed) {
      showSide('enemy', speciesName(b.enemy.species), b.enemy.level, b.enemy.hp, b.enemy.maxHp);
    } else {
      setText(parts.enemy.name, b.rival && !(b.foes || []).length
        ? `${npcName(b.rival.npc)} envoie son prochain monstre…` : 'Mode libre');
      setText(parts.enemy.level, '');
      setText(parts.enemy.text, (b.foes || []).length ? 'Touche un adversaire, ou son icône, pour le viser.' : '');
      setWidth(parts.enemy.bar.firstElementChild, '0px');
    }
    sides.ally.hidden = !b.ally;
    if (b.ally) showSide('ally', speciesName(b.ally.species), b.ally.level, b.ally.hp, b.ally.maxHp);
    if (captureMark.style.left !== `${b.captureHpPercent}%`) captureMark.style.left = `${b.captureHpPercent}%`;
    captureMark.hidden = !!b.rival || !aimed;
    sides.enemy.classList.toggle('windup', (b.foes || []).some((f) => f.fighter.state === 'windup'));
    const tags = aimed ? `${b.enemy.index}` : '';
    if (tagsFor !== tags) {
      tagsFor = tags;
      parts.enemy.tags.replaceChildren(...(aimed ? b.enemy.elements : []).map((id) => {
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
          onTap(button, () => { if (game) toggleTarget(game.battle(), x.slot); });
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
    setText(away, b.phase === 'training'
      ? `Trop loin ! Reviens vers ton magicien : fin de l’entraînement dans ${left} s`
      : `Trop loin ! Reviens à portée : fin du combat dans ${left} s`);
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
    parryButton.style.setProperty('--cd', Math.min(1, f.parryCooldown / 2));  // 2 s (rules::kParryTicks + kParryCooldown)
  }

  // ---- Damage numbers and the outcome ----
  const layer = $('damage-layer');
  const seen = { allyHit: 0, allyParried: 0, shield: 0, outcome: -1, mercy: -1 };
  const seenFoes = new Map();  // creature index -> { hit, parried }
  // Another world is played (saves.js): what was seen in the one before is forgotten.
  window.addEventListener('monstres-world', () => {
    Object.assign(seen, { allyHit: 0, allyParried: 0, shield: 0, outcome: -1, mercy: -1 });
    seenFoes.clear();
    selected = -1;
    object = -1;
    quiet = false;
  });

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
    const rival = o.rival ? npcName(o.rival) : '';
    const texts = o.rival ? {
      victory: ['Victoire !', `Tu as battu ${rival} !`],
      defeat: [`${rival} a gagné`, 'Tes monstres sont épuisés.'],
      forfeit: ['Tu as déclaré forfait', `${rival} remporte le combat.`],
    }[o.result] : {
      victory: ['Victoire !', o.count > 1 ? `${o.count} monstres sont K.O.` : `${foe} est K.O.`],
      captured: [`${foe} capturé !`, 'Il rejoint ton équipe'],
      defeat: [`${companion} est K.O.`, 'Soigne-le d’un sort, ou va voir Odette au village.'],
      fled: ['Tu as pris la fuite', ''],
      escaped: [`${foe} s’est libéré`, 'Il s’enfuit : le sceau n’a pas tenu.'],
    }[o.result];
    if (!texts) return;
    const won = o.result === 'victory' || o.result === 'captured';
    const gains = (o.gains || []).filter((g) => g.species);  // also those beaten before a defeat
    const NO_XP = {
      beaten: o.count > 1 ? 'Déjà battus : pas d’expérience cette fois.' : 'Déjà battu : pas d’expérience cette fois.',
      released: 'Il avait été relâché : pas d’expérience cette fois.',
      trainer: `${rival || 'Ce dresseur'} ne donne de l’expérience qu’au premier combat gagné.`,
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


  // ---- Capturing: the circle of runes (Battle::seal, src/core/battle.cpp) ----
  // The runes of a series light up one after the other; then the player
  // touches them in the same order (keys 1 to 8 too). The bar: the escape on
  // the left, the capture on the right; it starts halfway, each rune right
  // moves it a little. The capture spells: spells.js.
  const RUNE_GLYPHS = [
    'M12 3v18M12 9l6-5M12 14l6-5',
    'M7 21V3l10 6v12',
    'M8 3v18M8 7l8 5-8 5',
    'M12 3v18M12 4l-6 5M12 9l-6 5',
    'M6 3l12 18M18 3L6 21',
    'M12 3l7 9-7 9-7-9z',
    'M6 21L12 3l6 18M8.5 14h7',
    'M8 3v18h9M8 11h6',
  ];
  const sealLayer = $('seal');
  const sealRing = sealLayer.querySelector('.seal-ring');
  const sealTitle = sealLayer.querySelector('.seal-title');
  const sealText = sealLayer.querySelector('.seal-text > span');
  const sealFill = sealLayer.querySelector('.seal-fill');
  const sealPips = sealLayer.querySelector('.seal-pips');
  const sealTop = sealLayer.querySelector('.seal-top');
  const sealTools = $('seal-tools');
  const svgNs = 'http://www.w3.org/2000/svg';
  const svg = (tag, attrs, children = []) => {
    const node = document.createElementNS(svgNs, tag);
    for (const [k, v] of Object.entries(attrs)) node.setAttribute(k, v);
    node.append(...children);
    return node;
  };
  let sealStage = 'none';
  let runeButtons = [];
  let lastTouch = 0;
  function buildRing(count) {
    const ring = svg('svg', { viewBox: '0 0 100 100', 'aria-hidden': 'true' }, [
      svg('g', { class: 'turn' }, [
        svg('circle', { cx: 50, cy: 50, r: 47, 'stroke-width': 0.6, 'stroke-dasharray': '2 3', opacity: 0.8 }),
        svg('circle', { cx: 50, cy: 50, r: 42, 'stroke-width': 0.4, opacity: 0.6 }),
        svg('polygon', { points: Array.from({ length: count }, (_, k) => {
          const a = -Math.PI / 2 + k * 2 * Math.PI / count;
          return `${50 + 42 * Math.cos(a)},${50 + 42 * Math.sin(a)}`;
        }).join(' '), fill: 'none', stroke: 'var(--seal-color)', 'stroke-width': 0.35, opacity: 0.5 }),
      ]),
    ]);
    // Two at the top, two at the bottom, one on each side: none under the bar.
    runeButtons = Array.from({ length: count }, (_, k) => {
      const a = -Math.PI / 2 + (k * 2 + 1) * Math.PI / count;
      const button = el('button', { type: 'button', className: 'rune', disabled: true });
      button.setAttribute('aria-label', `Rune ${k + 1}`);
      button.style.left = `${50 + 50 * Math.cos(a)}%`;
      button.style.top = `${50 + 50 * Math.sin(a)}%`;
      button.append(svg('svg', { viewBox: '0 0 24 24', fill: 'none', stroke: 'currentColor', 'stroke-width': 2.2,
        'stroke-linecap': 'round', 'stroke-linejoin': 'round', 'aria-hidden': 'true' },
      [svg('path', { d: RUNE_GLYPHS[k % RUNE_GLYPHS.length] })]), el('kbd', { textContent: String(k + 1) }));
      // At once, finger down: quick sequences, and two hands.
      let pressed = -Infinity;
      button.addEventListener('pointerdown', (event) => {
        event.preventDefault();
        pressed = event.timeStamp;
        if (game && !button.disabled) game.rune(k);
      });
      button.addEventListener('click', (event) => {
        event.preventDefault();
        if (event.timeStamp - pressed < 1000) return;  // that press's own click
        if (game && !button.disabled) game.rune(k);  // the keyboard's Enter
      });
      return button;
    });
    sealRing.replaceChildren(ring, ...runeButtons);
  }
  window.addEventListener('keydown', (event) => {
    if (!game || sealStage !== 'input' || event.repeat || event.ctrlKey || event.metaKey || event.altKey) return;
    const k = /^(Digit|Numpad)([1-9])$/.exec(event.code);
    if (!k || Number(k[2]) > runeButtons.length) return;
    event.preventDefault();
    event.stopImmediatePropagation();  // not an attack
    game.rune(Number(k[2]) - 1);
  }, true);

  const percent = (x) => `${x.toLocaleString('fr-FR', { maximumFractionDigits: 1 })} %`;
  const SEAL_TEXTS = {
    show: (s) => (s.done || s.credited ? 'Observe encore l’ordre des runes…' : 'Observe l’ordre des runes…'),
    input: (s) => `À toi : touche les runes dans le même ordre (${s.done}/${s.length}) · +${percent(s.boost ? s.gain * 1.5 : s.gain)} par rune`,
    right: (s) => (s.power >= 100 ? 'Le sceau est complet !' : 'Série réussie ! Une autre…'),
    wrong: (s) => (s.guard ? 'Raté… mais ta protection tient : rien de perdu' : `Raté ! Il se débat (−${percent(s.loss)})`),
    caught: () => 'Le sceau est complet !',
    escaped: () => 'Il s’est libéré !',
  };
  function renderSeal(b) {
    const s = b.seal;
    root.classList.toggle('sealing', !!s);
    if (!s) {
      if (sealStage !== 'none') {
        sealStage = 'none';
        sealLayer.hidden = true;
        sealRing.classList.add('closed');
      }
      return;
    }
    if (runeButtons.length !== s.runes) buildRing(s.runes);
    if (sealLayer.hidden) {
      sealLayer.hidden = false;
      lastTouch = s.touchTick;
    }
    sealStage = s.stage;
    const color = (LORE.elements[s.element] || { color: '#f2c94c' }).color;
    sealLayer.style.setProperty('--seal-color', color);
    sealLayer.className = s.stage;
    // Around the monster, where it is drawn (the camera glides to it first),
    // but whole on the screen: below the bar, left of the capture spells.
    const at = game.creatureOnScreen(s.foe);
    if (at) {
      const r = sealRing.offsetWidth / 2 + 34;  // the runes stand out of the ring
      const top = sealTop.getBoundingClientRect().bottom + 2;
      const right = sealTools.getBoundingClientRect().left - 4;
      const fit = (v, lo, hi) => (lo > hi ? (lo + hi) / 2 : Math.min(hi, Math.max(lo, v)));
      sealRing.style.left = `${fit(at.x, r, right - r)}px`;
      sealRing.style.top = `${fit(at.y, top + r, window.innerHeight - r)}px`;
    }
    // The ring opens once the camera is on the monster.
    sealRing.classList.toggle('closed', s.stage === 'show' && s.round === 0 && s.lit < 0 && s.timer < 40);
    setText(sealTitle, `Capture · ${speciesName(s.species)} · ${percent(Math.round(Math.max(0, Math.min(100, s.bar)) * 2) / 2)}`);
    const helped = [s.guard ? 'protégé' : '', s.boost ? 'renforcé' : ''].filter(Boolean).join(', ');
    setText(sealText, (SEAL_TEXTS[s.stage] || (() => ''))(s) + (helped && s.stage !== 'wrong' ? ` · ${helped}` : ''));
    sealFill.style.width = `${Math.max(0, Math.min(100, s.bar))}%`;
    if (sealPips.childElementCount !== s.length) {
      sealPips.replaceChildren(...Array.from({ length: s.length }, () => el('i')));
    }
    const done = s.stage === 'right' || s.stage === 'caught' ? s.length : s.stage === 'input' ? s.done : 0;
    [...sealPips.children].forEach((pip, i) => pip.classList.toggle('on', i < done));
    const touched = s.touchTick !== lastTouch;
    if (touched) lastTouch = s.touchTick;
    runeButtons.forEach((button, k) => {
      button.disabled = s.stage !== 'input';
      const lit = s.lit === k;
      button.classList.toggle('lit', lit);
      button.classList.toggle('hint', s.stage === 'input' && s.hint === k);
      if (lit) button.style.setProperty('--glow', String(1 - s.glow * 0.5));
      if (touched && k === s.touched) {
        const kind = s.stage === 'wrong' ? 'bad' : 'good';
        button.classList.remove('good', 'bad');
        void button.offsetWidth;
        button.classList.add(kind);
        clearTimeout(button.flash);
        button.flash = setTimeout(() => button.classList.remove(kind), kind === 'bad' ? 500 : 220);
      }
    });
  }

  // ---- Every frame ----
  let fighting = false;
  let quiet = false;  // no battle, nothing selected: the game is asked less often
  let skipped = 0;
  const QUIET_FRAMES = 6;  // about ten times a second
  function frame() {
    requestAnimationFrame(frame);
    if (!game || typeof game.battle !== 'function') return;
    if (quiet && ++skipped < QUIET_FRAMES) return;
    skipped = 0;
    const b = game.battle();
    quiet = b.phase === 'none' && selected < 0 && object < 0;
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
    renderSeal(b);
  }

  const start = () => {
    game = window.monstres;
    requestAnimationFrame(frame);
  };
  whenReady(start);
})();
