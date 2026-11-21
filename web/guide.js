// The "Guide" tab of the grimoire: help for playing, an entry per subject
// (the list on the left, the chosen one on the right). For now, the types:
// their icons, what each one is strong or weak against, and the whole chart.
(() => {
  'use strict';

  const LORE = window.MonstresLore;
  const { $, el, prefs, whenReady } = window.MonstresUtil;

  // ---- The types ----
  // In this order everywhere in the guide. Names and colors from lore.js.
  const TYPES = ['fire', 'water', 'nature', 'earth', 'air', 'electric', 'ice', 'light', 'shadow', 'arcane', 'normal'];

  // Each icon is drawn in dark lines on a disk of the type's color.
  const GLYPHS = {
    fire: '<path d="M12 3c1 3 5 5 5 10a5 5 0 0 1-10 0c0-2 1-3.5 2-4.5 0 2 1 3 2 3-1-3 0-6 1-8.5z"/>',
    water: '<path d="M12 3c3 4.5 6 7.5 6 11a6 6 0 0 1-12 0c0-3.5 3-6.5 6-11z"/><path d="M9.5 14.5a2.5 2.5 0 0 0 2.5 2.5"/>',
    nature: '<path d="M5 19c0-8 5-13 14-14 0 9-5 14-13 14z"/><path d="M5 19l8-8"/>',
    earth: '<path d="M3 19l6-10 3 4 2-3 7 9z"/><path d="M9 9l1.5 4"/>',
    air: '<path d="M3 9h11a3 3 0 1 0-3-3"/><path d="M3 13h15a3 3 0 1 1-3 3"/><path d="M3 17h6"/>',
    electric: '<path d="M13 2L5 14h6l-1 8 8-12h-6z"/>',
    ice: '<path d="M12 3v18M4.2 7.5l15.6 9M4.2 16.5l15.6-9"/><path d="M9.5 4.5L12 6.5l2.5-2M9.5 19.5l2.5-2 2.5 2"/>',
    light: '<circle cx="12" cy="12" r="4"/><path d="M12 2v3M12 19v3M2 12h3M19 12h3M4.9 4.9l2.1 2.1M17 17l2.1 2.1M4.9 19.1L7 17M17 7l2.1-2.1"/>',
    shadow: '<path d="M16 3.5A8.5 8.5 0 1 0 20.5 15 7 7 0 0 1 16 3.5z"/>',
    arcane: '<path d="M12 2l2 7.5 7.5 2.5-7.5 2.5L12 22l-2-7.5L2.5 12 10 9.5z"/>',
    normal: '<circle cx="12" cy="12" r="6"/><circle cx="12" cy="12" r="1.5"/>',
  };

  const typeName = (id) => (LORE.elements[id] || { name: id }).name;
  function typeIcon(id, size = 22) {
    const color = (LORE.elements[id] || { color: '#ccc' }).color;
    const icon = el('span', { className: 'type-icon', title: typeName(id),
      innerHTML: `<svg viewBox="0 0 24 24" width="${size}" height="${size}" fill="none" stroke="#2a2233" ` +
        `stroke-width="1.9" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true">${GLYPHS[id]}</svg>` });
    icon.style.background = color;
    icon.dataset.type = id;
    icon.setAttribute('role', 'img');
    icon.setAttribute('aria-label', typeName(id));
    return icon;
  }

  // The chart: how an attack of a type does against a monster of a type,
  // +1 efficient, -1 inefficient, 0 nothing particular. From the game (the
  // rules of battles: src/core/battle.cpp, type_effect); both tables of the
  // guide are read from it, so they always agree.
  let chart = null;
  whenReady((game) => {
    const c = game.typeChart();
    chart = Object.fromEntries(c.types.map((a, i) => [a, Object.fromEntries(c.types.map((d, j) => [d, c.chart[i][j]]))]));
  });
  const effect = (attack, defense) => (chart && chart[attack] ? chart[attack][defense] || 0 : 0);

  function icons(ids) {
    return ids.length ? el('span', { className: 'type-icons' }, ids.map((id) => typeIcon(id, 18)))
      : el('span', { className: 'none', textContent: '—' });
  }

  function typesPage() {
    const list = el('ul', { className: 'type-list' }, TYPES.map((t) => {
      const item = el('li', {}, [typeIcon(t), el('span', { textContent: typeName(t) })]);
      item.dataset.type = t;
      return item;
    }));

    const head = ['Type', 'Fort contre', 'Faible contre', 'Résiste à', 'Vulnérable à'];
    const rows = TYPES.map((t) => {
      const row = el('tr', {}, [
        el('th', { scope: 'row' }, [typeIcon(t)]),
        el('td', { className: 'strong' }, [icons(TYPES.filter((d) => effect(t, d) > 0))]),
        el('td', { className: 'weak' }, [icons(TYPES.filter((d) => effect(t, d) < 0))]),
        el('td', { className: 'resists' }, [icons(TYPES.filter((a) => effect(a, t) < 0))]),
        el('td', { className: 'vulnerable' }, [icons(TYPES.filter((a) => effect(a, t) > 0))]),
      ]);
      row.dataset.type = t;
      return row;
    });
    const summary = el('table', { className: 'type-table', id: 'type-summary' }, [
      el('thead', {}, [el('tr', {}, head.map((h) => el('th', { scope: 'col', textContent: h })))]),
      el('tbody', {}, rows),
    ]);

    const SIGN = { 1: ['+', 'efficace'], 0: ['·', 'normal'], [-1]: ['−', 'peu efficace'] };
    const matrix = el('table', { className: 'type-matrix', id: 'type-matrix' }, [
      el('thead', {}, [el('tr', {}, [
        el('th', { className: 'corner', innerHTML: '<span>Attaque ↓</span><span>Défense →</span>' }),
        ...TYPES.map((d) => el('th', { scope: 'col' }, [typeIcon(d, 16)])),
      ])]),
      el('tbody', {}, TYPES.map((a) => {
        const row = el('tr', {}, [el('th', { scope: 'row' }, [typeIcon(a, 16)]), ...TYPES.map((d) => {
          const [sign, word] = SIGN[effect(a, d)];
          const cell = el('td', { className: effect(a, d) > 0 ? 'plus' : effect(a, d) < 0 ? 'minus' : 'dot', textContent: sign,
            title: `${typeName(a)} contre ${typeName(d)} : ${word}` });
          cell.dataset.defense = d;
          return cell;
        })]);
        row.dataset.type = a;
        return row;
      })),
    ]);

    return [
      el('h2', { textContent: 'Les types' }),
      el('p', { className: 'description', textContent: 'Chaque monstre et chaque attaque a un type. ' +
        'Une attaque peut être efficace (plus de dégâts) ou peu efficace (moins de dégâts) selon le type du monstre touché.' }),
      el('p', { className: 'facts', textContent: 'Efficace : dégâts × 1,5. Peu efficace : dégâts ÷ 1,5. ' +
        'Un monstre à deux types compte les deux : × 2,25 s’il est vulnérable deux fois, ÷ 2,25 s’il résiste deux fois, ' +
        'dégâts normaux s’il est vulnérable d’un côté et résiste de l’autre. ' +
        'Les attaques rapides sont de type Normal ; les attaques spéciales, du type du monstre.' }),
      el('h3', { textContent: 'Les onze types' }),
      list,
      el('h3', { textContent: 'Forces et faiblesses' }),
      el('p', { className: 'facts', textContent: 'Fort ou faible contre : ses attaques sur ces types. ' +
        'Résiste ou vulnérable à : les attaques de ces types sur lui.' }),
      el('div', { className: 'table-scroll' }, [summary]),
      el('h3', { textContent: 'Toutes les rencontres' }),
      el('p', { className: 'facts', textContent: 'Le type qui attaque à gauche, celui qui se défend en haut. ' +
        '+ efficace, − peu efficace, · rien de particulier.' }),
      el('div', { className: 'table-scroll' }, [matrix]),
    ];
  }

  // ---- The entries ----
  const ENTRIES = [
    { id: 'types', title: 'Les types', note: 'Forces et faiblesses', icon: () => typeIcon('fire', 26), page: typesPage },
  ];
  let chosen = prefs.get('guideEntry', 'types');

  function renderTab() {
    const entry = ENTRIES.find((e) => e.id === chosen) || ENTRIES[0];
    $('guide-list').replaceChildren(...ENTRIES.map((e) => {
      const button = el('button', { type: 'button', className: 'bestiary-entry guide-entry' }, [
        e.icon(), el('span', {}, [el('strong', { textContent: e.title }), el('small', { textContent: e.note })])]);
      button.dataset.entry = e.id;
      if (e === entry) button.setAttribute('aria-current', 'true');
      button.addEventListener('click', () => {
        chosen = e.id;
        prefs.set('guideEntry', e.id);
        renderTab();
      });
      return el('li', {}, [button]);
    }));
    const detail = $('guide-detail');
    detail.dataset.entry = entry.id;
    detail.replaceChildren(...entry.page());
  }

  window.MonstresGuide = { renderTab, types: TYPES, effect };
})();
