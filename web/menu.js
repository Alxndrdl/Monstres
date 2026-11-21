// The grimoire (button at the top left): character sheet, team, bestiary and maps.
// Time stands still while it is open. Texts come from web/lore.js, data from
// the game (window.monstres: wizard(), bestiary(), map(), portrait()).
(() => {
  'use strict';

  const LORE = window.MonstresLore;
  let game = null;

  const { $, el, prefs, whenReady } = window.MonstresUtil;
  whenReady((g) => { game = g; });

  // ---- Portraits drawn by the game, kept as images ----
  const portraits = new Map();
  function portraitUrl(species, size = 256, look = 0) {
    const key = `${species}/${size}/${look}`;
    if (!portraits.has(key)) {
      const canvas = el('canvas', { width: size, height: size });
      canvas.getContext('2d').putImageData(game.portrait(species, size, look), 0, 0);
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

  // options.teleport: the map, to choose where to go (from a teleporter).
  let teleportMode = false;
  function open(tab = current, options = {}) {
    if (!game) return;
    teleportMode = !!options.teleport;
    if (tab === 'map') openMap(teleportMode);
    else mapData = null;  // read again when the map is shown
    menu.hidden = false;
    button.setAttribute('aria-expanded', 'true');
    game.setPaused(true);
    show(tab);
  }

  function close() {
    if (!isOpen()) return;
    menu.hidden = true;
    teleportMode = false;
    button.setAttribute('aria-expanded', 'false');
    if (document.activeElement && menu.contains(document.activeElement)) document.activeElement.blur();
    if (game) game.setPaused(false);
  }

  function show(tab) {
    if (!['character', 'team', 'bag', 'spells', 'quests', 'bestiary', 'map', 'guide'].includes(tab)) tab = 'character';
    if (tab !== 'map') teleportMode = false;
    current = tab;
    prefs.set('menuTab', tab);
    for (const t of tabs) {
      const selected = t.dataset.tab === tab;
      t.setAttribute('aria-selected', String(selected));
      t.tabIndex = selected ? 0 : -1;
      $(t.getAttribute('aria-controls')).hidden = !selected;
    }
    if (tab === 'character') renderCharacter();
    else if (tab === 'team') renderTeam();
    else if (tab === 'bag') renderBag();
    else if (tab === 'spells') window.MonstresSpells.renderTab();
    else if (tab === 'quests') renderQuests();
    else if (tab === 'bestiary') renderBestiary();
    else if (tab === 'guide') window.MonstresGuide.renderTab();
    else renderMap();
  }

  button.addEventListener('click', () => (isOpen() ? close() : open()));
  window.MonstresMenu = { open, close, isOpen, portraitUrl: (...args) => portraitUrl(...args) };
  $('menu-close').addEventListener('click', close);
  menu.addEventListener('pointerdown', (event) => {
    if (event.target === menu) close();  // the dark backdrop around the window
  });
  for (const t of tabs) t.addEventListener('click', () => show(t.dataset.tab));

  // Keyboard: Escape closes; C, E, I, P, J, B, M and H open a tab (not while typing a name).
  // By letter, not by key position: M is not at the same place on AZERTY.
  const SHORTCUTS = { c: 'character', e: 'team', i: 'bag', p: 'spells', j: 'quests', b: 'bestiary', m: 'map', h: 'guide' };
  window.addEventListener('keydown', (event) => {
    const typing = event.target instanceof HTMLElement && ['INPUT', 'TEXTAREA'].includes(event.target.tagName);
    if (event.key === 'Escape' && releaseOpen()) {
      event.stopImmediatePropagation();
      keepIt();  // Escape keeps it
    } else if (releaseOpen()) {
      // The window asks first: no shortcuts behind it.
    } else if (event.key === 'Escape' && isOpen()) {
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

  // ---- The bag ----
  const ITEM_ICONS = { coin: '🪙', manaPotion: '🧪', strangeStone: '🪨', goods: '📦' };
  let chosenItem = prefs.get('bagItem', 'coin');
  function renderBag() {
    const bag = game.bag();
    const items = bag.items.filter((it) => it.count > 0 || it.id === 'coin');
    $('bag-coins').textContent = `${bag.coins} ${bag.coins > 1 ? 'pièces' : 'pièce'}`;
    if (!items.some((it) => it.id === chosenItem)) chosenItem = items[0].id;
    $('bag-list').replaceChildren(...items.map((it) => {
      const info = LORE.items[it.id] || { name: it.id };
      const b = el('button', { type: 'button', className: 'bestiary-entry' }, [
        el('span', { className: 'bag-icon', textContent: ITEM_ICONS[it.id] || '•' }),
        el('span', {}, [el('strong', { textContent: info.name }), el('small', { textContent: `× ${it.count}` })]),
      ]);
      b.id = `bag-${it.id}`;
      if (it.id === chosenItem) b.setAttribute('aria-current', 'true');
      b.addEventListener('click', () => {
        chosenItem = it.id;
        prefs.set('bagItem', it.id);
        renderBag();
      });
      return el('li', {}, [b]);
    }));
    const it = items.find((x) => x.id === chosenItem);
    const info = LORE.items[it.id] || { name: it.id, description: '' };
    const again = () => setTimeout(renderBag, 80);
    const actions = [];
    if (it.id === 'manaPotion') {
      const drink = el('button', { type: 'button', className: 'button', id: 'bag-drink', textContent: 'Boire',
        disabled: !bag.canDrink });
      drink.addEventListener('click', () => { game.useItem(it.item); again(); });
      actions.push(drink);
    }
    if (!it.quest && it.count > 0) {
      const one = el('button', { type: 'button', className: 'button', id: 'bag-discard-one', textContent: 'Jeter 1' });
      one.addEventListener('click', () => { game.discard(it.item, 1); again(); });
      const all = el('button', { type: 'button', className: 'button danger', id: 'bag-discard-all', textContent: 'Tout jeter' });
      all.addEventListener('click', () => { game.discard(it.item, 0); again(); });
      actions.push(one, all);
    }
    $('bag-detail').replaceChildren(
      el('h2', { textContent: `${ITEM_ICONS[it.id] || ''} ${info.name}` }),
      el('p', { className: 'kind', textContent: it.quest ? 'Objet de quête : impossible de s’en débarrasser' : `${it.count} dans ton sac` }),
      el('p', { className: 'description', textContent: info.description }),
      ...(it.id === 'manaPotion' && !bag.canDrink && it.count > 0
        ? [el('p', { className: 'facts', textContent: 'Ton mana est déjà plein.' })] : []),
      el('div', { className: 'bag-actions' }, actions),
    );
  }

  // ---- Character sheet ----

  // [label, value, max for the bar, text shown (else the value)].
  function statList(target, entries) {
    target.replaceChildren(...entries.map(([label, value, max, text]) => el('div', { className: 'stat' }, [
      el('dt', { textContent: label }),
      el('dd', { textContent: text === undefined ? String(value) : text }),
      el('div', { className: 'bar' }, [el('span', { style: `width:${Math.min(100, (100 * value) / max)}%` })]),
    ])));
  }

  // The player's record: what it caught, won, beat.
  const recordList = (target, entries) => target.replaceChildren(...entries.map(([label, value]) =>
    el('div', {}, [el('dt', { textContent: label }), el('dd', { textContent: String(value) })])));

  function renderCharacter() {
    const w = game.wizard();
    $('wizard-portrait').src = portraitUrl(-1);
    $('wizard-name').textContent = w.name || 'Magicien sans nom';  // chosen with the game (saves.js)
    $('wizard-rank').textContent = `Apprenti magicien · niveau ${w.level}`;
    $('wizard-xp-fill').style.width = `${Math.min(100, (100 * w.xp) / w.xpToNext)}%`;
    $('wizard-xp-text').textContent = `Expérience : ${w.xp} / ${w.xpToNext}`;
    // The mana, as a monster's hit points: what is left of its most.
    $('wizard-mana-fill').style.width = `${w.mana ? Math.min(100, (100 * w.manaNow) / w.mana) : 0}%`;
    $('wizard-mana-text').textContent = `Mana : ${w.manaNow} / ${w.mana}`;
    const r = w.record || {};
    recordList($('wizard-record'), [
      ['Monstres capturés', r.captured || 0], ['Combats gagnés', r.battlesWon || 0], ['Monstres battus', r.beaten || 0],
    ]);
    const zone = game.state().player.zone;
    $('wizard-where').textContent = `Tu te trouves : ${LORE.zones[zone] || zone}.`;
  }

  // ---- Team: the monsters of the player, the companion first ----
  let chosenMember = prefs.get('teamSlot', 0);
  // The sex, as its sign only: blue for males, pink for females.
  const sexSign = (female) => el('span', {
    className: `sex ${female ? 'female' : 'male'}`, textContent: female ? '♀' : '♂', title: female ? 'Femelle' : 'Mâle',
  });
  const rarityColor = (r) => (LORE.rarities[r] || { color: '#c9c4d8' }).color;

  function moveName(move) {
    const text = LORE.moves[move.id] || { name: move.id };
    return (text.byElement && text.byElement[move.element]) || text.name;
  }

  function hpBar(hp, max) {
    const ratio = max ? hp / max : 0;
    return el('div', { className: 'bar hp-bar' }, [el('span', {
      style: `width:${Math.min(100, 100 * ratio)}%;background:${ratio > 0.5 ? '#8fd16a' : ratio > 0.25 ? '#f2c94c' : '#ff7a6b'}`,
    })]);
  }

  function renderTeam() {
    const team = game.team();
    if (chosenMember >= team.length) chosenMember = 0;
    $('team-list').replaceChildren(...team.map((m) => {
      const info = LORE.species[m.species];
      const item = el('button', { type: 'button', className: 'bestiary-entry team-entry' }, [
        el('img', { className: 'portrait', src: portraitUrl(speciesIndex(m.species), 96, m.look), alt: '' }),
        el('span', {}, [
          el('strong', { textContent: info.name }),
          el('small', { textContent: `Niveau ${m.level}${m.active ? ' · compagnon' : ''}` }),
          hpBar(m.hp, m.maxHp),
        ]),
      ]);
      item.dataset.slot = m.slot;
      if (m.slot === chosenMember) item.setAttribute('aria-current', 'true');
      item.addEventListener('click', () => {
        chosenMember = m.slot;
        prefs.set('teamSlot', chosenMember);
        renderTeam();
      });
      return el('li', {}, [item]);
    }));
    $('team-count').textContent = `${team.length}/6 monstres`;
    renderMember(team[chosenMember], team.length);
  }

  // Letting a monster go back to the wild: a window in the middle of the
  // screen asks to confirm (the grimoire then closes, to see it go).
  const releaseDialog = $('release-dialog');
  const releaseOpen = () => !releaseDialog.hidden;
  function askRelease(m) {
    const name = LORE.species[m.species].name;
    releaseDialog.querySelector('.release-portrait').src = portraitUrl(speciesIndex(m.species), 128, m.look);
    releaseDialog.querySelector('.release-title').textContent = `Relâcher ${name} ?`;
    releaseDialog.querySelector('.release-level').textContent = `Niveau ${m.level}`;
    $('release-confirm').textContent = `Relâcher ${name}`;
    $('release-cancel').textContent = `Garder ${name}`;
    $('release-confirm').onclick = () => {
      releaseDialog.hidden = true;
      game.release(m.slot);
      chosenMember = 0;
      prefs.set('teamSlot', 0);
      close();  // to see it go
    };
    releaseDialog.hidden = false;
    $('release-cancel').focus();
  }
  function keepIt() {
    releaseDialog.hidden = true;
    renderTeam();
  }
  $('release-cancel').addEventListener('click', keepIt);
  releaseDialog.addEventListener('pointerdown', (event) => {
    if (event.target === releaseDialog) keepIt();  // the dark backdrop
  });
  function releaseBox(m, teamSize) {
    const name = LORE.species[m.species].name;
    const phase = game.battle().phase;
    const box = el('section', { className: 'release-box' });
    if (teamSize < 2) {
      box.append(el('p', { className: 'facts', textContent: 'Ton seul monstre ne peut pas être relâché.' }));
      return box;
    }
    if (phase !== 'none') {
      box.append(el('p', { className: 'facts', textContent: 'Pas pendant un combat ou un entraînement.' }));
      return box;
    }
    const release = el('button', { type: 'button', className: 'button danger', id: 'release', textContent: `Relâcher ${name}…` });
    release.addEventListener('click', () => askRelease(m));
    box.append(release, el('p', { className: 'facts', textContent: 'Il sort du cristal et retourne vivre dans la nature.' }));
    return box;
  }

  // Sizes, weights and ages, the French way.
  const decimal = (v, digits) => v.toLocaleString('fr-FR', { maximumFractionDigits: digits });
  const sizeText = (mm) => (mm >= 1000 ? `${decimal(mm / 1000, 2)} m` : `${decimal(mm / 10, 1)} cm`);
  const weightText = (g) => (g >= 1000 ? `${decimal(g / 1000, g >= 10000 ? 1 : 2)} kg` : `${g} g`);
  function ageText(days) {
    if (days < 31) return `${days} jour${days > 1 ? 's' : ''}`;
    const months = Math.floor(days / 30.4);
    if (days < 365) return `${months} mois`;
    const years = Math.floor(days / 365), rest = Math.floor((days - 365 * years) / 30.4);
    return `${years} an${years > 1 ? 's' : ''}${rest ? ` et ${rest} mois` : ''}`;
  }
  const buildName = (form) => (LORE.builds[form] || { name: form }).name;

  // A modifier's source: the variant's name (a build: its name).
  const sourceName = (species, id) => (LORE.builds[id] ? LORE.builds[id].name : variantText(species, id).name);
  const signed = (v) => (v > 0 ? `+${v}` : `${v}`);

  // The gifts as a spider web: one axis each, 0 a little away from the
  // middle (the lowest values stay apart), 20 at the edge; the range its
  // variants allow drawn faintly behind. Beside it, the chosen gift (touched,
  // or the first one; hovering shows another for a moment): its value, its
  // range, and where its modifiers come from.
  function giftWeb(m) {
    const NS = 'http://www.w3.org/2000/svg';
    const details = m.giftDetails;
    const box = el('div', { className: 'gift-web-box' });
    const svg = document.createElementNS(NS, 'svg');
    svg.setAttribute('viewBox', '-185 -100 370 200');
    svg.setAttribute('class', 'gift-web');
    svg.setAttribute('role', 'img');
    svg.setAttribute('aria-label', details.map((d, k) => `${LORE.gifts[k]} ${d.value}`).join(', '));
    const R = 68, ZERO = 0.2, n = details.length;
    const point = (k, v) => {
      const a = -Math.PI / 2 + (2 * Math.PI * k) / n;
      const r = R * (ZERO + (1 - ZERO) * Math.max(0, v) / 20);
      return [Math.cos(a) * r, Math.sin(a) * r];
    };
    const add = (tag, attrs, parent = svg) => {
      const node = document.createElementNS(NS, tag);
      for (const [k, v] of Object.entries(attrs)) node.setAttribute(k, v);
      parent.append(node);
      return node;
    };
    const poly = (values) => values.map((v, k) => point(k, v).join(',')).join(' ');
    for (const level of [0, 5, 10, 15, 20]) {
      add('polygon', { points: poly(details.map(() => level)), class: level === 20 ? 'web-edge' : level === 0 ? 'web-zero' : 'web-ring' });
    }
    for (let k = 0; k < n; ++k) {
      const [x0, y0] = point(k, 0), [x1, y1] = point(k, 20);
      add('line', { x1: x0, y1: y0, x2: x1, y2: y1, class: 'web-axis' });
    }
    add('polygon', { points: poly(details.map((d) => d.max)), class: 'web-range' });
    add('polygon', { points: poly(details.map((d) => d.min)), class: 'web-range-low' });
    add('polygon', { points: poly(details.map((d) => d.value)), class: 'web-value' });

    const tip = el('div', { className: 'gift-info' });
    tip.setAttribute('aria-live', 'polite');
    let chosen = 0;  // touched (or the first one): shown when nothing is hovered
    const showTip = (k) => {
      const d = details[k];
      const mods = d.mods.length ? d.mods.map((x) => el('li', {}, [
        el('span', { textContent: sourceName(m.species, x.source) }),
        el('strong', { className: x.value > 0 ? 'up' : 'down', textContent: signed(x.value) }),
      ])) : [el('li', { className: 'none', textContent: 'Aucun modificateur' })];
      const total = d.mods.reduce((a, x) => a + x.value, 0);
      const capped = d.base + total !== d.value;
      tip.replaceChildren(
        el('div', { className: 'tip-head' }, [el('strong', { textContent: LORE.gifts[k] }), el('span', { textContent: String(d.value) })]),
        el('p', { textContent: `Base tirée : ${d.base} (entre 5 et 15)${capped ? ' · ramenée entre 0 et 20' : ''}` }),
        el('ul', {}, mods),
        el('p', { className: 'tip-range', textContent: `Minimum ${d.min} · maximum ${d.max}` }),
      );
      for (const g of svg.querySelectorAll('.web-hit')) g.classList.toggle('on', Number(g.dataset.gift) === k);
    };
    const hideTip = () => showTip(chosen);
    details.forEach((d, k) => {
      const [x, y] = point(k, d.value);
      const [lx, ly] = point(k, 26);
      const group = add('g', { class: 'web-hit', tabindex: 0, 'data-gift': k });
      group.setAttribute('aria-label', `${LORE.gifts[k]} ${d.value}, de ${d.min} à ${d.max}`);
      const [ex, ey] = point(k, 20);
      add('line', { x1: 0, y1: 0, x2: ex, y2: ey, class: 'web-hit-area' }, group);
      add('circle', { cx: x, cy: y, r: 4, class: 'web-dot' }, group);
      const text = `${LORE.gifts[k]} ${d.value}`;
      // An easy target around the label (a text is only hit on its letters).
      const width = 6.5 * text.length + 8;
      const left = Math.abs(lx) < 5 ? lx - width / 2 : lx > 0 ? lx - 4 : lx - width + 4;
      add('rect', { x: left, y: ly - 8, width, height: 18, class: 'web-hit-label' }, group);
      const label = add('text', { x: lx, y: ly + 4, class: 'web-label' }, group);
      label.setAttribute('text-anchor', Math.abs(lx) < 5 ? 'middle' : lx > 0 ? 'start' : 'end');
      label.textContent = text;
      group.addEventListener('pointerenter', (event) => { if (event.pointerType === 'mouse') showTip(k); });
      group.addEventListener('pointerleave', (event) => { if (event.pointerType === 'mouse') hideTip(); });
      group.addEventListener('focus', () => { chosen = k; showTip(k); });
      group.addEventListener('click', () => { chosen = k; showTip(k); });
    });
    box.append(svg);
    showTip(chosen);
    return el('div', { className: 'gift-layout' }, [box, tip]);
  }

  function renderMember(m, teamSize) {
    const info = LORE.species[m.species];
    // Its variants, each in the color of its rarity (as the bestiary lists them).
    const entry = game.bestiary().find((e) => e.species === m.species) || { variants: [] };
    const variants = m.variants.filter((id) => !id.startsWith('sex.') && !LORE.builds[id]).map((id) => {
      const v = entry.variants.find((x) => x.id === id) || {};
      const span = el('span', { textContent: (id.includes('.') ? '' : '✦ ') + (variantText(m.species, id).name || id),
        title: (LORE.rarities[v.rarity] || { name: '' }).name });
      span.style.setProperty('--rarity', rarityColor(v.rarity));
      return span;
    });
    const build = m.form !== 'normal' ? el('span', { className: `badge build ${m.form}`,
      textContent: `${(LORE.builds[m.form] || {}).sign || ''} ${buildName(m.form)}` }) : '';
    const moves = el('ol', { className: 'moves' }, m.moves.map((move, i) => {
      const element = LORE.elements[move.element] || { name: move.element, color: '#ccc' };
      const text = LORE.moves[move.id] || { description: '' };
      return el('li', { className: 'move' }, [
        el('div', { className: 'move-head' }, [
          el('strong', { textContent: moveName(move) }),
          el('span', { className: 'badge', textContent: element.name, style: `background:${element.color}` }),
        ]),
        el('p', {
          className: 'facts',
          textContent: `${i === 0 ? 'Attaque rapide' : 'Attaque spéciale'} · ${LORE.moveShapes[move.shape]} · ` +
            `puissance ${move.power} · ${move.shape === 'burst' ? 'se charge 1 s · ' : ''}` +
            `recharge ${move.cooldown.toLocaleString('fr-FR', { maximumFractionDigits: 1 })} s`,
        }),
        el('p', { className: 'move-text', textContent: text.description }),
      ]);
    }));
    // The fourth: every monster parries (rules::kParryTicks, kParryCooldown in battle.hpp).
    moves.append(el('li', { className: 'move' }, [
      el('div', { className: 'move-head' }, [el('strong', { textContent: LORE.parry.name }),
        el('span', { className: 'badge', textContent: LORE.elements.normal.name, style: `background:${LORE.elements.normal.color}` })]),
      el('p', { className: 'facts', textContent: 'Défense · garde 0,5 s · recharge 2 s' }),
      el('p', { className: 'move-text', textContent: LORE.parry.description }),
    ]));
    $('team-detail').className = 'bestiary-detail member';
    $('team-detail').replaceChildren(
      el('img', { className: 'portrait', src: portraitUrl(speciesIndex(m.species), 256, m.look), alt: info.name }),
      el('div', {}, [
        el('h2', { textContent: info.name }),
        el('p', { className: 'kind' }, [sexSign(m.female), ` ${info.kind}`]),
        el('div', { className: 'badges' }, [familyBadge(m.family), ...elementBadges(m.elements), build]),
        el('p', { className: 'rank', textContent: `Niveau ${m.level}` }),
        el('div', { className: 'xp' }, [
          el('div', { className: 'bar' }, [el('span', { style: `width:${Math.min(100, (100 * m.xp) / m.xpToNext)}%` })]),
          el('span', { textContent: `Expérience : ${m.xp} / ${m.xpToNext}` }),
        ]),
        el('div', { className: 'xp hp' }, [hpBar(m.hp, m.maxHp), el('span', { textContent: `PV : ${m.hp} / ${m.maxHp}` })]),
        variants.length ? el('p', { className: 'variant-list' }, variants) : '',
        el('p', { className: 'facts measures', textContent: `Taille : ${sizeText(m.sizeMm)} · poids : ` +
          `${weightText(m.weightG)} · âge : ${ageText(m.ageDays)}` }),
      ]),
      el('section', { className: 'gifts-box' }, [el('h3', { textContent: 'Aptitudes' }), giftWeb(m)]),
      el('section', { className: 'moves-box' }, [el('h3', { textContent: 'Attaques' }), moves]),
      releaseBox(m, teamSize),
    );
  }

  // ---- Quests: the journal ----
  let chosenQuest = prefs.get('questId', null);

  function renderQuests() {
    const quests = game.quests().filter((q) => q.stage !== 'unknown');
    const list = $('quest-list');
    if (!quests.length) {
      list.replaceChildren(el('li', { className: 'facts', textContent: 'Aucune quête pour l’instant.' }));
      $('quest-detail').replaceChildren(el('p', { className: 'facts',
        textContent: 'Parle aux villageois : certains ont besoin d’aide. Touche-les, puis « Parler ».' }));
      return;
    }
    if (!quests.some((q) => q.id === chosenQuest)) chosenQuest = (quests.find((q) => q.stage === 'active') || quests[0]).id;
    const section = (title, items) => (items.length ? [el('li', { className: 'quest-section', textContent: title }),
      ...items.map((q) => {
        const info = LORE.quests[q.id];
        const item = el('button', { type: 'button', className: `bestiary-entry quest-entry ${q.stage}` }, [
          el('span', {}, [el('strong', { textContent: info.title }),
            el('small', { textContent: q.stage === 'done' ? 'Terminée' : q.ready ? 'À rapporter' : 'En cours' })]),
        ]);
        item.dataset.quest = q.id;
        if (q.id === chosenQuest) item.setAttribute('aria-current', 'true');
        item.addEventListener('click', () => {
          chosenQuest = q.id;
          prefs.set('questId', q.id);
          renderQuests();
        });
        return el('li', {}, [item]);
      })] : []);
    list.replaceChildren(...section('En cours', quests.filter((q) => q.stage === 'active')),
      ...section('Terminées', quests.filter((q) => q.stage === 'done')));
    const q = quests.find((x) => x.id === chosenQuest);
    const info = LORE.quests[q.id];
    const giver = (LORE.npcs[info.giver] || { name: '?' }).name;
    // A step is done (true), or counted (so many of `of`: the bundles, the raiders, the lambs).
    const steps = Object.entries(info.steps).map(([id, label]) => {
      const v = q.steps[id];
      const counted = typeof v === 'number';
      const done = counted ? v >= (q.steps.of || 1) : !!v;
      return el('li', { className: done ? 'step done' : 'step', textContent: counted ? `${label} (${v}/${q.steps.of})` : label });
    });
    $('quest-detail').className = 'bestiary-detail quest';
    $('quest-detail').replaceChildren(
      el('h2', { textContent: info.title }),
      el('p', { className: 'kind', textContent: `${info.main ? 'Quête principale · ' : ''}Demandée par ${giver} · ${q.stage === 'done' ? 'terminée' : 'en cours'}` }),
      el('p', { className: 'description', textContent: info.summary }),
      el('h3', { textContent: 'Étapes' }),
      el('ol', { className: 'quest-steps' }, steps),
      el('p', { className: 'facts', textContent: q.steps.rewarded && q.stage !== 'done' ? `Récompense : aucune. ${info.again}`
        : `Récompense : ${info.xp} points d’expérience pour ton magicien, et pour ton compagnon.` }),
      ...(q.stage === 'done' ? [el('p', { className: 'quest-done', textContent: info.done })] : []),
    );
  }

  // ---- Bestiary ----
  const MONSTER_STATS = [
    ['hp', 'Points de vie', 50], ['attack', 'Attaque', 15], ['defense', 'Défense', 15],
    ['speed', 'Vitesse', 15], ['magic', 'Magie', 15],
  ];
  let chosen = prefs.get('bestiarySpecies', 'companion');
  let dnaOpen = false;      // the variants of the chosen species instead of its sheet
  let habitatOpen = false;  // or where it lives, on the map
  let chosenVariant = null; // variant shown below the grid

  // A strand of DNA: the button to the variants of a species.
  const DNA_ICON = '<svg class="dna-icon" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" ' +
    'stroke-linecap="round" aria-hidden="true"><path d="M7 2c0 6 10 5 10 10S7 16 7 22"/>' +
    '<path d="M17 2c0 6-10 5-10 10s10 4 10 10"/><path d="M8.6 5.5h6.8M9.6 9h4.8M9.6 15h4.8M8.6 18.5h6.8"/></svg>';
  const dnaIcon = () => el('span', { innerHTML: DNA_ICON, className: 'dna-icon-box' });

  const elementBadges = (ids) => ids.map((id) => {
    const element = LORE.elements[id] || { name: id, color: '#ccc' };
    return el('span', { className: 'badge', textContent: element.name, style: `background:${element.color}` });
  });
  const familyBadge = (id) => el('span', { className: 'badge family', textContent: LORE.families[id] || id });

  // A variant's name and text; the builds are the same for every species.
  function variantText(species, id) {
    const own = (LORE.species[species].variants || {})[id];
    if (own) return own;
    const build = LORE.builds[id];
    if (build) return { name: `${LORE.species[species].name} ${build.sign}`, description: build.description };
    return { name: id, description: '' };
  }
  const known = (e) => e.variants.filter((v) => v.status !== 'unknown').length;
  const captured = (e) => e.variants.filter((v) => v.status === 'captured').length;

  function renderBestiary() {
    const entries = game.bestiary();
    if (!entries.some((e) => e.species === chosen)) chosen = entries[0].species;
    const list = $('bestiary-list');
    list.replaceChildren(...entries.map((e) => {
      const info = LORE.species[e.species];
      const item = el('button', { type: 'button', className: 'bestiary-entry' }, [
        el('img', { className: 'portrait', src: portraitUrl(speciesIndex(e.species), 96), alt: '' }),
        el('span', {}, [
          el('strong', { textContent: info.name }),
          el('small', { textContent: info.kind }),
          el('small', { className: 'dna-count', title: 'Variantes capturées au moins une fois' },
            [dnaIcon(), `${captured(e)}/${e.variants.length}`]),
        ]),
      ]);
      item.dataset.species = e.species;
      if (e.species === chosen) item.setAttribute('aria-current', 'true');
      item.addEventListener('click', () => {
        if (chosen !== e.species) chosenVariant = null;  // its variants, or its habitat, stay open
        if (!e.habitat.length) habitatOpen = false;
        chosen = e.species;
        prefs.set('bestiarySpecies', chosen);
        renderBestiary();
      });
      return el('li', {}, [item]);
    }));

    const e = entries.find((x) => x.species === chosen);
    if (habitatOpen) renderHabitat(e);
    else if (dnaOpen) renderVariants(e);
    else renderSheet(e);
  }

  // Its call (web/audio.js), when there is sound.
  function cryButton(species) {
    const audio = window.MonstresAudio;
    if (!audio) return [];
    const on = audio.canPlay('effects');
    const b = el('button', { type: 'button', className: 'dna-button cry-button', id: 'cry-button',
      title: on ? 'Écouter son cri' : 'Les effets sonores sont coupés (paramètres)' }, [
      el('span', { textContent: '♪', ariaHidden: 'true' }), el('span', { textContent: 'Cri' })]);
    b.disabled = !on;
    b.addEventListener('click', () => audio.cry(species, 'call'));
    return [b];
  }

  function renderSheet(e) {
    const info = LORE.species[e.species];
    const stats = el('dl', { className: 'stats' });
    statList(stats, MONSTER_STATS.map(([key, label, max]) => [label, e[key], max]));
    // Where it lives: on the map.
    const habitat = el('button', { type: 'button', className: 'dna-button habitat-button', id: 'habitat-button' }, [
      el('span', { textContent: '⌖' }),
      el('span', { textContent: 'Habitat' }),
      ...(e.habitat.length ? [] : [el('small', { textContent: 'pas dans la nature' })]),
    ]);
    habitat.disabled = !e.habitat.length;
    habitat.addEventListener('click', () => {
      habitatOpen = true;
      Object.assign(habitatView, { cx: 0, cz: 0, scale: 0 });
      renderBestiary();
    });
    const dna = el('button', { type: 'button', className: 'dna-button', id: 'dna-button' }, [
      dnaIcon(),
      el('span', { textContent: 'Variantes' }),
      el('small', { textContent: `${captured(e)}/${e.variants.length} capturées` }),
    ]);
    // What the player did with this species so far.
    const record = el('dl', { className: 'record' }, [
      ['Variantes capturées', `${captured(e)} / ${e.variants.length}`],
      ['Variantes aperçues', `${known(e)} / ${e.variants.length}`],
      ['Monstres capturés', e.captured],
      ['Dans ton équipe', e.inTeam],
      ['Battus', e.beaten],
      ['Relâchés', e.released],
    ].map(([label, value]) => el('div', {}, [el('dt', { textContent: label }), el('dd', { textContent: String(value) })])));
    dna.addEventListener('click', () => {
      dnaOpen = true;
      renderBestiary();
      $('bestiary-detail').querySelector('.dna-back').focus();
    });
    $('bestiary-detail').className = 'bestiary-detail';
    $('bestiary-detail').replaceChildren(
      el('img', { className: 'portrait', src: portraitUrl(speciesIndex(e.species)), alt: info.name }),
      el('div', {}, [
        el('h2', { textContent: info.name }),
        el('p', { className: 'kind', textContent: info.kind }),
        el('div', { className: 'badges' }, [familyBadge(e.family), ...elementBadges(e.elements)]),
        el('p', { className: 'description', textContent: info.description, style: 'margin-top:10px' }),
        // On average (males and females, a normal build).
        el('p', {
          className: 'facts measures',
          textContent: `Taille : ${sizeText(Math.round(e.sizeMm))} · poids : ${weightText(Math.round(e.weightG))} · ` +
            `vitesse : ${decimal(e.speed, 1)} m/s`,
        }),
        el('div', { className: 'sheet-buttons' }, [dna, habitat, ...cryButton(e.species)]),
      ]),
      stats,
      el('section', { className: 'record-box' }, [el('h3', { textContent: 'Ton carnet' }), record]),
    );
  }

  // Where a species lives: the playable area's map, its zones blinking, in place of its card.
  let habitatLoop = 0;
  const habitatView = { cx: 0, cz: -320, scale: 0 };  // 0: the whole playable area (its middle: further north)
  function renderHabitat(e) {
    const info = LORE.species[e.species];
    const back = el('button', { type: 'button', className: 'dna-back', textContent: '← Fiche' });
    back.addEventListener('click', () => {
      habitatOpen = false;
      renderBestiary();
    });
    const view = el('canvas', { className: 'habitat-map', id: 'habitat-map' });
    view.setAttribute('aria-label', `Carte : où vit ${info.name}`);
    const key = (glyph, color, text) => el('div', {}, [el('i', { textContent: glyph, style: `color:${color}` }), text]);
    $('bestiary-detail').className = 'bestiary-detail habitat';
    $('bestiary-detail').replaceChildren(
      el('header', { className: 'dna-header' }, [back, el('h2', { textContent: `Habitat de ${info.name}` })]),
      el('div', { className: 'habitat-frame' }, [
        view,
        el('div', { className: 'map-legend habitat-legend', role: 'note' }, [
          key('➤', '#f2c94c', 'Toi'), key('●', '#7fe0ff', 'Téléporteur'), key('▦', '#ffd646', 'Habitat'),
        ]),
      ]),
    );
    // The whole playable area at first (zoom and move it as the map); its
    // zones light up and fade, again and again. The view is kept from one
    // species to the next.
    // (Further north than wide: x in [-world, world], z in [-worldNorth, world].)
    const fit = () => {
      const d = mapBase();
      return Math.min(view.clientWidth / (2 * d.world), view.clientHeight / (d.world + d.worldNorth)) * 1.02;
    };
    const look = () => {
      const d = mapBase();
      const min = fit();
      habitatView.scale = Math.min(MAX_SCALE, Math.max(min, habitatView.scale || min));
      habitatView.cx = Math.min(d.world, Math.max(-d.world, habitatView.cx || 0));
      habitatView.cz = Math.min(d.world, Math.max(-d.worldNorth, habitatView.cz));
      return habitatView;
    };
    panZoom(view, (factor, px, py) => {
      const v = look();
      const wx = v.cx + (px - view.clientWidth / 2) / v.scale, wz = v.cz + (py - view.clientHeight / 2) / v.scale;
      v.scale = Math.min(MAX_SCALE, Math.max(fit(), v.scale * factor));
      v.cx = wx - (px - view.clientWidth / 2) / v.scale;
      v.cz = wz - (py - view.clientHeight / 2) / v.scale;
    }, (dx, dy) => {
      const v = look();
      v.cx -= dx / v.scale;
      v.cz -= dy / v.scale;
    });
    cancelAnimationFrame(habitatLoop);
    const frame = (now) => {
      if (!view.isConnected || !isOpen()) return;
      const v = look();
      drawMap(view, { cx: v.cx, cz: v.cz, scale: v.scale, player: mapPlayer() },
        { habitat: e, habitatAlpha: 0.2 + 0.6 * (0.5 + 0.5 * Math.sin(now / 280)) });
      habitatLoop = requestAnimationFrame(frame);
    };
    habitatLoop = requestAnimationFrame(frame);
  }

  const STATUS_TEXT = {
    unknown: 'Inconnue', seen: 'Aperçue, pas encore capturée', captured: 'Capturée',
  };

  // Every variant of a species: one row per gene (the sex first), then the rare forms. Unknown
  // ones are a "?", seen ones are greyed, captured ones are shown as they are.
  function renderVariants(e) {
    const info = LORE.species[e.species];
    const genes = info.genes || {};
    const groups = [];
    for (const v of e.variants) {
      const key = v.kind === 'chromatic' || v.kind === 'elemental' ? 'rare' : v.kind === 'build' ? 'build'
        : v.id.split('.')[0];
      let group = groups.find((g) => g.key === key);
      const title = key === 'rare' ? 'Formes rares' : key === 'build' ? 'Gabarits' : genes[key] || key;
      if (!group) groups.push(group = { key, title, items: [] });
      group.items.push(v);
    }
    if (chosenVariant && !e.variants.some((v) => v.id === chosenVariant)) chosenVariant = null;

    const back = el('button', { type: 'button', className: 'dna-back', textContent: '← Fiche' });
    back.addEventListener('click', () => {
      dnaOpen = false;
      renderBestiary();
    });
    const infoBox = el('div', { className: 'variant-info', id: 'variant-info' });
    infoBox.setAttribute('aria-live', 'polite');
    const cells = [];
    const showInfo = (v) => {
      chosenVariant = v.id;
      for (const c of cells) c.setAttribute('aria-pressed', String(c.dataset.id === v.id));
      renderVariantInfo(e, v, infoBox);
    };
    const rows = groups.map((group) => el('section', { className: 'dna-group' }, [
      el('h3', { textContent: group.title }),
      el('div', { className: 'dna-grid' }, group.items.map((v) => {
        const text = variantText(e.species, v.id);
        const unknown = v.status === 'unknown';
        const cell = el('button', {
          type: 'button', className: 'variant-cell',
          title: unknown ? 'Variante inconnue' : `${text.name} · ${STATUS_TEXT[v.status]}`,
        }, unknown ? [el('span', { className: 'unknown', textContent: '?' }), el('small', { textContent: '???' })]
          : [el('img', { src: portraitUrl(speciesIndex(e.species), 96, v.look), alt: '' }),
             el('small', {}, v.kind === 'sex' ? [sexSign(v.id === 'sex.female'), ` ${text.name}`] : [text.name])]);
        cell.dataset.id = v.id;
        cell.dataset.status = v.status;
        cell.dataset.rarity = v.rarity;
        cell.setAttribute('aria-pressed', 'false');
        cell.setAttribute('aria-label', unknown ? 'Variante inconnue' : `${text.name}, ${STATUS_TEXT[v.status]}`);
        cell.addEventListener('click', () => showInfo(v));
        cells.push(cell);
        return cell;
      })),
    ]));

    $('bestiary-detail').className = 'bestiary-detail dna';
    $('bestiary-detail').replaceChildren(
      el('header', { className: 'dna-header' }, [
        back,
        el('div', {}, [
          el('h2', {}, [dnaIcon(), ` Variantes de ${info.name}`]),
          el('p', {
            className: 'facts',
            textContent: `${captured(e)} capturées sur ${e.variants.length} · ${known(e)} aperçues`,
          }),
        ]),
      ]),
      // The grid on the left, the chosen variant on the right: always in view.
      el('div', { className: 'dna-body' }, [el('div', { className: 'dna-groups' }, rows), infoBox]),
    );
    const first = e.variants.find((v) => v.id === chosenVariant);
    if (first) showInfo(first);
    else infoBox.replaceChildren(el('p', {
      className: 'facts', textContent: 'Touche une variante pour la voir. Les variantes se découvrent en ' +
        's’approchant à moins de 20 m d’un monstre qui la porte.',
    }));
  }

  // What a variant changes: the bonuses of its gifts and stats.
  function variantEffects(v) {
    const items = (v.mods || []).map((x) => el('li', { className: x.value > 0 ? 'up' : 'down',
      textContent: `${signed(x.value)} ${LORE.stats[x.stat] || x.stat}` }));
    if (!items.length) return '';  // nothing to say
    return el('div', { className: 'variant-effects' }, [el('span', { className: 'facts', textContent: 'Effets :' }),
      el('ul', {}, items)]);
  }

  function renderVariantInfo(e, v, box) {
    const info = LORE.species[e.species];
    if (v.status === 'unknown') {
      box.replaceChildren(el('p', {
        className: 'facts',
        textContent: `Variante encore inconnue. Approche-toi à moins de 20 m d’un ${info.name} qui la porte ` +
          'pour la découvrir.',
      }));
      return;
    }
    const text = variantText(e.species, v.id);
    const rarity = LORE.rarities[v.rarity] || { name: v.rarity, color: '#ccc' };
    const where = v.zones.map((z) => LORE.zones[z] || z).join(', ');
    const geneName = v.kind === 'chromatic' ? 'Forme chromatique' : v.kind === 'elemental' ? 'Forme élémentaire'
      : v.kind === 'build' ? 'Gabarit' : (info.genes || {})[v.id.split('.')[0]] || '';
    box.replaceChildren(
      el('img', {
        className: `portrait ${v.status}`, src: portraitUrl(speciesIndex(e.species), 160, v.look), alt: text.name,
      }),
      el('div', {}, [
        el('h3', {}, v.kind === 'sex' ? [sexSign(v.id === 'sex.female'), ` ${text.name}`] : [text.name]),
        el('p', { className: 'kind', textContent: geneName }),
        el('div', { className: 'badges' }, [
          el('span', { className: 'badge', textContent: rarity.name, style: `background:${rarity.color}` }),
          ...(v.kind === 'elemental' ? elementBadges(v.elements) : []),
        ]),
        el('p', { className: 'description', textContent: text.description }),
        variantEffects(v),
        v.kind === 'build' && v.id === 'omega'
          ? el('p', { className: 'facts', textContent: 'Où la trouver : pas dans la nature, lors d’événements exceptionnels' })
          : where ? el('p', { className: 'facts', textContent: `Où la trouver : ${where}` }) : '',
        el('p', { className: `facts status ${v.status}`, textContent: STATUS_TEXT[v.status] }),
      ]),
    );
  }

  // A map's gestures (the map, a species' habitat): the wheel zooms, dragging
  // with one finger (or the mouse) moves it, two fingers zoom.
  // zoom(factor, x, y): around that point of the canvas; pan(dx, dy): in pixels.
  // Returns whether the last touch moved or zoomed it (then it is no click).
  function panZoom(target, zoom, pan) {
    target.addEventListener('wheel', (event) => {
      event.preventDefault();
      const rect = target.getBoundingClientRect();
      zoom(Math.exp(-event.deltaY * 0.0015), event.clientX - rect.left, event.clientY - rect.top);
    }, { passive: false });
    const pointers = new Map();
    let pinch = 0, moved = false;
    target.addEventListener('pointerdown', (event) => {
      target.setPointerCapture(event.pointerId);
      pointers.set(event.pointerId, { x: event.clientX, y: event.clientY });
      moved = false;
      if (pointers.size === 2) {
        const [a, b] = [...pointers.values()];
        pinch = Math.hypot(a.x - b.x, a.y - b.y);
      }
    });
    target.addEventListener('pointermove', (event) => {
      const last = pointers.get(event.pointerId);
      if (!last) return;
      const now = { x: event.clientX, y: event.clientY };
      pointers.set(event.pointerId, now);
      if (pointers.size === 1) {
        const dx = now.x - last.x, dy = now.y - last.y;
        moved = moved || Math.abs(dx) + Math.abs(dy) > 2;
        pan(dx, dy);
      } else if (pointers.size === 2) {
        const [a, b] = [...pointers.values()];
        const d = Math.hypot(a.x - b.x, a.y - b.y);
        const rect = target.getBoundingClientRect();
        if (pinch > 0) zoom(d / pinch, (a.x + b.x) / 2 - rect.left, (a.y + b.y) / 2 - rect.top);
        pinch = d;
        moved = true;
      }
    });
    const release = (event) => {
      pointers.delete(event.pointerId);
      if (pointers.size < 2) pinch = 0;
    };
    target.addEventListener('pointerup', release);
    target.addEventListener('pointercancel', release);
    return () => moved;
  }

  // ---- The map: one map, from the playable area up close to the whole
  // continent (wheel, pinch, + and − buttons; drag to move it). Only the
  // land and the places: no creatures. ----
  const canvas = $('map-canvas');
  const PX_PER_M = 1;           // resolution of the drawn playable area
  const MAX_SCALE = 8;          // CSS pixels per meter, zoomed in
  const LOCAL_SCALE = 5;        // when the map opens
  let base = null;              // the playable area: { key, image, R }
  let land = null;              // the continent: { seed key, image, data }
  let habitatImage = null;      // a species' zones (the bestiary's habitat): { key, image }
  let scale = prefs.get('mapScale', LOCAL_SCALE);
  let center = null;            // where the map looks (null: on the player)

  const minScale = () => {
    if (!land) return 0.5;
    const d = land.data;
    const w = (d.pointsX - 1) * d.cell, h = (d.pointsZ - 1) * d.cell;
    return Math.min(canvas.clientWidth / w, canvas.clientHeight / h) * 0.95;
  };
  const clampScale = (s) => Math.min(MAX_SCALE, Math.max(minScale(), s));

  function zoomAt(factor, px, py) {
    // Keeps the point under (px, py) in place.
    const view = currentView();
    const wx = view.cx + (px - canvas.clientWidth / 2) / view.scale;
    const wz = view.cz + (py - canvas.clientHeight / 2) / view.scale;
    scale = clampScale(view.scale * factor);
    center = { x: wx - (px - canvas.clientWidth / 2) / scale, z: wz - (py - canvas.clientHeight / 2) / scale };
    prefs.set('mapScale', scale);
    renderMap();
  }
  // Not too far: the middle of the map stays over the land (the continent, or the playable area).
  function clampCenter(c) {
    if (!c) return c;
    const d = land && land.data;
    const lo = d ? { x: d.minX, z: d.minZ } : { x: -600, z: -600 };
    const hi = d ? { x: d.minX + (d.pointsX - 1) * d.cell, z: d.minZ + (d.pointsZ - 1) * d.cell } : { x: 600, z: 600 };
    return { x: Math.min(hi.x, Math.max(lo.x, c.x)), z: Math.min(hi.z, Math.max(lo.z, c.z)) };
  }
  // Where the player is on the map: inside a cave, a house or the tower, at
  // the door it went in by (the map shows the world outside).
  function mapPlayer() {
    const p = game.state().player;
    return p.mapX === undefined ? p
      : { ...p, x: p.mapX, z: p.mapZ, facingX: p.mapFacingX, facingZ: p.mapFacingZ };
  }
  function currentView() {
    const p = mapPlayer();
    center = clampCenter(center);
    return { cx: center ? center.x : p.x, cz: center ? center.z : p.z, scale: clampScale(scale), player: p };
  }

  $('map-zoom-in').addEventListener('click', () => zoomAt(1.6, canvas.clientWidth / 2, canvas.clientHeight / 2));
  $('map-zoom-out').addEventListener('click', () => zoomAt(1 / 1.6, canvas.clientWidth / 2, canvas.clientHeight / 2));
  $('map-recenter').addEventListener('click', () => {
    center = null;
    renderMap();
  });
  const mapMoved = panZoom(canvas, zoomAt, (dx, dy) => {
    const view = currentView();
    center = { x: view.cx - dx / view.scale, z: view.cz - dy / view.scale };
    renderMap();
  });

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
    castleWall: '#9d978e', castleTower: '#a8a298', castleGate: '#8f897f', castleKeep: '#b0aaa0',
    caveMouth: '#2a2630', crystal: '#c79bff',
    cart: '#8a6a44', bridge: '#9a7a50', farmhouse: '#b8553a', barn: '#a33a2c', fruitTree: 'rgba(60, 120, 50, 0.9)',
    crops: 'rgba(214, 186, 96, 0.85)', haystack: '#d8b860', windmill: '#a8a298', cabin: '#7a5534', tent: '#d8cca8',
    campfire: '#e07a30', woodpile: '#7a5534', hollowTree: '#4e3a26', burrow: '#7a5a3a', lighthouse: '#e8e4dc',
    menhir: '#a8a39a', meteorite: '#3a2f46', waterMill: '#c9b89a', hiddenCave: '#2a2630',
    chalet: '#8a6a50', mineEntrance: '#2a2630', minecart: '#7a5534', larch: 'rgba(92, 138, 62, 0.9)',
    stoneWall: '#9a968e', cairn: '#a8a39a', hotSpring: '#7fd8d8', mineProp: '#7a5534',
  };

  // The season (settings menu, ui.js): the map's colors follow it, as the
  // game's do (the shaders' seasonal(): spring fresher, autumn ochre, snow
  // and ice in winter).
  let season = Number(prefs.get('season', 1)) || 0;
  if (!(season >= 0 && season <= 3)) season = 1;
  window.addEventListener('monstres-season', (event) => {
    if (season === event.detail.season) return;
    season = event.detail.season;
    if (isOpen() && current === 'map') {
      renderMap();
    } else if (game && base) {  // its pictures drawn again ahead, in the new season's colors
      base = null;
      warm = null;
      clearTimeout(warmTimer);
      warmTimer = setTimeout(warmUp, 1000);
    }
  });
  const SEASON_OBJECTS = [
    { tree: 'rgba(66, 132, 56, 0.85)', bush: 'rgba(84, 150, 66, 0.9)', fruitTree: 'rgba(238, 196, 212, 0.95)' },
    {},
    { tree: 'rgba(198, 112, 40, 0.88)', bush: 'rgba(180, 116, 52, 0.9)', fruitTree: 'rgba(196, 128, 44, 0.9)',
      hollowTree: '#8a5a2a', larch: 'rgba(222, 170, 50, 0.9)' },
    { tree: 'rgba(112, 92, 72, 0.8)', bush: 'rgba(150, 132, 104, 0.9)', fruitTree: 'rgba(112, 92, 72, 0.8)',
      pine: 'rgba(46, 84, 66, 0.92)', crops: 'rgba(236, 238, 244, 0.9)', haystack: '#e8e8ee' },
  ];
  const objectColor = (kind) => SEASON_OBJECTS[season][kind] || OBJECT_COLORS[kind] || '#777';
  const ICE = [196, 220, 234];
  // A pixel of the map in its season: `water` 0 land, 1 the sea (as it is),
  // 2 a lake or a river (frozen in winter); `snowy` 0..1 how much snow lies
  // there in winter (none on cliffs). Changes `px` ({ r, g, b }).
  function inSeason(px, water, snowy) {
    if (season === 1 || water === 1) return;
    if (water === 2) {
      if (season === 3) { px.r = ICE[0]; px.g = ICE[1]; px.b = ICE[2]; }
      return;
    }
    const { r, g, b } = px;
    const green = Math.min(Math.max((g - Math.max(r, b)) / 255 * 5, 0), 1);
    const l = 0.3 * r + 0.59 * g + 0.11 * b;
    let tr = r, tg = g, tb = b;
    if (season === 0) { tr = r * 0.95 + 5; tg = g * 1.1 + 8; tb = b * 0.82; }
    if (season === 2) { tr = 0.70 * l * 1.7; tg = 0.58 * l * 1.7; tb = 0.30 * l * 1.7; }
    if (season === 3) { tr = 0.78 * l * 1.45; tg = 0.72 * l * 1.45; tb = 0.52 * l * 1.45; }
    const k = season === 2 ? 0.6 * green : green;
    px.r = r + (tr - r) * k; px.g = g + (tg - g) * k; px.b = b + (tb - b) * k;
    if (season === 3) {
      const w = 0.9 * snowy;
      px.r += (236 - px.r) * w; px.g += (240 - px.g) * w; px.b += (248 - px.b) * w;
    }
  }

  function heightAt(data, x, z) {
    const nx = data.pointsX, nz = data.pointsZ;
    if (!nx || !nz) return 0;
    const cell = data.cell || 1;
    const fx = Math.min(Math.max((x - data.minX) / cell, 0), nx - 1.001);
    const fz = Math.min(Math.max((z - data.minZ) / cell, 0), nz - 1.001);
    const i = Math.floor(fx), j = Math.floor(fz), tx = fx - i, tz = fz - j;
    const h = (a, b) => data.heights[a * nz + b];
    const top = h(i, j) + (h(i + 1, j) - h(i, j)) * tx;
    const bottom = h(i, j + 1) + (h(i + 1, j + 1) - h(i, j + 1)) * tx;
    return top + (bottom - top) * tz;
  }

  const smooth = (e0, e1, v) => {
    const t = Math.min(Math.max((v - e0) / (e1 - e0), 0), 1);
    return t * t * (3 - 2 * t);
  };

  // The continent, drawn once per world: one pixel per cell of its grid,
  // shaded relief, sea, beaches, snow, forest, marsh, desert, lava.
  function* buildLand(d) {
    const image = el('canvas', { width: d.pointsX, height: d.pointsZ });
    const ctx = image.getContext('2d');
    const pixels = ctx.createImageData(d.pointsX, d.pointsZ);
    const H = (i, j) => d.heights[Math.min(d.pointsX - 1, Math.max(0, i)) * d.pointsZ + Math.min(d.pointsZ - 1, Math.max(0, j))];
    const inside = (m, x, z, kx = 1, kz = 1) => {
      const dx = (x - m.x) / (m.r * kx), dz = (z - m.z) / (m.r * kz);
      return dx * dx + dz * dz < 1;
    };
    const mark = (id) => d.landmarks.find((m) => m.region === id);
    const forest = mark('forest'), marsh = mark('marsh'), capital = mark('capital');
    for (let j = 0; j < d.pointsZ; ++j) {
      for (let i = 0; i < d.pointsX; ++i) {
        const x = d.minX + i * d.cell, z = d.minZ + j * d.cell;
        const h = H(i, j);
        let r, g, b;
        let water = 0;
        if (h < d.seaLevel) {  // the sea: lighter in the shallows
          const k = Math.min(1, (d.seaLevel - h) / 25);
          r = 70 - 40 * k; g = 150 - 60 * k; b = 190 - 40 * k;
          water = 1;
        } else {
          const t = Math.min(Math.max((h + 2) / 60, 0), 1);
          r = 92 + 50 * t; g = 140 + 30 * t; b = 70 + 30 * t;
          const blend = (k, cr, cg, cb) => { r += (cr - r) * k; g += (cg - g) * k; b += (cb - b) * k; };
          // Beaches (not around the playable area: its ground comes down to 0 there).
          if (h < d.seaLevel + 2.5 && Math.max(Math.abs(x), Math.abs(z)) > 400) blend(1, 214, 196, 140);
          if (forest && inside(forest, x, z, 1.3, 0.95)) { r *= 0.62; g *= 0.78; b *= 0.62; }
          if (marsh && inside(marsh, x, z, 1.35, 0.9)) blend(1, 104, 112, 74);
          if (capital && inside(capital, x, z)) blend(1, 178, 168, 146);
          blend(smooth(1000, 1250, z), 220, 186, 128);  // the desert
          blend(smooth(120, 210, h), 140, 136, 134);    // bare rock up high
          blend(smooth(d.snowLine, d.snowLine + 30, h), 238, 242, 248);
          const ash = smooth(2650, 2950, x) * 0.4;      // the volcanic peninsula
          r *= 1 - ash; g *= 1 - ash * 1.1; b *= 1 - ash * 1.1;
        }
        for (const p of d.ponds) {
          const dx = (x - p.x) / p.rx, dz = (z - p.z) / p.rz;
          if (dx * dx + dz * dz < 1 && h < p.level) { r = 72; g = 140; b = 170; water = 2; }
        }
        const lava = Math.hypot(x - d.crater.x, z - d.crater.z) < d.crater.r * 0.8;
        if (!lava) {
          const steep = Math.abs(H(i + 1, j) - H(i - 1, j)) + Math.abs(H(i, j + 1) - H(i, j - 1));
          const px = { r, g, b };
          inSeason(px, water, (1 - smooth(1000, 1250, z) * 0.8) * (1 - smooth(0.6, 1.2, steep / (2 * d.cell))) *
                   (h < d.seaLevel + 1 ? 0.2 : 1));
          ({ r, g, b } = px);
        }
        if (lava) { r = 240; g = 110; b = 40; }
        // Light from the north-west, shading the slopes: soft (over 4 cells,
        // the little bumps smoothed away), the relief still readable.
        const shade = 1 + 0.009 * ((H(i - 2, j) - H(i + 2, j)) + (H(i, j - 2) - H(i, j + 2)));
        const k = Math.min(Math.max(shade, 0.75), 1.2);
        const o = (j * d.pointsX + i) * 4;
        pixels.data[o] = r * k; pixels.data[o + 1] = g * k; pixels.data[o + 2] = b * k; pixels.data[o + 3] = 255;
      }
      if (j % 16 === 15) yield;
    }
    ctx.putImageData(pixels, 0, 0);
    return image;
  }

  // The continent's own ground at (x, z) (no hills): where the sea is.
  function landAt(c, x, z) {
    if (!c) return 0;
    const fx = Math.min(Math.max((x - c.minX) / c.cell, 0), c.pointsX - 1.001);
    const fz = Math.min(Math.max((z - c.minZ) / c.cell, 0), c.pointsZ - 1.001);
    const i = Math.floor(fx), j = Math.floor(fz), tx = fx - i, tz = fz - j;
    const h = (a, b) => c.heights[a * c.pointsZ + b];
    const top = h(i, j) + (h(i + 1, j) - h(i, j)) * tx, bottom = h(i, j + 1) + (h(i + 1, j + 1) - h(i, j + 1)) * tx;
    return top + (bottom - top) * tz;
  }

  // Distance to a place's center in its radii, stretched by its outline
  // (village.hpp, outline_distance): 1 on its edge.
  function outlineDistance(data, center, rx, rz, phases, x, z) {
    const dx = (x - center.x) / rx, dz = (z - center.z) / rz;
    const r = Math.hypot(dx, dz);
    if (r < 1e-6) return 0;
    const a = Math.atan2(dz, dx);
    let inward = 0;
    for (let k = 0; k < 4; ++k) inward += data.outlineDepths[k] * 0.5 * (1 + Math.sin(data.outlineWaves[k] * a + phases[k]));
    return r / (1 - inward);
  }

  // The playable area, drawn once per world: terrain, path and objects.
  // (A rectangle: further north than wide.)
  function* buildBase(data, continent) {
    const x0 = -(data.world + 7), z0 = -(data.worldNorth + 7);
    const sizeX = Math.round(2 * (data.world + 7) * PX_PER_M), sizeZ = Math.round((data.world + data.worldNorth + 14) * PX_PER_M);
    const image = el('canvas', { width: sizeX, height: sizeZ });
    const ctx = image.getContext('2d');
    const pixels = ctx.createImageData(sizeX, sizeZ);
    const lake = data.lake, village = data.village, ruins = data.ruins;
    const forest = data.forest, cave = data.cave, castle = data.castle, north = data.north;
    // The ground's height at every pixel, looked up once (the slopes and the
    // shading below read their neighbors here: 1 px per meter).
    const heights = new Float32Array(sizeX * sizeZ);
    for (let py = 0; py < sizeZ; ++py) {
      for (let px = 0; px < sizeX; ++px) heights[py * sizeX + px] = heightAt(data, x0 + (px + 0.5) / PX_PER_M, z0 + (py + 0.5) / PX_PER_M);
      if (py % 64 === 63) yield;
    }
    const H = (px, py) => heights[Math.min(sizeZ - 1, Math.max(0, py)) * sizeX + Math.min(sizeX - 1, Math.max(0, px))];
    const near = (c, rx, rz, x, z) => c && Math.abs(x - c.x) < rx && Math.abs(z - c.z) < rz;  // its outline's box
    const step = Math.max(1, Math.round(PX_PER_M)), far = Math.max(1, Math.round(3 * PX_PER_M));
    for (let py = 0; py < sizeZ; ++py) {
      for (let px = 0; px < sizeX; ++px) {
        const x = x0 + (px + 0.5) / PX_PER_M, z = z0 + (py + 0.5) / PX_PER_M;
        const h = heights[py * sizeX + px];
        const t = Math.min(Math.max((h + 4) / 12, 0), 1);
        let r = 78 + 70 * t, g = 128 + 46 * t, b = 62 + 30 * t;
        let water = 0;
        if (near(forest, forest && forest.rx, forest && forest.rz, x, z) &&
            outlineDistance(data, forest, forest.rx, forest.rz, forest.outline, x, z) < 1 &&
            outlineDistance(data, forest, forest.clearing, forest.clearing, forest.clearingOutline, x, z) >= 1) {
          r *= 0.8; g *= 0.88; b *= 0.8;  // darker under the trees (not in the clearing)
        }
        // Cliffs (the terrace's edge, the cave's spur): grey rock where the ground is steep.
        const steep = Math.hypot(H(px + step, py) - H(px - step, py), H(px, py + step) - H(px, py - step)) * PX_PER_M / (2 * step);
        const rock = Math.min(1, Math.max(0, (steep - 0.7) / 0.6));
        if (steep > 0.7) { const k = rock; r += (140 - r) * k; g += (136 - g) * k; b += (132 - b) * k; }
        if (village && Math.hypot(x - village.x, z - village.z) < 7) { r = 168; g = 142; b = 104; }
        if (ruins && Math.max(Math.abs(x - ruins.x), Math.abs(z - ruins.z)) < 8.5) { r = 150; g = 146; b = 136; }
        if (castle && Math.max(Math.abs(x - castle.x), Math.abs(z - castle.z)) < castle.r) { r = 156; g = 150; b = 140; }
        // The sea where it comes in, and its beaches (not the hollows of the hills).
        const around = Math.abs(x) > 150 || Math.abs(z) > 150 ? landAt(continent, x, z) : 0;
        if (h < data.seaLevel + 2.5 && around < data.seaLevel + 1) { r = 214; g = 196; b = 140; }
        const shore = h < data.seaLevel + 2.5 && around < data.seaLevel + 1;
        if (h < data.seaLevel && around < data.seaLevel) { const k = Math.min(1, (data.seaLevel - h) / 12); r = 70 - 40 * k; g = 150 - 60 * k; b = 190 - 40 * k; water = 1; }
        if (near(lake, lake && lake.rx * 1.05, lake && lake.rz * 1.05, x, z)) {
          const d = outlineDistance(data, lake, lake.rx, lake.rz, lake.outline, x, z);
          if (d < 1.04 && h > data.waterLevel - 0.05) { r = 214; g = 196; b = 140; }  // sandy shore
          if (d < 1 && h < data.waterLevel) { const k = Math.min(1, (data.waterLevel - h) / 1.2); r = 80 - 30 * k; g = 150 - 40 * k; b = 196 - 20 * k; water = 2; }
        }
        // The north: snow on the mountains' slopes all year long, the frozen lake's ice.
        if (north && z < north.frost + 120) {
          const above = h - north.floor - north.rise * Math.max(0, north.frost - z);
          const snow = smooth(north.frost + 120, north.frost - 120, z) * smooth(4, 30, above) * (1 - 0.6 * rock);
          r += (236 - r) * snow; g += (240 - g) * snow; b += (248 - b) * snow;
          if (near(north.lake ? { x: north.lake[0], z: north.lake[1] } : null, north.lakeRx * 1.05, north.lakeRz * 1.05, x, z) &&
              outlineDistance(data, { x: north.lake[0], z: north.lake[1] }, north.lakeRx, north.lakeRz, north.lakeOutline, x, z) < 1) {
            r = ICE[0]; g = ICE[1]; b = ICE[2];
          }
        }
        if (season !== 1) {
          const px = { r, g, b };
          inSeason(px, water, (1 - rock) * (shore ? 0.35 : 1));
          ({ r, g, b } = px);
        }
        // Light from the north-west, shading the slopes: soft (over 6 m, the
        // little bumps smoothed away), the relief still readable.
        const shade = 1 + 0.07 * ((H(px - far, py) - H(px + far, py)) + (H(px, py - far) - H(px, py + far)));
        const k = Math.min(Math.max(shade, 0.78), 1.18);
        const o = (py * sizeX + px) * 4;
        pixels.data[o] = r * k; pixels.data[o + 1] = g * k; pixels.data[o + 2] = b * k; pixels.data[o + 3] = 255;
      }
      if (py % 16 === 15) yield;
    }
    ctx.putImageData(pixels, 0, 0);
    const toX = (v) => (v - x0) * PX_PER_M, toZ = (v) => (v - z0) * PX_PER_M;

    // The west's stream, and its road (west.hpp).
    const line = (pts, color, width) => {
      ctx.strokeStyle = color;
      ctx.lineWidth = width * PX_PER_M;
      ctx.lineCap = ctx.lineJoin = 'round';
      ctx.beginPath();
      for (let i = 0; i < pts.length; i += 2) {
        if (i === 0) ctx.moveTo(toX(pts[i]), toZ(pts[i + 1])); else ctx.lineTo(toX(pts[i]), toZ(pts[i + 1]));
      }
      ctx.stroke();
    };
    if (data.west) {
      // The river, as wide as it is at each place.
      const st = data.west.stream, sw = data.west.streamW || [];
      ctx.strokeStyle = season === 3 ? `rgba(${ICE.join(', ')}, 0.95)` : 'rgba(70, 150, 200, 0.95)';  // frozen in winter
      ctx.lineCap = 'round';
      for (let i = 0; i + 3 < st.length; i += 2) {
        ctx.lineWidth = 2 * (sw[i / 2] || 2) * PX_PER_M;
        ctx.beginPath();
        ctx.moveTo(toX(st[i]), toZ(st[i + 1]));
        ctx.lineTo(toX(st[i + 2]), toZ(st[i + 3]));
        ctx.stroke();
      }
      line(data.west.road, 'rgba(176, 140, 96, 0.95)', 2.6);
    }
    // The path.
    ctx.strokeStyle = 'rgba(176, 140, 96, 0.95)';
    ctx.lineWidth = 2.2 * PX_PER_M;
    ctx.lineCap = ctx.lineJoin = 'round';
    ctx.beginPath();
    for (let i = 0; i < data.path.length; i += 2) {
      const X = toX(data.path[i]), Y = toZ(data.path[i + 1]);
      if (i === 0) ctx.moveTo(X, Y); else ctx.lineTo(X, Y);
    }
    ctx.stroke();

    // Objects: trees as round crowns, the rest as their outline.
    const order = (kind) => (kind === 'tree' || kind === 'pine' || kind === 'larch' ? 1 : 0);  // crowns over the rest
    const objects = [...data.objects].sort((a, b) => order(a[0]) - order(b[0]));
    for (const [kind, x, z, hx, hz] of objects) {
      ctx.fillStyle = objectColor(kind);
      if (kind === 'tree' || kind === 'pine' || kind === 'bush' || kind === 'tower' || kind === 'hut' ||
          kind === 'well' || kind === 'rubble' || kind === 'stump' || kind === 'castleTower' || kind === 'crystal' ||
          kind === 'fruitTree' || kind === 'haystack' || kind === 'windmill' || kind === 'hollowTree' ||
          kind === 'lighthouse' || kind === 'menhir' || kind === 'meteorite' || kind === 'campfire' ||
          kind === 'larch' || kind === 'cairn' || kind === 'hotSpring' || kind === 'stoneWall') {
        ctx.beginPath();
        ctx.arc(toX(x), toZ(z), Math.max(kind === 'hotSpring' ? 3.6 : hx, 0.3) * PX_PER_M, 0, Math.PI * 2);
        ctx.fill();
        if (kind === 'tower') {
          ctx.lineWidth = 0.5 * PX_PER_M;
          ctx.strokeStyle = '#4a4650';
          ctx.stroke();
        }
      } else {
        const w = Math.max(hx, 0.25), d = Math.max(hz, 0.25);
        ctx.fillRect(toX(x - w), toZ(z - d), 2 * w * PX_PER_M, 2 * d * PX_PER_M);
      }
    }
    return { image, x0, z0, w: sizeX / PX_PER_M, h: sizeZ / PX_PER_M };
  }

  let teleportTargets = [];  // where to click on the map, in teleport mode
  canvas.addEventListener('click', (event) => {
    if (!teleportMode || mapMoved()) return;
    const rect = canvas.getBoundingClientRect();
    const x = event.clientX - rect.left, y = event.clientY - rect.top;
    const target = teleportTargets.find((t) => Math.hypot(t.x - x, t.y - y) < 18);
    if (!target) return;
    game.teleportTo(target.id);
    close();  // the game goes on: the teleport happens with the next tick
    // The teleporter arrived at is the one selected now (not the one left).
    const there = (game.teleporters().list || []).find((t) => t.id === target.id);
    window.dispatchEvent(new CustomEvent('monstres-object', { detail: { index: there ? there.object : -1 } }));
  });

  // Opening the map: on the player, up close (to choose a teleporter: all the
  // known ones and the player, the farm's and the menhirs' far in the west too).
  let fitPlayable = false;  // the next drawing shows them all
  let mapData = null;  // the world's map data (big: read again only when the map opens)
  function openMap(teleport) {
    mapData = null;
    center = teleport ? { x: 0, z: 0 } : null;
    fitPlayable = teleport;
  }

  // The pictures of the map (the continent's, the playable area's): long to
  // draw (over a million pixels), so drawn ahead, a few rows at a time while
  // the game runs; what is left is finished when the map opens.
  const baseKey = (data) => `${data.path.length}:${data.path[0]}:${data.path[1]}:${data.objects.length}:${season}`;
  let warm = null;  // { key, steps (generator), done: { land, base } once finished }
  function* drawPictures(data, key) {
    const c = game.continent();
    const landImage = c.ready ? yield* buildLand(c) : null;
    const picture = yield* buildBase(data, c.ready ? c : null);
    return { land: c.ready ? { data: c, image: landImage } : null, base: { key, ...picture } };
  }
  function startWarm(data) {
    const key = baseKey(data);
    warm = { key, steps: drawPictures(data, key), done: null };
  }
  function finishWarm() {
    for (;;) {
      const step = warm.steps.next();
      if (step.done) { warm.done = step.value; return; }
    }
  }
  let warmTimer = 0;
  function warmUp() {
    clearTimeout(warmTimer);
    warmTimer = 0;
    if (!game || base) return;
    if (!warm) {
      if (!game.continent().ready) { warmTimer = setTimeout(warmUp, 1000); return; }  // its land first
      startWarm(game.map());
    }
    if (warm.done) return;
    const until = performance.now() + 6;  // a few ms per frame
    while (performance.now() < until) {
      const step = warm.steps.next();
      if (step.done) { warm.done = step.value; return; }
    }
    warmTimer = setTimeout(warmUp, 30);
  }
  whenReady(() => { warmTimer = setTimeout(warmUp, 2000); });

  // The world's map data, read once per opening (big), and the pictures drawn from it.
  function mapBase() {
    if (!mapData) mapData = game.map();
    const data = mapData;
    const key = baseKey(data);
    if (!base || base.key !== key) {
      clearTimeout(warmTimer);
      warmTimer = 0;
      if (!warm || warm.key !== key) startWarm(data);
      if (!warm.done) finishWarm();
      ({ land, base } = warm.done);
      habitatImage = null;
    }
    return data;
  }

  // The map in a canvas, around view { cx, cz, scale (CSS px per meter) }.
  // options: marks (the villagers' icons around the player), teleport (the
  // reachable teleporters, larger), habitat (a bestiary entry: its zones,
  // highlighted with that opacity, 0..1).
  function drawMap(target, view, options = {}) {
    const data = mapBase();
    const p = view.player;
    const dpr = window.devicePixelRatio || 1;
    const cssW = target.clientWidth, cssH = target.clientHeight;
    if (!cssW || !cssH) return [];
    target.width = Math.round(cssW * dpr);
    target.height = Math.round(cssH * dpr);
    const ctx = target.getContext('2d');
    ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
    ctx.fillStyle = 'rgb(30, 90, 150)';  // the open sea (as the deep sea of the continent's image)
    ctx.fillRect(0, 0, cssW, cssH);
    const k = view.scale;
    const X = (x) => cssW / 2 + (x - view.cx) * k;
    const Y = (z) => cssH / 2 + (z - view.cz) * k;
    ctx.imageSmoothingEnabled = true;
    if (land) {
      const d = land.data;
      // Each pixel is centered on a grid point.
      ctx.drawImage(land.image, X(d.minX - d.cell / 2), Y(d.minZ - d.cell / 2), d.pointsX * d.cell * k, d.pointsZ * d.cell * k);
    }
    ctx.drawImage(base.image, X(base.x0), Y(base.z0), base.w * k, base.h * k);

    // Where a species lives: its zones, all alike (the opacity makes them blink).
    if (options.habitat && data.zones) {
      const nx = data.zoneCountX, nz = data.zoneCountZ;
      const key = `${options.habitat.species}:${base.key}`;
      if (!habitatImage || habitatImage.key !== key) {
        const zoneIds = ['meadow', 'village', 'lake', 'ruins', 'forest', 'cave', 'castle', 'coast', 'hills', 'plains',
          'farm', 'woods', 'crater', 'river', 'alpine', 'larches', 'frost', 'frozenLake', 'mine'];  // Zone
        const wanted = new Set(options.habitat.habitat.map((z) => zoneIds.indexOf(z)));
        const image = document.createElement('canvas');
        image.width = nx;
        image.height = nz;
        const g = image.getContext('2d');
        const px = g.createImageData(nx, nz);
        for (let i = 0; i < nx; ++i) {
          for (let j = 0; j < nz; ++j) {
            const zone = data.zones.charCodeAt(i * nz + j) - 97;  // 'a' + Zone: column i (x), row j (z, from the north)
            if (wanted.has(zone)) px.data.set([255, 214, 70, 255], (j * nx + i) * 4);
          }
        }
        g.putImageData(px, 0, 0);
        habitatImage = { key, image };
      }
      const step = data.zoneStep;
      ctx.globalAlpha = options.habitatAlpha ?? 0.6;
      ctx.drawImage(habitatImage.image, X(-data.world - step / 2), Y(-data.worldNorth - step / 2), nx * step * k, nz * step * k);
      ctx.globalAlpha = 1;
    }

    const label = (text, x, z, size, faint = false) => {
      ctx.font = `600 ${size}px system-ui, sans-serif`;
      ctx.textAlign = 'center';
      ctx.textBaseline = 'middle';
      ctx.lineWidth = 3;
      ctx.strokeStyle = 'rgba(20, 18, 36, 0.8)';
      ctx.strokeText(text, X(x), Y(z));
      ctx.fillStyle = faint ? 'rgba(255, 255, 255, 0.82)' : '#fff';
      ctx.fillText(text, X(x), Y(z));
    };
    // Zoomed out: the regions of the continent (not reachable yet: faint).
    if (land && k < 0.9) {
      for (const m of land.data.landmarks) {
        if (m.region === 'start' && k > 0.35) continue;  // its own places are shown
        label(LORE.regions[m.region] || m.region, m.x, m.z, m.r > 600 ? 15 : 12, m.region !== 'start');
      }
    }
    // Zoomed in: the places of the playable area.
    if (k > 0.35) {
      const size = k > 3 ? 15 : 12;
      if (data.village) label(LORE.zones.village, data.village.x, data.village.z - 9, size);
      if (data.lake) label(LORE.zones.lake, data.lake.x, data.lake.z + (k < 3 ? data.lake.rz + 5 : 0), size - 1);
      if (data.ruins) label(LORE.zones.ruins, data.ruins.x, data.ruins.z - 12, size);
      if (data.forest) label(LORE.zones.forest, data.forest.x, data.forest.z, size);
      if (data.cave) label(LORE.zones.cave, data.cave.x, data.cave.z, size);
      if (data.castle) label(LORE.zones.castle, data.castle.x, data.castle.z - data.castle.r - 3, size);
      // The west's places (west.hpp): the big ones zoomed out, all of them up close.
      if (data.west) {
        const w = data.west;
        const big = ['farm', 'woods', 'lighthouse', 'crater', 'menhirs'];
        for (const [id, name] of Object.entries(LORE.westPlaces)) {
          if (!w[id] || (k < 1.2 && !big.includes(id))) continue;
          const below = id === 'farm' ? -w.farmR - 4 : id === 'crater' ? -w.craterR - 6 : id === 'woods' ? 0 : -8;
          label(name, w[id][0], w[id][1] + below, big.includes(id) ? size : size - 1);
        }
      }
      // The north's (north.hpp).
      if (data.north) {
        const n = data.north;
        const big = ['refuge', 'lake', 'col', 'mine'];
        for (const [id, name] of Object.entries(LORE.northPlaces)) {
          if (!n[id] || (k < 1.2 && !big.includes(id))) continue;
          const below = id === 'lake' ? 0 : id === 'mine' ? -12 : -9;
          label(name, n[id][0], n[id][1] + below, big.includes(id) ? size : size - 1);
        }
        if (k < 1.2) {  // the belts' names, a little off the path
          const pathX = (z) => {
            let best = 0, gap = Infinity;
            for (let i = 0; i < data.path.length; i += 2) {
              const g = Math.abs(data.path[i + 1] - z);
              if (g < gap) { gap = g; best = data.path[i]; }
            }
            return best;
          };
          label(LORE.zones.alpine, pathX(-700) - 60, -700, size, true);
          label(LORE.zones.larches, pathX(-950) - 60, -950, size, true);
          label(LORE.zones.frost, pathX(-1150) + 90, -1150, size, true);
        }
      }
    }

    // The teleporters found (the ones reachable from here, in teleport mode: brighter, larger).
    const targets = [];
    for (const t of game.teleporters().list) {
      if (!t.known) continue;
      const reachable = options.teleport && t.reachable;
      const r = reachable ? 11 : k > 1 ? 6 : 4;
      ctx.beginPath();
      ctx.arc(X(t.x), Y(t.z), r, 0, Math.PI * 2);
      ctx.fillStyle = reachable ? 'rgba(127, 224, 255, 0.95)' : 'rgba(127, 224, 255, 0.6)';
      ctx.fill();
      ctx.lineWidth = 2;
      ctx.strokeStyle = '#1b3d52';
      ctx.stroke();
      if (reachable) targets.push({ id: t.id, x: X(t.x), y: Y(t.z) });
    }

    // Around the player: what the villagers have for it (as over their heads).
    if (options.marks && k > 0.5) {
      const ICONS = { newQuest: ['!', '#ffffff'], quest: ['!', '#f2c94c'], heal: ['+', '#6fe07a'], fight: ['×', '#ff6a5c'],
        shop: ['¤', '#ffd34d'], farm: ['⌂', '#9be36a'] };
      ctx.font = '800 12px system-ui, sans-serif';
      ctx.textAlign = 'center';
      ctx.textBaseline = 'middle';
      for (const m of game.marks()) {
        if (Math.hypot(m.x - p.x, m.z - p.z) > 200 && !m.marks.includes('farm')) continue;  // the farm: from afar too
        m.marks.forEach((id, i) => {
          const [glyph, color] = ICONS[id] || ['·', '#fff'];
          const cx = X(m.x) + (i - (m.marks.length - 1) / 2) * 15, cy = Y(m.z) - 12;
          ctx.beginPath();
          ctx.arc(cx, cy, 7, 0, Math.PI * 2);
          ctx.fillStyle = 'rgba(20, 18, 36, 0.85)';
          ctx.fill();
          ctx.lineWidth = 1.5;
          ctx.strokeStyle = color;
          ctx.stroke();
          ctx.fillStyle = color;
          ctx.fillText(glyph, cx, cy + 0.5);
        });
      }
    }

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
    return targets;
  }

  function renderMap() {
    if (!game) return;
    mapBase();
    if (!canvas.clientWidth || !canvas.clientHeight) return;
    if (fitPlayable) {
      const p = mapPlayer();
      const spots = [p, ...game.teleporters().list.filter((t) => t.known)];
      const xs = spots.map((t) => t.x), zs = spots.map((t) => t.z);
      const lo = { x: Math.min(...xs), z: Math.min(...zs) }, hi = { x: Math.max(...xs), z: Math.max(...zs) };
      // Clear of the tools at the top and of the hint at the bottom (pixels), and 20 m around.
      const side = 30, top = 70, bottom = 50, margin = 20;
      scale = Math.min((canvas.clientWidth - 2 * side) / (hi.x - lo.x + 2 * margin),
        (canvas.clientHeight - top - bottom) / (hi.z - lo.z + 2 * margin),
        canvas.clientWidth / 320, canvas.clientHeight / 320);
      center = { x: (lo.x + hi.x) / 2, z: (lo.z + hi.z) / 2 - (top - bottom) / 2 / scale };
      fitPlayable = false;
    }
    const view = currentView();
    const k = view.scale;
    $('map-zoom-in').disabled = k >= MAX_SCALE - 1e-3;
    $('map-zoom-out').disabled = k <= minScale() + 1e-3;
    $('map-scale').textContent = k >= 1 ? `${Math.round(100 / k)} m` : `${(100 / k / 1000).toLocaleString('fr-FR', { maximumFractionDigits: 1 })} km`;
    $('map-hint').hidden = !teleportMode;
    teleportTargets = drawMap(canvas, view, { marks: true, teleport: teleportMode });
  }

  // The legend, shown or hidden (remembered).
  const legend = $('map-legend');
  const legendToggle = $('map-legend-toggle');
  const showLegend = (on) => {
    legend.hidden = !on;
    legendToggle.setAttribute('aria-expanded', String(on));
    prefs.set('mapLegend', on);
  };
  showLegend(prefs.get('mapLegend', false));
  legendToggle.addEventListener('click', () => showLegend(legend.hidden));


  // For tests and other scripts.
  // Another world is played (saves.js): its own map, read again.
  window.addEventListener('monstres-world', () => {
    close();
    mapData = null;
    base = null;
    warm = null;
    clearTimeout(warmTimer);
    warmTimer = setTimeout(warmUp, 2000);
  });
  // teleportTargets: where the teleporters are on the map (teleport mode), for tests.
  window.monstresMenu = { open, close, isOpen, show, teleportTargets: () => teleportTargets.slice() };
})();
