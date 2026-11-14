// The grimoire (button at the top left): character sheet, team, bestiary and maps.
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
    if (!['character', 'team', 'spells', 'quests', 'bestiary', 'map'].includes(tab)) tab = 'character';
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
    else if (tab === 'spells') window.MonstresSpells.renderTab();
    else if (tab === 'quests') renderQuests();
    else if (tab === 'bestiary') renderBestiary();
    else renderMap();
  }

  button.addEventListener('click', () => (isOpen() ? close() : open()));
  window.MonstresMenu = { open, close, isOpen };
  $('menu-close').addEventListener('click', close);
  menu.addEventListener('pointerdown', (event) => {
    if (event.target === menu) close();  // the dark backdrop around the window
  });
  for (const t of tabs) t.addEventListener('click', () => show(t.dataset.tab));

  // Keyboard: Escape closes; C, E, B and M open a tab (not while typing a name).
  // By letter, not by key position: M is not at the same place on AZERTY.
  const SHORTCUTS = { c: 'character', e: 'team', p: 'spells', j: 'quests', b: 'bestiary', m: 'map' };
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

  // ---- Character sheet ----
  const STATS = [
    ['hp', 'Points de vie', 60], ['mana', 'Mana', 60], ['power', 'Puissance magique', 30],
    ['wisdom', 'Sagesse', 30], ['agility', 'Agilité', 30], ['endurance', 'Endurance', 30],
  ];
  const nameInput = $('wizard-name');

  // [label, value, max for the bar, text shown (else the value)].
  function statList(target, entries) {
    target.replaceChildren(...entries.map(([label, value, max, text]) => el('div', { className: 'stat' }, [
      el('dt', { textContent: label }),
      el('dd', { textContent: text === undefined ? String(value) : text }),
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
    // The mana: what is left of its most (the bar shows how full it is).
    statList($('wizard-stats'), STATS.map(([key, label, max]) => (key === 'mana'
      ? [label, w.manaNow, w.mana, `${w.manaNow} / ${w.mana}`] : [label, w[key], max])));
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

  // ---- Team: the monsters of the player, the companion first ----
  let chosenMember = prefs.get('teamSlot', 0);
  const SEX = { true: '♀ Femelle', false: '♂ Mâle' };

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
    const button = el('button', { type: 'button', className: 'button danger', id: 'release', textContent: `Relâcher ${name}…` });
    button.addEventListener('click', () => askRelease(m));
    box.append(button, el('p', { className: 'facts', textContent: 'Il sort du cristal et retourne vivre dans la nature.' }));
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
  // variants allow drawn faintly behind. Hovering (or touching) a gift tells
  // its value, its range, and where its modifiers come from.
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

    const tip = el('div', { className: 'gift-tip', hidden: true });
    tip.setAttribute('role', 'tooltip');
    let pinned = -1;  // touched: it stays until touched again
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
      const [x, y] = point(k, 26);
      // Beside its label, towards the middle; above it for the gifts at the bottom.
      tip.style.transform = `translate(${x > 20 ? '-100%' : x < -20 ? '0' : '-50%'}, ${y > 10 ? 'calc(-100% - 22px)' : '12px'})`;
      tip.style.left = `${50 + (x / 370) * 100}%`;
      tip.style.top = `${50 + (y / 200) * 100}%`;
      tip.hidden = false;
      for (const g of svg.querySelectorAll('.web-hit')) g.classList.toggle('on', Number(g.dataset.gift) === k);
    };
    const hideTip = () => {
      if (pinned >= 0) return;
      tip.hidden = true;
      for (const g of svg.querySelectorAll('.web-hit')) g.classList.remove('on');
    };
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
      group.addEventListener('focus', () => showTip(k));
      group.addEventListener('blur', hideTip);
      group.addEventListener('click', () => {
        pinned = pinned === k ? -1 : k;
        if (pinned >= 0) showTip(k);
        else hideTip();
      });
    });
    box.append(svg, tip);
    return box;
  }

  // The fight stats, with the points some variants add (hover: from where).
  function fightStats(m) {
    const stats = el('dl', { className: 'stats' });
    const rows = [['hp', 'Points de vie', m.maxHp, 80], ['attack', 'Attaque', m.attack, 25],
      ['defense', 'Défense', m.defense, 25], ['speed', 'Vitesse', m.speed, 25], ['magic', 'Magie', m.magic, 25]];
    statList(stats, rows.map(([key, label, value, max]) => {
      const mods = (m.statMods || {})[key] || [];
      const total = mods.reduce((a, x) => a + x.value, 0);
      return [label, value, max, total ? `${value} (${signed(total)})` : undefined];
    }));
    rows.forEach(([key], i) => {
      const mods = (m.statMods || {})[key];
      if (mods) stats.children[i].title = mods.map((x) => `${sourceName(m.species, x.source)} : ${signed(x.value)}`).join('\n');
    });
    return stats;
  }

  function renderMember(m, teamSize) {
    const info = LORE.species[m.species];
    const texts = info.variants || {};
    const stats = fightStats(m);
    const variants = m.variants.filter((id) => !id.startsWith('sex.') && !LORE.builds[id])
      .map((id) => (id.includes('.') ? '' : '✦ ') + (variantText(m.species, id).name || id));
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
            `puissance ${move.power} · recharge ${move.cooldown.toLocaleString('fr-FR', { maximumFractionDigits: 1 })} s`,
        }),
        el('p', { className: 'move-text', textContent: text.description }),
      ]);
    }));
    $('team-detail').className = 'bestiary-detail member';
    $('team-detail').replaceChildren(
      el('img', { className: 'portrait', src: portraitUrl(speciesIndex(m.species), 256, m.look), alt: info.name }),
      el('div', {}, [
        el('h2', { textContent: info.name }),
        el('p', { className: 'kind', textContent: `${info.kind} · ${SEX[m.female]}${m.active ? ' · ton compagnon' : ''}` }),
        el('div', { className: 'badges' }, [familyBadge(m.family), ...elementBadges(m.elements), build]),
        el('p', { className: 'rank', textContent: `Niveau ${m.level}` }),
        el('div', { className: 'xp' }, [
          el('div', { className: 'bar' }, [el('span', { style: `width:${Math.min(100, (100 * m.xp) / m.xpToNext)}%` })]),
          el('span', { textContent: `Expérience : ${m.xp} / ${m.xpToNext}` }),
        ]),
        el('div', { className: 'xp hp' }, [hpBar(m.hp, m.maxHp), el('span', { textContent: `PV : ${m.hp} / ${m.maxHp}` })]),
        variants.length ? el('p', { className: 'facts', textContent: `Variantes : ${variants.join(' · ')}` }) : '',
        el('p', { className: 'facts measures', textContent: `Taille : ${sizeText(m.sizeMm)} · poids : ` +
          `${weightText(m.weightG)} · âge : ${ageText(m.ageDays)}` }),
      ]),
      stats,
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
    const steps = Object.entries(info.steps).map(([id, label]) => el('li', {
      className: q.steps[id] ? 'step done' : 'step', textContent: label,
    }));
    $('quest-detail').className = 'bestiary-detail quest';
    $('quest-detail').replaceChildren(
      el('h2', { textContent: info.title }),
      el('p', { className: 'kind', textContent: `Demandée par ${giver} · ${q.stage === 'done' ? 'terminée' : 'en cours'}` }),
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
        if (chosen !== e.species) chosenVariant = null;
        chosen = e.species;
        prefs.set('bestiarySpecies', chosen);
        renderBestiary();
      });
      return el('li', {}, [item]);
    }));

    const e = entries.find((x) => x.species === chosen);
    if (dnaOpen) renderVariants(e);
    else renderSheet(e);
  }

  function renderSheet(e) {
    const info = LORE.species[e.species];
    const stats = el('dl', { className: 'stats' });
    statList(stats, MONSTER_STATS.map(([key, label, max]) => [label, e[key], max]));
    const count = e.species === 'companion' ? 'Il n’y en a qu’un : le tien.'
      : `Autour de toi : ${e.count} ${e.count > 1 ? 'individus' : 'individu'}.`;
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
        el('p', { className: 'facts', textContent: `Habitat : ${info.habitat}` }),
        el('p', {
          className: 'facts',
          textContent: `Taille : ${e.height.toFixed(1).replace('.', ',')} m · ` +
            `vitesse : ${e.walkSpeed.toFixed(1).replace('.', ',')} à ${e.runSpeed.toFixed(1).replace('.', ',')} m/s`,
        }),
        el('p', { className: 'facts', textContent: count }),
        dna,
      ]),
      stats,
      el('section', { className: 'record-box' }, [el('h3', { textContent: 'Ton carnet' }), record]),
    );
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
             el('small', { textContent: text.name })]);
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

  // What a variant changes: gifts and fight stats; a build, its size and
  // its fight stats in percent too.
  function variantEffects(v) {
    const items = (v.mods || []).map((x) => el('li', { className: x.value > 0 ? 'up' : 'down',
      textContent: `${signed(x.value)} ${LORE.stats[x.stat] || x.stat}` }));
    if (v.fight) {
      const f = v.fight;
      items.push(el('li', { className: f.size > 1 ? 'up' : 'down',
        textContent: `taille ×${f.size.toLocaleString('fr-FR', { maximumFractionDigits: 2 })}` }));
      for (const key of ['hp', 'attack', 'defense', 'speed', 'magic']) {
        if (f[key] === 100) continue;
        items.push(el('li', { className: f[key] > 100 ? 'up' : 'down', textContent: `${signed(f[key] - 100)} % ${LORE.stats[key]}` }));
      }
    }
    if (!items.length) return el('p', { className: 'facts variant-effects none', textContent: 'Effets : aucun sur ses aptitudes.' });
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
    const where = v.zones.length ? v.zones.map((z) => LORE.zones[z] || z).join(', ') : 'partout';
    const geneName = v.kind === 'chromatic' ? 'Forme chromatique' : v.kind === 'elemental' ? 'Forme élémentaire'
      : v.kind === 'build' ? 'Gabarit' : (info.genes || {})[v.id.split('.')[0]] || '';
    box.replaceChildren(
      el('img', {
        className: `portrait ${v.status}`, src: portraitUrl(speciesIndex(e.species), 160, v.look), alt: text.name,
      }),
      el('div', {}, [
        el('h3', { textContent: text.name }),
        el('p', { className: 'kind', textContent: geneName }),
        el('div', { className: 'badges' }, [
          el('span', { className: 'badge', textContent: rarity.name, style: `background:${rarity.color}` }),
          ...(v.kind === 'elemental' ? elementBadges(v.elements) : []),
        ]),
        el('p', { className: 'description', textContent: text.description }),
        variantEffects(v),
        el('p', { className: 'facts', textContent: v.kind === 'build' && v.id === 'omega'
          ? 'Où la trouver : pas dans la nature, lors d’événements exceptionnels' : `Où la trouver : ${where}` }),
        el('p', { className: `facts status ${v.status}`, textContent: STATUS_TEXT[v.status] }),
      ]),
    );
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
  function currentView() {
    const p = game.state().player;
    return { cx: center ? center.x : p.x, cz: center ? center.z : p.z, scale: clampScale(scale), player: p };
  }

  $('map-zoom-in').addEventListener('click', () => zoomAt(1.6, canvas.clientWidth / 2, canvas.clientHeight / 2));
  $('map-zoom-out').addEventListener('click', () => zoomAt(1 / 1.6, canvas.clientWidth / 2, canvas.clientHeight / 2));
  $('map-recenter').addEventListener('click', () => {
    center = null;
    renderMap();
  });
  canvas.addEventListener('wheel', (event) => {
    event.preventDefault();
    const rect = canvas.getBoundingClientRect();
    zoomAt(Math.exp(-event.deltaY * 0.0015), event.clientX - rect.left, event.clientY - rect.top);
  }, { passive: false });
  // Dragging with one finger (or the mouse) moves the map; two fingers zoom.
  const pointers = new Map();
  let pinch = 0, moved = false;
  canvas.addEventListener('pointerdown', (event) => {
    canvas.setPointerCapture(event.pointerId);
    pointers.set(event.pointerId, { x: event.clientX, y: event.clientY });
    moved = false;
    if (pointers.size === 2) {
      const [a, b] = [...pointers.values()];
      pinch = Math.hypot(a.x - b.x, a.y - b.y);
    }
  });
  canvas.addEventListener('pointermove', (event) => {
    const last = pointers.get(event.pointerId);
    if (!last) return;
    const now = { x: event.clientX, y: event.clientY };
    pointers.set(event.pointerId, now);
    if (pointers.size === 1) {
      const dx = now.x - last.x, dy = now.y - last.y;
      if (Math.abs(dx) + Math.abs(dy) > 0) moved = moved || Math.abs(dx) + Math.abs(dy) > 2;
      const view = currentView();
      center = { x: view.cx - dx / view.scale, z: view.cz - dy / view.scale };
      renderMap();
    } else if (pointers.size === 2) {
      const [a, b] = [...pointers.values()];
      const d = Math.hypot(a.x - b.x, a.y - b.y);
      const rect = canvas.getBoundingClientRect();
      if (pinch > 0) zoomAt(d / pinch, (a.x + b.x) / 2 - rect.left, (a.y + b.y) / 2 - rect.top);
      pinch = d;
      moved = true;
    }
  });
  const release = (event) => {
    pointers.delete(event.pointerId);
    if (pointers.size < 2) pinch = 0;
  };
  canvas.addEventListener('pointerup', release);
  canvas.addEventListener('pointercancel', release);

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
  };

  function heightAt(data, x, z) {
    const n = data.points;
    if (!n) return 0;
    const cell = data.cell || 1;
    const fx = Math.min(Math.max((x + data.relief) / cell, 0), n - 1.001);
    const fz = Math.min(Math.max((z + data.relief) / cell, 0), n - 1.001);
    const i = Math.floor(fx), j = Math.floor(fz), tx = fx - i, tz = fz - j;
    const h = (a, b) => data.heights[a * n + b];
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
  function buildLand(d) {
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
        if (h < d.seaLevel) {  // the sea: lighter in the shallows
          const k = Math.min(1, (d.seaLevel - h) / 25);
          r = 70 - 40 * k; g = 150 - 60 * k; b = 190 - 40 * k;
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
          if (dx * dx + dz * dz < 1 && h < p.level) { r = 72; g = 140; b = 170; }
        }
        if (Math.hypot(x - d.crater.x, z - d.crater.z) < d.crater.r * 0.8) { r = 240; g = 110; b = 40; }  // lava
        // Light from the north-west, shading the slopes.
        const shade = 1 + 0.03 * ((H(i - 1, j) - H(i + 1, j)) + (H(i, j - 1) - H(i, j + 1)));
        const k = Math.min(Math.max(shade, 0.55), 1.4);
        const o = (j * d.pointsX + i) * 4;
        pixels.data[o] = r * k; pixels.data[o + 1] = g * k; pixels.data[o + 2] = b * k; pixels.data[o + 3] = 255;
      }
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

  // The playable area, drawn once per world: terrain, path and objects.
  function buildBase(data, continent) {
    const R = data.world + 7;
    const size = Math.round(2 * R * PX_PER_M);
    const image = el('canvas', { width: size, height: size });
    const ctx = image.getContext('2d');
    const pixels = ctx.createImageData(size, size);
    const lake = data.lake, village = data.village, ruins = data.ruins;
    const forest = data.forest, cave = data.cave, castle = data.castle;
    for (let py = 0; py < size; ++py) {
      for (let px = 0; px < size; ++px) {
        const x = -R + (px + 0.5) / PX_PER_M, z = -R + (py + 0.5) / PX_PER_M;
        const h = heightAt(data, x, z);
        const t = Math.min(Math.max((h + 4) / 12, 0), 1);
        let r = 78 + 70 * t, g = 128 + 46 * t, b = 62 + 30 * t;
        if (forest) {  // darker under the trees
          const fx = (x - forest.x) / forest.rx, fz = (z - forest.z) / forest.rz;
          if (fx * fx + fz * fz < 1) { r *= 0.8; g *= 0.88; b *= 0.8; }
        }
        if (cave && Math.hypot(x - cave.x, z - cave.z) < cave.r) { const k = Math.min(1, (h + 2) / 14); r = 110 + 50 * k; g = 106 + 46 * k; b = 104 + 44 * k; }
        if (village && Math.hypot(x - village.x, z - village.z) < 7) { r = 168; g = 142; b = 104; }
        if (ruins && Math.max(Math.abs(x - ruins.x), Math.abs(z - ruins.z)) < 8.5) { r = 150; g = 146; b = 136; }
        if (castle && Math.max(Math.abs(x - castle.x), Math.abs(z - castle.z)) < castle.r) { r = 156; g = 150; b = 140; }
        // The sea where it comes in, and its beaches (not the hollows of the hills).
        const land = Math.abs(x) > 150 || Math.abs(z) > 150 ? landAt(continent, x, z) : 0;
        if (h < data.seaLevel + 2.5 && land < data.seaLevel + 1) { r = 214; g = 196; b = 140; }
        if (h < data.seaLevel && land < data.seaLevel) { const k = Math.min(1, (data.seaLevel - h) / 12); r = 70 - 40 * k; g = 150 - 60 * k; b = 190 - 40 * k; }
        if (lake) {
          const dx = (x - lake.x) / lake.rx, dz = (z - lake.z) / lake.rz;
          const d = dx * dx + dz * dz;
          if (d < 1.08 && h > data.waterLevel - 0.05) { r = 214; g = 196; b = 140; }  // sandy shore
          if (d < 1 && h < data.waterLevel) { const k = Math.min(1, (data.waterLevel - h) / 1.2); r = 80 - 30 * k; g = 150 - 40 * k; b = 196 - 20 * k; }
        }
        // Light from the north-west, shading the slopes.
        const shade = 1 + 0.45 * ((heightAt(data, x - 1, z) - heightAt(data, x + 1, z)) +
                                  (heightAt(data, x, z - 1) - heightAt(data, x, z + 1)));
        const k = Math.min(Math.max(shade, 0.6), 1.35);
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
          kind === 'well' || kind === 'rubble' || kind === 'stump' || kind === 'castleTower' || kind === 'crystal') {
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

  let teleportTargets = [];  // where to click on the map, in teleport mode
  canvas.addEventListener('click', (event) => {
    if (!teleportMode || moved) return;
    const rect = canvas.getBoundingClientRect();
    const x = event.clientX - rect.left, y = event.clientY - rect.top;
    const target = teleportTargets.find((t) => Math.hypot(t.x - x, t.y - y) < 18);
    if (!target) return;
    game.teleportTo(target.id);
    close();  // the game goes on: the teleport happens with the next tick
  });

  // Opening the map: on the player, up close (to choose a teleporter: the whole playable area).
  let fitPlayable = false;  // the next drawing shows the whole playable area
  let mapData = null;  // the world's map data (big: read again only when the map opens)
  function openMap(teleport) {
    mapData = null;
    center = teleport ? { x: 0, z: 0 } : null;
    fitPlayable = teleport;
  }

  function renderMap() {
    if (!game) return;
    if (!mapData) mapData = game.map();
    const data = mapData;
    const key = `${data.path.length}:${data.path[0]}:${data.path[1]}:${data.objects.length}`;
    if (!base || base.key !== key) {
      const c = game.continent();
      land = c.ready ? { data: c, image: buildLand(c) } : null;
      base = { key, ...buildBase(data, land && land.data) };
    }

    const p = game.state().player;
    $('map-where').textContent = teleportMode ? 'Choisis un téléporteur où aller.'
      : `Tu te trouves : ${LORE.zones[p.zone] || p.zone}`;
    const dpr = window.devicePixelRatio || 1;
    const cssW = canvas.clientWidth, cssH = canvas.clientHeight;
    if (!cssW || !cssH) return;
    canvas.width = Math.round(cssW * dpr);
    canvas.height = Math.round(cssH * dpr);
    const ctx = canvas.getContext('2d');
    ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
    ctx.fillStyle = 'rgb(30, 90, 150)';  // the open sea (as the deep sea of the continent's image)
    ctx.fillRect(0, 0, cssW, cssH);

    if (fitPlayable) {
      scale = Math.min(cssW, cssH) / 320;
      fitPlayable = false;
    }
    const view = currentView();
    const k = view.scale;  // CSS px per meter
    const X = (x) => cssW / 2 + (x - view.cx) * k;
    const Y = (z) => cssH / 2 + (z - view.cz) * k;
    $('map-zoom-in').disabled = k >= MAX_SCALE - 1e-3;
    $('map-zoom-out').disabled = k <= minScale() + 1e-3;
    $('map-scale').textContent = k >= 1 ? `${Math.round(100 / k)} m` : `${(100 / k / 1000).toLocaleString('fr-FR', { maximumFractionDigits: 1 })} km`;
    ctx.imageSmoothingEnabled = true;
    if (land) {
      const d = land.data;
      // Each pixel is centered on a grid point.
      ctx.drawImage(land.image, X(d.minX - d.cell / 2), Y(d.minZ - d.cell / 2), d.pointsX * d.cell * k, d.pointsZ * d.cell * k);
    }
    ctx.drawImage(base.image, X(-base.R), Y(-base.R), 2 * base.R * k, 2 * base.R * k);

    // The edge of the playable area.
    ctx.strokeStyle = 'rgba(255, 255, 255, 0.45)';
    ctx.setLineDash([6, 6]);
    ctx.lineWidth = 1;
    ctx.strokeRect(X(-data.world), Y(-data.world), 2 * data.world * k, 2 * data.world * k);
    ctx.setLineDash([]);

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
    }

    // The teleporters found (the ones reachable from here, in teleport mode: brighter, larger).
    teleportTargets = [];
    for (const t of game.teleporters().list) {
      if (!t.known) continue;
      const reachable = teleportMode && t.reachable;
      const r = reachable ? 11 : k > 1 ? 6 : 4;
      ctx.beginPath();
      ctx.arc(X(t.x), Y(t.z), r, 0, Math.PI * 2);
      ctx.fillStyle = reachable ? 'rgba(127, 224, 255, 0.95)' : 'rgba(127, 224, 255, 0.6)';
      ctx.fill();
      ctx.lineWidth = 2;
      ctx.strokeStyle = '#1b3d52';
      ctx.stroke();
      if (reachable) teleportTargets.push({ id: t.id, x: X(t.x), y: Y(t.z) });
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
