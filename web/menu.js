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
    if (!['character', 'team', 'bestiary', 'map'].includes(tab)) tab = 'character';
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
  const SHORTCUTS = { c: 'character', e: 'team', b: 'bestiary', m: 'map' };
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

  // Letting a monster go back to the wild: asked twice, done once the grimoire is closed.
  let releasing = -1;  // slot waiting for the confirmation
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
    if (releasing !== m.slot) {
      const button = el('button', { type: 'button', className: 'button danger', id: 'release', textContent: 'Relâcher' });
      button.addEventListener('click', () => {
        releasing = m.slot;
        renderTeam();
      });
      box.append(button, el('p', { className: 'facts', textContent: 'Il sort du cristal et retourne vivre dans la nature.' }));
      return box;
    }
    const yes = el('button', { type: 'button', className: 'button danger', id: 'release-confirm', textContent: `Oui, relâcher ${name}` });
    const no = el('button', { type: 'button', className: 'button', id: 'release-cancel', textContent: 'Non' });
    yes.addEventListener('click', () => {
      releasing = -1;
      game.release(m.slot);
      chosenMember = 0;
      prefs.set('teamSlot', 0);
      close();  // to see it go
    });
    no.addEventListener('click', () => {
      releasing = -1;
      renderTeam();
    });
    box.append(el('p', { textContent: `Relâcher ${name} ? Il ne reviendra pas, et le recapturer ne rapportera pas d’expérience.` }),
      el('div', { className: 'row' }, [yes, no]));
    return box;
  }

  function renderMember(m, teamSize) {
    const info = LORE.species[m.species];
    const texts = info.variants || {};
    const stats = el('dl', { className: 'stats' });
    statList(stats, [['Points de vie', m.maxHp, 80], ['Attaque', m.attack, 25], ['Défense', m.defense, 25],
      ['Vitesse', m.speed, 25], ['Magie', m.magic, 25]]);
    const variants = m.variants.filter((id) => !id.startsWith('sex.'))
      .map((id) => (id.includes('.') ? '' : '✦ ') + ((texts[id] || {}).name || id));
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
        el('div', { className: 'badges' }, [familyBadge(m.family), ...elementBadges(m.elements)]),
        el('p', { className: 'rank', textContent: `Niveau ${m.level}` }),
        el('div', { className: 'xp' }, [
          el('div', { className: 'bar' }, [el('span', { style: `width:${Math.min(100, (100 * m.xp) / m.xpToNext)}%` })]),
          el('span', { textContent: `Expérience : ${m.xp} / ${m.xpToNext}` }),
        ]),
        el('div', { className: 'xp hp' }, [hpBar(m.hp, m.maxHp), el('span', { textContent: `PV : ${m.hp} / ${m.maxHp}` })]),
        variants.length ? el('p', { className: 'facts', textContent: `Variantes : ${variants.join(' · ')}` }) : '',
      ]),
      stats,
      el('section', { className: 'moves-box' }, [el('h3', { textContent: 'Attaques' }), moves]),
      releaseBox(m, teamSize),
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

  const variantText = (species, id) => (LORE.species[species].variants || {})[id] || { name: id, description: '' };
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
          el('small', { className: 'dna-count' }, [dnaIcon(), `${known(e)}/${e.variants.length}`]),
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
      : `Dans ce monde : ${e.count} ${e.count > 1 ? 'individus' : 'individu'}.`;
    const dna = el('button', { type: 'button', className: 'dna-button', id: 'dna-button' }, [
      dnaIcon(),
      el('span', { textContent: 'Variantes' }),
      el('small', { textContent: `${known(e)}/${e.variants.length} découvertes` }),
    ]);
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
      const key = v.kind === 'chromatic' || v.kind === 'elemental' ? 'rare' : v.id.split('.')[0];
      let group = groups.find((g) => g.key === key);
      if (!group) groups.push(group = { key, title: key === 'rare' ? 'Formes rares' : genes[key] || key, items: [] });
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
            textContent: `${known(e)} découvertes sur ${e.variants.length} · ${captured(e)} capturées`,
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
      : (info.genes || {})[v.id.split('.')[0]] || '';
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
        el('p', { className: 'facts', textContent: `Où la trouver : ${where}` }),
        el('p', { className: `facts status ${v.status}`, textContent: STATUS_TEXT[v.status] }),
      ]),
    );
  }

  // ---- Maps ----
  const canvas = $('map-canvas');
  const PX_PER_M = 2;           // resolution of the drawn terrain
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

  // The terrain, path and objects, drawn once per world.
  function buildBase(data) {
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
        if (lake) {
          const dx = (x - lake.x) / lake.rx, dz = (z - lake.z) / lake.rz;
          const d = dx * dx + dz * dz;
          if (d < 1.08 && h > data.waterLevel - 0.05) { r = 214; g = 196; b = 140; }  // sandy shore
          if (d < 1 && h < data.waterLevel) { const k = Math.min(1, (data.waterLevel - h) / 1.2); r = 80 - 30 * k; g = 150 - 40 * k; b = 196 - 20 * k; }
        }
        // Light from the north-west, shading the slopes.
        const shade = 1 + 0.45 * ((heightAt(data, x - 1, z) - heightAt(data, x + 1, z)) +
                                  (heightAt(data, x, z - 1) - heightAt(data, x, z + 1)));
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
    for (const [i, c] of s.creatures.entries()) {
      const mine = i === 0;  // the companion (unless it is in the crystal)
      if ((mine && s.active < 0) || (global && !mine)) continue;
      ctx.beginPath();
      ctx.arc(X(c.x), Y(c.z), mine ? 4.5 : 3.5, 0, Math.PI * 2);
      ctx.fillStyle = mine ? SPECIES_DOT.companion : SPECIES_DOT[c.species] || '#fff';
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
    if (data.forest) label(LORE.zones.forest, data.forest.x, data.forest.z, labelSize);
    if (data.cave) label(LORE.zones.cave, data.cave.x, data.cave.z, labelSize);
    if (data.castle) label(LORE.zones.castle, data.castle.x, data.castle.z - data.castle.r - (global ? 6 : 3), labelSize);

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
