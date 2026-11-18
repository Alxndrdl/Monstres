// The magician's spells, on the page: out of battles, the four adventure
// spells where the attacks are in a battle; in a battle or training, the two
// combat ones beside the attacks; capturing, the four capture ones in a
// square at the bottom right, over the mana (#seal-tools). Keys R, G, V, X.
// Also the "Sorts" tab of the grimoire, where they are chosen (out of
// battles). The rules are in the game (src/core/battle.cpp).
(() => {
  'use strict';

  const LORE = window.MonstresLore;
  const { $, el, whenReady, setText } = window.MonstresUtil;
  let game = null;

  const ICONS = {
    heal: '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.4" stroke-linecap="round" ' +
      'stroke-linejoin="round" aria-hidden="true"><path d="M12 5v14M5 12h14"/></svg>',
    shield: '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.2" stroke-linecap="round" ' +
      'stroke-linejoin="round" aria-hidden="true"><path d="M12 3l7 3v6c0 4.5-3 7.5-7 9-4-1.5-7-4.5-7-9V6z"/>' +
      '<path d="M9 12l2 2 4-4"/></svg>',
    // Mercy: a hand held open.
    mercy: '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" ' +
      'stroke-linejoin="round" aria-hidden="true"><path d="M7 11V6.5a1.5 1.5 0 0 1 3 0V11M10 10V5a1.5 1.5 0 0 1 3 0v5"/>' +
      '<path d="M13 10V6a1.5 1.5 0 0 1 3 0v6M16 11.5a1.5 1.5 0 0 1 3 0V15a6 6 0 0 1-6 6h-1a6 6 0 0 1-5-2.7L4.5 14.5a1.6 1.6 0 0 1 2.5-2L7 11"/></svg>',
    // Detection: waves around a point.
    detect: '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" ' +
      'stroke-linejoin="round" aria-hidden="true"><circle cx="12" cy="12" r="2"/><path d="M7.8 7.8a6 6 0 0 0 0 8.4M16.2 7.8a6 6 0 0 1 0 8.4"/>' +
      '<path d="M5 5a10 10 0 0 0 0 14M19 5a10 10 0 0 1 0 14"/></svg>',
    // Way out: a winding path to a door.
    wayOut: '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" ' +
      'stroke-linejoin="round" aria-hidden="true"><path d="M4 20c4 0 4-5 8-5s4-5 8-5"/><path d="M16 3h5v7"/>' +
      '<circle cx="4" cy="20" r="1.2"/></svg>',
    // The capture spells: the series again, a guard, a boost, another series, halfway, the next rune.
    replay: '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.2" stroke-linecap="round" ' +
      'stroke-linejoin="round" aria-hidden="true"><path d="M4 12a8 8 0 1 0 2.5-5.8"/><path d="M4 4v5h5"/></svg>',
    guard: '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.2" stroke-linecap="round" ' +
      'stroke-linejoin="round" aria-hidden="true"><path d="M12 3l7 3v6c0 4.5-3 7.5-7 9-4-1.5-7-4.5-7-9V6z"/>' +
      '<path d="M12 8v5M10 10.5h4"/></svg>',
    boost: '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.4" stroke-linecap="round" ' +
      'stroke-linejoin="round" aria-hidden="true"><path d="M6 13l6-6 6 6M6 19l6-6 6 6"/></svg>',
    newOrder: '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.2" stroke-linecap="round" ' +
      'stroke-linejoin="round" aria-hidden="true"><path d="M3 7h4l10 10h4M3 17h4l3-3M14 10l3-3h4"/>' +
      '<path d="M18 4l3 3-3 3M18 14l3 3-3 3"/></svg>',
    steady: '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.2" stroke-linecap="round" ' +
      'stroke-linejoin="round" aria-hidden="true"><rect x="3" y="9" width="18" height="6" rx="3"/><path d="M12 5v14"/></svg>',
    hint: '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.2" stroke-linecap="round" ' +
      'stroke-linejoin="round" aria-hidden="true"><path d="M2 12s3.5-7 10-7 10 7 10 7-3.5 7-10 7S2 12 2 12z"/>' +
      '<circle cx="12" cy="12" r="3"/></svg>',
  };
  const WHY = {
    cooldown: 'en recharge',
    noCompanion: 'aucun monstre sorti',
    full: 'PV au maximum',
    fleeing: 'pendant la fuite',
    mana: 'pas assez de mana',
    active: 'déjà actif',
    training: 'inutile à l’entraînement',
    outside: 'seulement dans une grotte ou un bâtiment',
    notNow: 'pas maintenant',
    steady: 'déjà à mi-chemin ou plus',
  };
  const spellName = (id) => (LORE.spells[id] || { name: id }).name;

  // ---- The buttons: four by the jump (adventure; the first two in a
  // battle), four in a square at the bottom right while capturing ----
  const KEYS = { KeyR: 0, KeyG: 1, KeyV: 2, KeyX: 3 };
  const KEY_NAMES = ['R', 'G', 'V', 'X'];
  const padButtons = [0, 1, 2, 3].map((k) => $(`spell-${k}`));
  const sealButtons = [0, 1, 2, 3].map((k) => $(`seal-spell-${k}`));
  let buttons = padButtons;  // the ones shown now
  let shown = '';
  for (const button of [...padButtons, ...sealButtons]) {
    const slot = Number(button.dataset.slot);
    button.addEventListener('pointerdown', (event) => {
      event.preventDefault();  // no focus
      if (game && !button.disabled) game.cast(slot);
    });
    button.addEventListener('click', (event) => event.preventDefault());
  }
  const typing = (event) => event.target instanceof HTMLElement &&
    (event.target.isContentEditable || ['INPUT', 'TEXTAREA', 'SELECT'].includes(event.target.tagName));
  window.addEventListener('keydown', (event) => {
    if (!game || event.repeat || event.ctrlKey || event.metaKey || event.altKey || typing(event)) return;
    if (!(event.code in KEYS)) return;
    if (window.MonstresMenu && window.MonstresMenu.isOpen()) return;
    const button = buttons[KEYS[event.code]];
    if (!button || button.hidden || button.disabled) return;
    event.preventDefault();
    game.cast(KEYS[event.code]);
    button.classList.add('pressed');
    setTimeout(() => button.classList.remove('pressed'), 120);
  });

  // The magician's mana: above the spells; capturing, under the capture spells.
  const manas = [$('mana'), $('seal-mana')];
  let manaShown = '';
  function renderMana(s) {
    const key = `${s.manaNow}/${s.manaMax}`;
    if (key === manaShown) return;
    manaShown = key;
    for (const mana of manas) {
      mana.querySelector('.mana-fill').style.width = `${s.manaMax ? (100 * s.manaNow) / s.manaMax : 0}%`;
      setText(mana.querySelector('.mana-text'), key);
      mana.classList.toggle('low', s.manaNow < 6);
      mana.title = `Mana : ${s.manaNow} / ${s.manaMax}`;
      mana.setAttribute('aria-label', mana.title);
    }
  }

  function render() {
    const s = game.spells();
    renderMana(s);
    buttons = s.capturing ? sealButtons : padButtons;
    const key = `${s.capturing}/${s.now.map((x) => x.id).join()}`;
    if (key !== shown) {
      shown = key;
      for (const button of s.capturing ? padButtons : sealButtons) button.hidden = true;
      buttons.forEach((button, i) => {
        const x = s.now[i] || { id: 'none' };
        button.hidden = x.id === 'none';
        button.dataset.spell = x.id;
        button.querySelector('.spell-icon').innerHTML = ICONS[x.id] || '';
        const name = button.querySelector('.spell-name');
        if (name) name.textContent = x.id === 'none' ? '' : spellName(x.id);
      });
    }
    s.now.forEach((x, i) => {
      const button = buttons[i];
      if (!button || button.hidden) return;
      const active = x.why === 'active';
      button.disabled = !!x.why;
      button.classList.toggle('active', active);
      button.classList.toggle('no-mana', x.why === 'mana');
      button.style.setProperty('--cd', x.cooldownTotal && !(active && x.id === 'mercy') ? x.cooldown / x.cooldownTotal : 0);
      const label = `${spellName(x.id)} · ${x.mana} mana${x.why ? ` (${WHY[x.why] || x.why})` : ''}`;
      const cost = button.querySelector('.spell-cost');
      if (cost) setText(cost, x.mana ? `${x.mana}` : '');
      button.title = label;
      button.setAttribute('aria-label', label);
    });
  }

  // ---- The grimoire's tab: the slots on the left; on the right, the chosen
  // slot's spell, and a button to change it (the spells it can hold; one
  // already in another slot of its kind is greyed: choosing it swaps them).
  // Command::EquipSpell's slots: 0, 1 combat; 2 to 5 adventure; 6 to 9 capture.
  const FIRST = { combat: 0, adventure: 2, capture: 6 };
  const SLOTS = ['adventure', 'combat', 'capture'].flatMap((kind) =>  // adventure first (the most used)
    [0, 1, 2, 3].slice(0, kind === 'combat' ? 2 : 4).map((index) => ({ slot: FIRST[kind] + index, kind, index, key: KEY_NAMES[index] })));
  const KIND = { adventure: 'Aventure', combat: 'Combat', capture: 'Capture' };
  const KIND_OF = { adventure: 'd’aventure', combat: 'de combat', capture: 'de capture' };
  let chosen = 2;        // slot shown on the right
  let editing = false;   // choosing its new spell
  let candidate = null;  // the spell picked in the list (its id, 'none' to empty the slot)

  const slotSpell = (s, x) => s[x.kind][x.index];
  const costText = (k) => (k.perRune ? `${k.mana} mana par rune restante` : `${k.mana} mana`);
  const icon = (id) => el('span', { className: `spell-badge ${id}`, innerHTML: ICONS[id] || '' });

  function spellFacts(id) {
    if (id === 'none') return [el('p', { className: 'facts', textContent: 'Aucun sort : le bouton n’apparaît pas.' })];
    const known = game.spells().known.find((k) => k.id === id) || {};
    const kinds = known.capture ? 'pendant une capture'
      : known.combat && known.adventure ? 'combat et aventure' : known.combat ? 'combat seulement' : 'aventure seulement';
    const wait = known.capture ? '' : id === 'mercy' ? ' · jusqu’à la fin du combat'
      : id === 'detect' || id === 'wayOut' ? ' · dure 1 min' : ` · recharge ${known.cooldown} s`;
    return [
      el('p', { textContent: (LORE.spells[id] || {}).text || '' }),
      el('p', { className: 'facts', textContent: `Coût : ${costText(known)}${wait} · ${kinds}` }),
    ];
  }

  function renderTab() {
    const s = game.spells();
    const x = SLOTS.find((y) => y.slot === chosen) || SLOTS[0];
    if (!s.editable) editing = false;
    // The slots.
    const list = $('spell-list');
    const items = [];
    for (const kind of ['adventure', 'combat', 'capture']) {
      items.push(el('li', { className: 'quest-section', textContent: `Sorts ${KIND_OF[kind]}` }));
      for (const y of SLOTS.filter((z) => z.kind === kind)) {
        const id = slotSpell(s, y);
        const button = el('button', { type: 'button', className: 'bestiary-entry spell-entry', id: `spell-slot-${y.slot}` }, [
          icon(id),
          el('span', {}, [el('strong', { textContent: id === 'none' ? 'Vide' : spellName(id) }),
            el('small', { textContent: `${KIND[kind]} · touche ${y.key}${id === 'none' ? '' :
              ` · ${costText(s.known.find((k) => k.id === id) || {})}`}` })]),
        ]);
        button.dataset.spell = id;
        if (y.slot === chosen) button.setAttribute('aria-current', 'true');
        button.addEventListener('click', () => {
          chosen = y.slot;
          editing = false;
          renderTab();
        });
        items.push(el('li', {}, [button]));
      }
    }
    list.replaceChildren(...items);

    // The chosen slot.
    const detail = $('spell-detail');
    const id = slotSpell(s, x);
    const edit = el('button', { type: 'button', className: 'icon-button spell-edit', id: 'spell-edit', disabled: !s.editable,
      title: s.editable ? 'Changer de sort' : 'Les sorts se changent hors combat',
      innerHTML: '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" ' +
        'stroke-linejoin="round" aria-hidden="true"><path d="M4 20h4L19 9l-4-4L4 16z"/><path d="M13 7l4 4"/></svg>' });
    edit.setAttribute('aria-label', edit.title);
    edit.addEventListener('click', () => {
      editing = !editing;
      candidate = null;
      renderTab();
    });
    const head = el('div', { className: 'spell-head' }, [icon(id),
      el('div', {}, [el('h2', { textContent: id === 'none' ? 'Emplacement vide' : spellName(id) }),
        el('p', { className: 'facts', textContent: `Sort ${KIND_OF[x.kind]} · touche ${x.key}` })]),
      edit]);
    const body = [head];
    if (!editing) {
      body.push(...spellFacts(id));
      if (!s.editable) body.push(el('p', { className: 'facts', textContent: 'Les sorts se changent hors combat.' }));
    } else {
      // The spells this slot can hold; those in the other slots of this kind are greyed (they would swap).
      const others = SLOTS.filter((y) => y.kind === x.kind && y.slot !== x.slot).map((y) => slotSpell(s, y));
      const options = [{ id: 'none', value: 0 }, ...s.known.filter((k) => k[x.kind])];
      body.push(el('p', { className: 'facts', textContent: 'Choisis le sort de cet emplacement :' }));
      body.push(el('ul', { className: 'spell-choices' }, options.map((o) => {
        const button = el('button', { type: 'button', className: 'bestiary-entry spell-choice', id: `spell-choice-${o.id}` }, [
          icon(o.id), el('strong', { textContent: o.id === 'none' ? 'Aucun' : spellName(o.id) })]);
        button.classList.toggle('taken', o.id !== 'none' && others.includes(o.id));
        button.classList.toggle('current', o.id === id);
        if (o.id === candidate) button.setAttribute('aria-current', 'true');
        button.addEventListener('click', () => {
          candidate = o.id;
          renderTab();
        });
        return el('li', {}, [button]);
      })));
      if (candidate) {
        body.push(...spellFacts(candidate));
        if (candidate !== 'none' && others.includes(candidate)) {
          body.push(el('p', { className: 'facts', textContent: 'Déjà dans un autre emplacement : les deux seront échangés.' }));
        }
        const value = candidate === 'none' ? 0 : options.find((o) => o.id === candidate).value;
        const ok = el('button', { type: 'button', className: 'button primary', id: 'spell-confirm',
          textContent: candidate === id ? 'Garder ce sort' : 'Valider', disabled: false });
        ok.addEventListener('click', () => {
          if (candidate !== id) game.equipSpell(x.slot, value);
          editing = false;
          candidate = null;
          renderTab();
        });
        const cancel = el('button', { type: 'button', className: 'button', textContent: 'Annuler' });
        cancel.addEventListener('click', () => {
          editing = false;
          candidate = null;
          renderTab();
        });
        body.push(el('div', { className: 'row' }, [ok, cancel]));
      }
    }
    detail.replaceChildren(el('div', { className: 'spell-detail' }, body));
  }

  const start = () => {
    game = window.monstres;
    if (typeof game.spells !== 'function') return;
    setInterval(render, 100);
    render();
  };
  whenReady(start);
  window.MonstresSpells = { renderTab };
})();
