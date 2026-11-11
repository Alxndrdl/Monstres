// The team bar, on the left: one icon per monster of the team. Touching a
// monster in the crystal summons it where the companion is (the companion
// goes back in); touching the companion recalls it. During a fight the
// companion can be swapped, not recalled.
(() => {
  'use strict';

  const LORE = window.MonstresLore;
  const bar = document.getElementById('team-bar');
  const root = document.documentElement;
  let game = null;
  const portraits = new Map();
  let shape = '';

  function portrait(species, look) {
    const key = `${species}/${look}`;
    if (!portraits.has(key)) {
      const canvas = document.createElement('canvas');
      canvas.width = canvas.height = 96;
      canvas.getContext('2d').putImageData(game.portrait(LORE.speciesOrder.indexOf(species), 96, look), 0, 0);
      portraits.set(key, canvas.toDataURL());
    }
    return portraits.get(key);
  }

  function build(team, fighting) {
    bar.replaceChildren(...team.map((m) => {
      const name = (LORE.species[m.species] || { name: m.species }).name;
      const element = LORE.elements[m.elements[0]] || { color: '#fff' };
      const button = document.createElement('button');
      button.type = 'button';
      button.className = 'team-icon';
      button.dataset.slot = m.slot;
      button.style.setProperty('--element', element.color);
      button.setAttribute('aria-pressed', String(m.active));
      button.disabled = m.active && fighting;  // not recalled in the middle of a fight
      const action = m.active ? (fighting ? 'au combat' : 'rappeler dans le cristal') : 'invoquer';
      button.title = `${name} · niv. ${m.level} — ${action}`;
      button.setAttribute('aria-label', button.title);
      const img = document.createElement('img');
      img.src = portrait(m.species, m.look);
      img.alt = '';
      const hp = document.createElement('span');
      hp.className = 'team-hp';
      hp.append(document.createElement('span'));
      const level = document.createElement('small');
      level.className = 'team-level';
      button.append(img, hp, level);
      button.addEventListener('pointerdown', (event) => event.preventDefault());  // no focus
      button.addEventListener('click', () => {
        if (!game || button.disabled) return;
        game.summon(m.slot);
        refresh();
      });
      return button;
    }));
    const columns = Math.ceil(team.length / 3);
    root.style.setProperty('--team-width', `${columns * 50 + 6}px`);
  }

  function refresh() {
    if (!game || typeof game.team !== 'function') return;
    const team = game.team();
    const fighting = game.battle().phase === 'fight';
    const key = team.map((m) => `${m.species}/${m.look}/${m.active}`).join() + fighting;
    if (key !== shape) {
      shape = key;
      build(team, fighting);
    }
    // Hit points and levels change more often.
    team.forEach((m, i) => {
      const icon = bar.children[i];
      if (!icon) return;
      const ratio = m.maxHp ? m.hp / m.maxHp : 0;
      const fill = icon.querySelector('.team-hp > span');
      fill.style.width = `${100 * ratio}%`;
      fill.style.background = ratio > 0.5 ? '#8fd16a' : ratio > 0.25 ? '#f2c94c' : '#ff7a6b';
      icon.querySelector('.team-level').textContent = m.level;
    });
  }

  const start = () => {
    game = window.monstres;
    refresh();
    setInterval(refresh, 200);
  };
  if (window.monstres && window.monstres.isReady) start();
  else window.addEventListener('monstres-ready', start, { once: true });
})();
