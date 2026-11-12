// Talking to the villagers: the dialogue box (bottom of the screen), with
// what they say and what the player can answer. Quests are given and
// finished here (the rules are in src/core/quests.cpp: the page only sends
// the player's choices). Also little notes at the top of the screen when a
// quest starts or ends, or a teleporter is found.
(() => {
  'use strict';

  const LORE = window.MonstresLore;
  const $ = (id) => document.getElementById(id);
  const el = (tag, props = {}, children = []) => {
    const node = Object.assign(document.createElement(tag), props);
    for (const child of children) node.append(child);
    return node;
  };
  const QUEST_INDEX = { lostPiou: 0, teleporters: 1 };  // QuestId
  const MIRABELLE = 1;
  const GASPARD = 2;

  let game = null;
  const box = $('dialogue');
  const speaker = box.querySelector('.dialogue-name');
  const text = box.querySelector('.dialogue-text');
  const answers = box.querySelector('.dialogue-answers');
  let talking = null;  // { index, npc }
  let watch = 0;
  const said = {};  // how many times each villager talked: they go through their lines

  const quest = (id) => game.quests().find((q) => q.id === id);
  const npcName = (n) => (LORE.npcs[n] || { name: 'Villageois' }).name;

  function close() {
    talking = null;
    box.hidden = true;
    clearInterval(watch);
  }

  // One step of the dialogue: what the villager says, and the answers.
  function show(line, choices = [['Au revoir', close]]) {
    text.textContent = line;
    answers.replaceChildren(...choices.map(([label, action, id]) => {
      const b = el('button', { type: 'button', className: 'button', textContent: label });
      if (id) b.id = id;
      b.addEventListener('click', action);
      return b;
    }));
  }

  // After a command: the game applies it with the next tick (at once when frozen).
  const afterTick = (f) => setTimeout(f, 60);

  // ---- Mirabelle: her lost Piou ----
  function presentMonster(onPick) {
    const team = game.team();
    show('Montre-moi ce monstre…', [
      ...team.map((m) => [`${(LORE.species[m.species] || { name: m.species }).name} (niv. ${m.level})`, () => onPick(m),
        `present-${m.slot}`]),
      ['Rien, finalement', mirabelle],
    ]);
  }

  function judge(m) {
    if (m.tag === 'lostPiou') {
      if (game.team().length < 2) {
        show('C’est lui ! Mais… si tu me le rends, tu n’auras plus aucun monstre avec toi. Capture d’abord un autre monstre.');
        return;
      }
      game.giveMonster(m.slot);
      afterTick(() => show('Émeraude ! Mon petit Piou ! Merci, merci mille fois ! Je ne le quitterai plus des yeux.'));
    } else if (m.species === 'bird' && m.variants.includes('plumage.emerald')) {
      show('Il a la même couleur… mais ce n’est pas le mien. Je reconnaîtrais Émeraude entre mille !',
        [['Montrer un autre monstre', () => presentMonster(judge)], ['Au revoir', close]]);
    } else if (m.species === 'bird') {
      show('Ce n’est pas mon Piou ! Le mien a un plumage d’une couleur très rare : vert émeraude.',
        [['Montrer un autre monstre', () => presentMonster(judge)], ['Au revoir', close]]);
    } else {
      show('Ça ? Mais ce n’est même pas un Piou !', [['Montrer un autre monstre', () => presentMonster(judge)], ['Au revoir', close]]);
    }
  }

  function mirabelle() {
    const q = quest('lostPiou');
    if (q.stage === 'done') {
      show('Émeraude ne me quitte plus d’une plume. Merci encore, petit magicien !');
    } else if (q.ready) {
      show('Ce plumage vert… C’est mon Piou ! Tu l’as retrouvé ?', [
        ['Lui rendre son Piou', () => presentMonster(judge), 'give-piou'],
        ['Pas maintenant', close],
      ]);
    } else if (q.stage === 'unknown') {
      show('J’ai perdu mon Piou… J’y tiens tellement ! Il a un plumage vert émeraude, une couleur très rare. ' +
        'Il doit errer autour du village, il a peur de tout.', [
        ['Je vais le retrouver', () => {
          game.acceptQuest(QUEST_INDEX.lostPiou);
          afterTick(() => show('Oh, merci ! Attention, il s’enfuit dès qu’on approche. Il faudra sans doute l’affaiblir pour le capturer.'));
        }, 'accept-quest'],
        ['Montrer un monstre', () => presentMonster(judge)],
        ['Au revoir', close],
      ]);
    } else {
      show('Tu as trouvé mon Piou ? Il a un plumage vert émeraude, tu ne peux pas le manquer.', [
        ['Montrer un monstre', () => presentMonster(judge)],
        ['Pas encore', close],
      ]);
    }
  }

  // ---- Gaspard: the circle of stones ----
  function gaspard() {
    const q = quest('teleporters');
    const s = q.steps;
    if (q.stage === 'done') {
      show('Des téléporteurs ! Dire qu’ils étaient là depuis toujours… Demain, je vais visiter le château !');
    } else if (q.stage === 'unknown') {
      show('Ce cercle de pierres m’intrigue. Ses runes brillent quand on s’en approche… ' +
        'J’en ai vu un tout pareil près du château, au sud. Tu sais à quoi ça sert ?', [
        ['Je vais le découvrir', () => {
          game.acceptQuest(QUEST_INDEX.teleporters);
          afterTick(() => show('Formidable ! Va voir celui du château, et reviens me montrer ce que tu as trouvé.'));
        }, 'accept-quest'],
        ['Au revoir', close],
      ]);
    } else if (q.ready) {
      show('Alors ? Je t’ai vu disparaître dans un éclair de lumière… et revenir !', [
        ['Tout raconter', () => {
          game.completeQuest(QUEST_INDEX.teleporters);
          afterTick(() => show('Des téléporteurs ! On se tient au milieu, et hop, on est à l’autre bout du pays ! Merci, magicien !'));
        }, 'complete-quest'],
      ]);
    } else if (!s.discover) {
      show('Va voir le cercle de pierres près du château, au sud. Peut-être qu’il y a un lien entre les deux ?');
    } else if (!s.go) {
      show('Tu as trouvé celui du château ? Alors montre-moi ! Mets-toi au milieu de ce cercle-ci et touche-le…');
    } else {
      show('Te voilà au château, j’imagine… Reviens par le même chemin, que je voie ça !');
    }
  }

  // ---- The other villagers: rumors about the quests, and chatter ----
  function villager(n) {
    const quests = game.quests();
    const pool = [];
    const add = (lines, k) => {
      if (lines && lines.length) pool.push(lines[(n + k) % lines.length]);
    };
    const piou = quests.find((q) => q.id === 'lostPiou');
    const stones = quests.find((q) => q.id === 'teleporters');
    if (piou.stage !== 'done') add(LORE.rumors.lostPiou, 0);
    else if (n % 2 === 1) add(LORE.rumors.lostPiouDone, 0);  // some of them thank the player
    if (stones.stage !== 'done') { if (n % 2 === 0) add(LORE.rumors.teleporters, 1); }
    else if (n % 3 === 0) add(LORE.rumors.teleportersDone, 0);
    for (const line of (LORE.npcs[n] || {}).chatter || []) pool.push(line);
    const k = said[n] = (said[n] || 0) + 1;
    show(pool[(k - 1) % pool.length] || 'Bonjour !', [['Encore un mot ?', () => villager(n)], ['Au revoir', close]]);
  }

  function open(c) {
    if (!game || !c || !c.npc) return;
    talking = { index: c.index, npc: c.npc };
    speaker.textContent = npcName(c.npc);
    box.hidden = false;
    if (c.npc === MIRABELLE) mirabelle();
    else if (c.npc === GASPARD) gaspard();
    else villager(c.npc);
    // Walking away ends the talk.
    clearInterval(watch);
    watch = setInterval(() => {
      const now = talking && game.selected();
      if (!talking) return;
      if (!now || now.npc !== talking.npc || now.distance > 9) close();
    }, 400);
  }

  window.addEventListener('keydown', (event) => {
    if (event.key === 'Escape' && !box.hidden) close();
  });

  // ---- Notes: a quest starts or ends, a teleporter is found ----
  const toast = $('toast');
  let toastTimer = 0;
  function note(title, detail) {
    toast.replaceChildren(el('strong', { textContent: title }), ...(detail ? [el('small', { textContent: detail })] : []));
    toast.hidden = false;
    toast.classList.remove('show');
    void toast.offsetWidth;
    toast.classList.add('show');
    clearTimeout(toastTimer);
    toastTimer = setTimeout(() => { toast.hidden = true; }, 3500);
  }
  let known = null;
  let stages = null;
  function watchWorld() {
    if (!game || typeof game.quests !== 'function') return;
    const t = game.teleporters().list.filter((x) => x.known).map((x) => x.place);
    const q = game.quests().map((x) => x.stage);
    if (known) {
      for (const place of t) {
        if (!known.includes(place)) note('Téléporteur découvert', LORE.teleporters[place]);
      }
      game.quests().forEach((x, i) => {
        const info = LORE.quests[x.id];
        if (stages[i] === 'unknown' && x.stage === 'active') note('Nouvelle quête', info.title);
        if (stages[i] !== 'done' && x.stage === 'done') note('Quête terminée !', `${info.title} · +${info.xp} XP`);
      });
    }
    known = t;
    stages = q;
  }

  const start = () => {
    game = window.monstres;
    watchWorld();
    setInterval(watchWorld, 300);
  };
  if (window.monstres && window.monstres.isReady) start();
  else window.addEventListener('monstres-ready', start, { once: true });
  window.monstresDialogue = { open, close, isOpen: () => !box.hidden };
})();
