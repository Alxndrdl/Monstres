// Talking to the villagers: the dialogue box (bottom of the screen), with
// what they say and what the player can answer. Quests are given and
// finished here (the rules are in src/core/quests.cpp: the page only sends
// the player's choices). Also little notes at the top of the screen when a
// quest starts or ends, or a teleporter is found.
(() => {
  'use strict';

  const LORE = window.MonstresLore;
  const { $, el, whenReady } = window.MonstresUtil;
  const QUEST_INDEX = { lostPiou: 0, teleporters: 1, giantChampi: 2 };  // QuestId
  const MIRABELLE = 1;
  const GASPARD = 2;
  const LISON = 3;
  const BASTIEN = 4;
  const ODETTE = 5;

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

  // ---- Lison: the giant Champi of the forest ----
  function lison() {
    const q = quest('giantChampi');
    const s = q.steps;
    if (q.stage === 'done') {
      show('Le Champi géant… dans ton équipe ! Je n’en reviens toujours pas. Prends-en bien soin.',
        [['Encore un mot ?', () => villager(LISON)], ['Au revoir', close]]);
    } else if (q.stage === 'unknown' && s.again) {
      show('On raconte que le Champi géant est retourné au cœur de la forêt… Tu veux retenter ta chance ?', [
        ['J’y retourne', () => {
          game.acceptQuest(QUEST_INDEX.giantChampi);
          afterTick(() => show('Bonne chance ! Cette fois, il n’y aura rien à gagner… à part le plaisir de l’avoir avec toi.'));
        }, 'accept-quest'],
        ['Au revoir', close],
      ]);
    } else if (q.stage === 'unknown') {
      show('J’étais allée cueillir des champignons au cœur de la forêt… et j’ai vu un Champi gigantesque ! ' +
        'Plus grand que moi ! D’autres Champi l’entouraient, plus grands que d’habitude eux aussi. ' +
        'Tu crois que tu pourrais l’attraper ?', [
        ['Je vais le chercher', () => {
          game.acceptQuest(QUEST_INDEX.giantChampi);
          afterTick(() => show('Sois prudent : il est très costaud, et il n’est pas seul. ' +
            'Si tu connais un sort pour retenir tes coups, c’est le moment de t’en servir !'));
        }, 'accept-quest'],
        ['Au revoir', close],
      ]);
    } else if (q.ready) {
      show('C’est… c’est lui ! Tu as capturé le Champi géant ?', [
        ['Le lui montrer', () => {
          game.completeQuest(QUEST_INDEX.giantChampi);
          afterTick(() => show(s.rewarded ? 'Toujours aussi impressionnant ! Je suis contente qu’il soit revenu avec toi.'
            : 'Incroyable ! Personne au village ne voudra me croire. Merci, magicien !'));
        }, 'complete-quest'],
      ]);
    } else {
      show('Le Champi géant vit au cœur de la forêt, entouré des siens. Il est très costaud : affaiblis-le bien avant de le capturer.');
    }
  }

  // ---- Bastien: battles against his monsters ----
  function bastien(c) {
    const wins = c.wins || 0;
    const challenge = () => {
      game.challenge(BASTIEN);
      close();
    };
    const offer = c.challenge ? [['Combattre !', challenge, 'challenge']] : [];
    const why = !c.challenge ? ' Mais d’abord, fais sortir un monstre en forme de ton cristal.' : '';
    if (!wins) {
      show('Tu as un monstre, toi aussi ? Moi, c’est Champi, et j’en garde un autre en réserve. ' +
        'Un combat de dresseurs, ça te dit ? Gagne, et tu apprendras beaucoup !' + why,
        [...offer, ['Une autre fois', close]]);
    } else {
      show(`Tu m’as déjà battu ${wins === 1 ? 'une fois' : `${wins} fois`} ! On remet ça ? ` +
        'Pour l’honneur, cette fois : je n’ai plus rien à t’apprendre.' + why,
        [...offer, ['Une autre fois', close]]);
    }
  }

  // ---- Odette: she heals the team, and fills the magician's mana ----
  function odette() {
    const tired = game.team().filter((m) => m.hp < m.maxHp);
    const w = game.wizard();
    const drained = w.manaNow < w.mana;
    if (!tired.length && !drained) {
      show('Tes monstres sont en pleine forme, et ton bâton brille de tout son mana ! Reviens me voir si besoin.',
        [['Encore un mot ?', () => villager(ODETTE)], ['Au revoir', close]]);
      return;
    }
    const ko = tired.filter((m) => m.hp === 0).length;
    const ask = ko ? 'Oh, tes pauvres monstres ! Laisse-moi les soigner, ça ne coûte rien.'
      : tired.length ? 'Tes monstres ont l’air fatigués. Je les soigne ? C’est gratuit, bien sûr.'
        : 'Ton bâton a perdu de son éclat… Je te rends ton mana ? C’est gratuit, bien sûr.';
    show(tired.length && drained ? `${ask} Et je remplirai ton mana avec mon bâton.` : ask, [
      ['Oui, merci !', () => {
        game.healTeam(ODETTE);
        afterTick(() => show(tired.length ? 'Et voilà, ils sont comme neufs ! Prends soin d’eux.'
          : 'Et voilà, ton mana est plein ! Dépense-le bien.'));
      }, 'heal-team'],
      ['Non merci', close],
    ]);
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
    const giant = quests.find((q) => q.id === 'giantChampi');
    if (n !== LISON && giant && giant.stage !== 'done' && n % 2 === 0) add(LORE.rumors.giantChampi, 0);
    if (piou.stage !== 'done') add(LORE.rumors.lostPiou, 0);
    else if (n % 2 === 1) add(LORE.rumors.lostPiouDone, 0);  // some of them thank the player
    if (stones.stage !== 'done') { if (n % 2 === 0) add(LORE.rumors.teleporters, 1); }
    else if (n % 3 === 0) add(LORE.rumors.teleportersDone, 0);
    if (n !== BASTIEN && n % 3 === 1) add(LORE.rumors.bastien, 0);  // some of them send the player to him
    if (n !== ODETTE && n % 3 === 2) add(LORE.rumors.odette, 0);  // and to her
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
    else if (c.npc === LISON) lison();
    else if (c.npc === BASTIEN) bastien(c);
    else if (c.npc === ODETTE) odette();
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
  let before = null;  // the quests as they were
  // Another world is played (saves.js): its teleporters and quests are not news.
  window.addEventListener('monstres-world', () => {
    known = null;
    before = null;
    close();
  });
  function watchWorld() {
    if (!game || typeof game.quests !== 'function') return;
    const t = game.teleporters().list.filter((x) => x.known).map((x) => x.place);
    const quests = game.quests();
    if (known) {
      for (const place of t) {
        if (!known.includes(place)) note('Téléporteur découvert', LORE.teleporters[place]);
      }
      quests.forEach((x, i) => {
        const info = LORE.quests[x.id];
        const was = before[i];
        if (was.stage === 'unknown' && x.stage === 'active') note('Nouvelle quête', info.title);
        // The giant Champi's second time gives nothing.
        const xp = was.steps && was.steps.rewarded ? '' : ` · +${info.xp} XP`;
        if (was.stage !== 'done' && x.stage === 'done') note('Quête terminée !', `${info.title}${xp}`);
      });
    }
    known = t;
    before = quests;
  }

  const start = () => {
    game = window.monstres;
    watchWorld();
    setInterval(watchWorld, 300);
  };
  whenReady(start);
  window.monstresDialogue = { open, close, isOpen: () => !box.hidden };
})();
