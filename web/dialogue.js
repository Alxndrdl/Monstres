// Talking to the villagers: the dialogue box (bottom of the screen), with
// what they say and what the player can answer. Quests are given and
// finished here (the rules are in src/core/quests.cpp: the page only sends
// the player's choices). Also little notes at the top of the screen when a
// quest starts or ends, or a teleporter is found.
(() => {
  'use strict';

  const LORE = window.MonstresLore;
  const { $, el, whenReady } = window.MonstresUtil;
  const QUEST_INDEX = { lostPiou: 0, teleporters: 1, giantChampi: 2, combat: 3, healing: 4, main: 5,
    merchant: 6, delivery: 7, transfer: 8, orchard: 9, shepherd: 10, mill: 11 };  // QuestId
  const MIRABELLE = 1;
  const GASPARD = 2;
  const LISON = 3;
  const BASTIEN = 4;
  const ODETTE = 5;
  const ISIDORE = 9;
  const ANSELME = 10;
  const CAPUCINE = 11;
  const VIOLETTE = 12;
  const BASILE = 13;
  const FIRMIN = 14;
  const ROMANE = 15;
  const LUCIEN = 16;
  const YVON = 17;

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

  // ---- Bastien: battles against his monsters (his lesson: win one) ----
  function bastien(c) {
    const wins = c.wins || 0;
    const lesson = quest('combat');
    const challenge = () => {
      game.challenge(BASTIEN);
      close();
    };
    const offer = c.challenge ? [['Combattre !', challenge, 'challenge']] : [];
    const why = !c.challenge ? ' Mais d’abord, fais sortir un monstre en forme de ton cristal.' : '';
    if (lesson.stage === 'unknown') {
      show('Toi, tu es le nouvel apprenti d’Isidore ! Un magicien doit savoir mener ses monstres au combat : ' +
        'attaquer, esquiver avec Espace, parer avec F. Je te propose une initiation : un vrai combat de dresseurs, contre moi.', [
        ['J’accepte', () => {
          game.acceptQuest(QUEST_INDEX.combat);
          afterTick(() => bastien(game.selected() || c));
        }, 'accept-quest'],
        ['Une autre fois', close],
      ]);
      return;
    }
    if (lesson.stage === 'active' && lesson.ready) {
      show('Bien joué ! Tu as su lire mes attaques et y répondre. Tu as tout compris.', [
        ['Merci, Bastien', () => {
          game.completeQuest(QUEST_INDEX.combat);
          afterTick(() => show('Reviens quand tu veux pour une revanche. Et passe le bonjour à Isidore !'));
        }, 'complete-quest'],
      ]);
      return;
    }
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

  // ---- Odette: she heals the team, and fills the magician's mana (her lesson: the Heal spell) ----
  function odette() {
    const lesson = quest('healing');
    if (lesson.stage === 'unknown') {
      show('Bonjour, petit magicien. Je soigne les monstres du village… mais je ne serai pas toujours là quand les tiens ' +
        'seront blessés. Ton bâton connaît le sort de Soin : veux-tu que je t’apprenne à t’en servir ?', [
        ['Oui, apprends-moi', () => {
          game.acceptQuest(QUEST_INDEX.healing);
          afterTick(() => show('Quand un de tes monstres est blessé, lance le sort de Soin : le bouton à côté des attaques, ' +
            'ou la touche R. Ça coûte un peu de mana. Reviens me voir quand ce sera fait !'));
        }, 'accept-quest'],
        ['Plus tard', close],
      ]);
      return;
    }
    if (lesson.stage === 'active' && lesson.ready) {
      show('Je l’ai vu, ton monstre va déjà mieux ! Tu sais soigner, maintenant.', [
        ['Merci, Odette', () => {
          game.completeQuest(QUEST_INDEX.healing);
          afterTick(() => odette());
        }, 'complete-quest'],
      ]);
      return;
    }
    healer(ODETTE);
  }

  // A healer (Odette, Violette at the farm): she heals the team, and fills the magician's mana.
  function healer(n) {
    const tired = game.team().filter((m) => m.hp < m.maxHp);
    const w = game.wizard();
    const drained = w.manaNow < w.mana;
    if (!tired.length && !drained) {
      show('Tes monstres sont en pleine forme, et ton bâton brille de tout son mana ! Reviens me voir si besoin.',
        [['Encore un mot ?', () => villager(n)], ['Au revoir', close]]);
      return;
    }
    const ko = tired.filter((m) => m.hp === 0).length;
    const ask = ko ? 'Oh, tes pauvres monstres ! Laisse-moi les soigner, ça ne coûte rien.'
      : tired.length ? 'Tes monstres ont l’air fatigués. Je les soigne ? C’est gratuit, bien sûr.'
        : 'Ton bâton a perdu de son éclat… Je te rends ton mana ? C’est gratuit, bien sûr.';
    show(tired.length && drained ? `${ask} Et je remplirai ton mana avec mon bâton.` : ask, [
      ['Oui, merci !', () => {
        game.healTeam(n);
        afterTick(() => show(tired.length ? 'Et voilà, ils sont comme neufs ! Prends soin d’eux.'
          : 'Et voilà, ton mana est plein ! Dépense-le bien.'));
      }, 'heal-team'],
      ['Non merci', close],
    ]);
  }

  // ---- Isidore, the old magician: the main quest ----
  function isidore() {
    const q = quest('main');
    const s = q.steps;
    const name = (game.wizard().name || '').trim() || 'jeune magicien';
    if (q.stage === 'unknown') {
      show(`Ah, te voilà enfin, ${name} ! Je suis Isidore, le magicien du village. Ton cristal brille bien… ` +
        'Avant de t’enseigner la vraie magie, il te faut apprendre les bases. Les villageois ont besoin d’aide, ' +
        'et chacun t’apprendra quelque chose : Mirabelle a perdu son Piou, Gaspard s’intéresse aux cercles de pierres, ' +
        'Bastien t’apprendra à combattre, et Odette à soigner.', [
        ['Je m’y mets !', () => {
          game.acceptQuest(QUEST_INDEX.main);
          afterTick(() => show('Parfait. Reviens me voir quand tu les auras tous aidés. Ton grimoire (touche J) garde la liste.'));
        }, 'accept-quest'],
        ['Plus tard', close],
      ]);
    } else if (q.stage === 'active' && q.ready) {
      show('Tout le village ne parle que de toi ! Tu as aidé chacun d’eux.', [
        ['Raconter', () => {
          game.completeQuest(QUEST_INDEX.main);
          afterTick(() => show(`Tu as bien appris, ${name}. Je suis fier de toi. La suite de ton apprentissage ` +
            'viendra bientôt… En attendant, explore le pays, et prends soin de tes monstres.'));
        }, 'complete-quest'],
      ]);
    } else if (q.stage === 'active') {
      const left = [
        [s.lostPiou, 'retrouver le Piou de Mirabelle'], [s.teleporters, 'aider Gaspard avec les cercles de pierres'],
        [s.combat, 'l’initiation au combat de Bastien'], [s.healing, 'l’initiation aux soins d’Odette'],
      ].filter(([done]) => !done).map(([, what]) => what);
      show(`Il te reste à ${left.join(', ').replace(/, ([^,]*)$/, ' et $1')}. Courage !`,
        [['Encore un mot ?', () => villager(ISIDORE)], ['Au revoir', close]]);
    } else {
      isidoreWest(name);
    }
  }

  // The main quest goes on: the strange stone for the farm, then what Capucine asks.
  function isidoreWest(name) {
    const delivery = quest('delivery');
    const transfer = quest('transfer');
    const orchard = quest('orchard');
    const more = [['Encore un mot ?', () => villager(ISIDORE)], ['Au revoir', close]];
    if (delivery.stage === 'unknown') {
      show(`J’ai une mission pour toi, ${name}. Prends ce colis : une pierre bien étrange, et une lettre. ` +
        'Porte-les à Capucine, qui tient la grande exploitation agricole, à l’ouest. Suis la route qui part du sentier, ' +
        'un peu au sud du village. Et ne l’égare pas : cette pierre est précieuse !', [
        ['Je m’en charge', () => {
          game.acceptQuest(QUEST_INDEX.delivery);
          afterTick(() => show('Merci. Tu trouveras la pierre dans ton sac (touche I). Capucine saura quoi en faire.'));
        }, 'accept-quest'],
        ['Plus tard', close],
      ]);
    } else if (delivery.stage === 'active') {
      show('As-tu porté la pierre à Capucine ? Son exploitation est à l’ouest, au bout de la route qui part du sentier.', more);
    } else if (transfer.stage !== 'done') {
      show('Capucine m’a écrit : la pierre est bien arrivée. Fais ce qu’elle te demande, elle en sait long sur les monstres.', more);
    } else if (orchard.stage !== 'done') {
      show('Des Chipou agressifs dans le verger de Capucine ? Sois prudent : ils attaquent sans prévenir. ' +
        'Si les choses tournent mal, ta magie te ramènera à l’exploitation.', more);
    } else {
      show(`Le verger sauvé, les monstres de Capucine en sécurité… Tu deviens un vrai magicien, ${name}. ` +
        'La suite de ton apprentissage viendra bientôt. Explore l’ouest en attendant !', more);
    }
  }

  // ---- Anselme: his overturned cart; then he sells mana potions ----
  function anselme() {
    const q = quest('merchant');
    const s = q.steps;
    if (q.stage === 'unknown') {
      show('Aïe, aïe, aïe… Ma charrette s’est renversée dans le fossé, et mes ballots se sont éparpillés partout ! ' +
        'Tu voudrais bien m’aider à les retrouver ? Ils ne doivent pas être bien loin.', [
        ['Je vais les chercher', () => {
          game.acceptQuest(QUEST_INDEX.merchant);
          afterTick(() => show('Merci, merci ! Il y en a trois. Regarde autour de la charrette, dans l’herbe.'));
        }, 'accept-quest'],
        ['Au revoir', close],
      ]);
    } else if (q.stage === 'active' && q.ready) {
      show('Mes trois ballots ! Tu es un amour. Aide-moi à remettre la charrette sur ses roues…', [
        ['Les lui rendre', () => {
          game.completeQuest(QUEST_INDEX.merchant);
          afterTick(() => show('Et hop ! Tiens, des pièces et deux potions de mana, pour ta peine. Et reviens me voir : ' +
            'je vends mes potions à qui en veut !'));
        }, 'complete-quest'],
      ]);
    } else if (q.stage === 'active') {
      show(`Tu en as trouvé ${s.goods} sur ${s.of}. Les autres doivent traîner dans l’herbe, autour de la charrette.`);
    } else {
      shop();
    }
  }

  function shop() {
    const sh = game.shop();
    const potion = sh.items.find((it) => it.id === 'manaPotion');
    const coins = sh.coins;
    const line = `Potions de mana, ${potion.price} pièces la fiole ! Tu as ${coins} ${coins > 1 ? 'pièces' : 'pièce'} ` +
      `et ${potion.have} ${potion.have > 1 ? 'potions' : 'potion'}.`;
    show(line, [
      [`Acheter une potion (${potion.price})`, () => {
        if (game.shop().coins < potion.price) {
          show('Ah… Il te manque quelques pièces. Les monstres battus en laissent tomber, tu sais.',
            [['D’accord', shop], ['Au revoir', close]]);
          return;
        }
        game.buy(potion.item);
        afterTick(shop);
      }, 'buy-potion'],
      ['Encore un mot ?', () => villager(ANSELME)],
      ['Au revoir', close],
    ]);
  }

  // ---- Capucine: the strange stone, the transfer, the orchard; the farm's monsters ----
  const LETTER = '« Chère Capucine, voici la pierre de transfert dont nous avions parlé. Elle est liée à la magie de ' +
    'mon apprenti : désormais, chaque monstre qu’il capturera alors que son équipe compte déjà six compagnons sera ' +
    'transporté jusqu’à toi. Prends-en soin comme des tiens, et laisse-le venir les chercher quand il le voudra. ' +
    'Ton vieil ami, Isidore. »';
  function capucine() {
    const delivery = quest('delivery');
    const transfer = quest('transfer');
    const orchard = quest('orchard');
    const farmButton = [['Voir mes monstres', () => { close(); openFarm(); }, 'open-farm']];
    if (delivery.stage === 'active') {
      show('Un colis d’Isidore ? Pour moi ? Voyons voir…', [
        ['Lui remettre la pierre', () => {
          game.completeQuest(QUEST_INDEX.delivery);
          afterTick(() => show(`Il y a une lettre : ${LETTER}`, [['Une pierre de transfert ?', capucine]]));
        }, 'complete-quest'],
      ]);
      return;
    }
    if (delivery.stage !== 'done') {
      show('Bienvenue à l’exploitation ! Mes vaches sont parties, mais il reste les pommes, le blé… et les Chipou, hélas.',
        [['Encore un mot ?', () => villager(CAPUCINE)], ['Au revoir', close]]);
      return;
    }
    if (transfer.stage === 'unknown') {
      show('La pierre est posée dans la grange, elle bourdonne doucement. Il faudrait vérifier qu’elle marche ! ' +
        'Capture un monstre alors que ton équipe est complète : il devrait apparaître ici. Ensuite, viens le récupérer.', [
        ['Essayons !', () => {
          game.acceptQuest(QUEST_INDEX.transfer);
          afterTick(() => show('Parfait. Six monstres avec toi, puis un septième capturé : il viendra chez moi.'));
        }, 'accept-quest'],
        ['Plus tard', close],
      ]);
      return;
    }
    if (transfer.stage === 'active') {
      if (transfer.ready) {
        show('Ça marche ! Ton monstre est arrivé dans un éclair, et tu as pu le reprendre. Isidore est vraiment doué.', [
          ['Formidable', () => {
            game.completeQuest(QUEST_INDEX.transfer);
            afterTick(capucine);
          }, 'complete-quest'],
        ]);
      } else if (transfer.steps.sent) {
        show('Ton monstre est arrivé ! Il broute dans l’enclos. Tu veux le reprendre ? Choisis qui laisser à sa place.',
          [...farmButton, ['Plus tard', close]]);
      } else {
        show('Rien n’est encore arrivé. Il faut capturer un monstre alors que ton équipe en compte déjà six.',
          [...farmButton, ['Au revoir', close]]);
      }
      return;
    }
    if (orchard.stage === 'unknown') {
      show('Oh non, regarde ! Des Chipou dans mon verger ! Une bande entière, et ils sont agressifs : ils attaquent ' +
        'quiconque s’approche. Tu veux bien les chasser ? Si ça tourne mal, ta magie te ramènera ici, près de Violette.', [
        ['Je m’en occupe', () => {
          game.acceptQuest(QUEST_INDEX.orchard);
          afterTick(() => show('Sois prudent ! Fais soigner tes monstres par Violette avant d’y aller.'));
        }, 'accept-quest'],
        ['Plus tard', close],
      ]);
      return;
    }
    if (orchard.stage === 'active') {
      if (orchard.ready) {
        show('Plus un Chipou dans le verger ! Tu es un héros. Tiens, des pièces et des potions : tu l’as bien mérité.', [
          ['Merci, Capucine', () => {
            game.completeQuest(QUEST_INDEX.orchard);
            afterTick(capucine);
          }, 'complete-quest'],
        ]);
      } else {
        show(`Il reste des Chipou dans le verger : ${orchard.steps.of - orchard.steps.beaten} encore. Courage !`,
          [...farmButton, ['Au revoir', close]]);
      }
      return;
    }
    const kept = game.farm().kept.length;
    show(kept ? `Tes ${kept} monstres vont bien, ils mangent comme quatre ! Tu veux en échanger ?`
      : 'L’enclos est prêt pour tes monstres. Capture-en avec une équipe complète, ils viendront ici.',
    [...farmButton, ['Encore un mot ?', () => villager(CAPUCINE)], ['Au revoir', close]]);
  }

  // ---- The farm's monsters (Capucine): swap the team's with the farm's ----
  const farmPanel = $('farm-panel');
  let farmPick = { team: -1, kept: -1 };
  function openFarm() {
    farmPick = { team: -1, kept: -1 };
    farmPanel.hidden = false;
    if (game) game.setPaused(true);
    renderFarm();
  }
  function closeFarm() {
    farmPanel.hidden = true;
    if (game) game.setPaused(false);
  }
  function renderFarm() {
    const f = game.farm();
    const portrait = window.MonstresMenu && window.MonstresMenu.portraitUrl;
    const entry = (m, side) => {
      const info = LORE.species[m.species] || { name: m.species };
      const b = el('button', { type: 'button' }, [
        ...(portrait ? [el('img', { alt: '', src: portrait(LORE.speciesOrder.indexOf(m.species), 80, m.look) })] : []),
        el('span', {}, [el('strong', { textContent: info.name }), el('small', { textContent: `niv. ${m.level} · ${m.hp}/${m.maxHp} PV` })]),
      ]);
      b.id = `farm-${side}-${m.index}`;
      b.setAttribute('aria-pressed', String(farmPick[side] === m.index));
      b.addEventListener('click', () => {
        farmPick[side] = farmPick[side] === m.index ? -1 : m.index;
        renderFarm();
      });
      return el('li', {}, [b]);
    };
    $('farm-team').replaceChildren(...f.team.map((m) => entry(m, 'team')));
    $('farm-kept').replaceChildren(...(f.kept.length ? f.kept.map((m) => entry(m, 'kept'))
      : [el('li', { className: 'facts', textContent: 'Personne pour l’instant.' })]));
    $('farm-kept-title').textContent = `À l’exploitation (${f.kept.length})`;
    $('farm-swap').disabled = !(f.usable && farmPick.team >= 0 && farmPick.kept >= 0);
    $('farm-leave').disabled = !(f.usable && farmPick.team >= 0 && farmPick.kept < 0 && f.team.length > 1);
    $('farm-take').disabled = !(f.usable && farmPick.kept >= 0 && farmPick.team < 0 && f.team.length < f.teamSize);
    $('farm-hint').textContent = f.usable ? 'Choisis un monstre de ton équipe et/ou un monstre de l’exploitation.'
      : 'Il faut être près de Capucine pour échanger tes monstres.';
  }
  const farmDo = (slot, kept) => {
    game.farmSwap(slot, kept);  // paused: applied at once
    farmPick = { team: -1, kept: -1 };
    setTimeout(renderFarm, 30);
  };
  $('farm-swap').addEventListener('click', () => farmDo(farmPick.team, farmPick.kept));
  $('farm-leave').addEventListener('click', () => farmDo(farmPick.team, -1));
  $('farm-take').addEventListener('click', () => farmDo(-1, farmPick.kept));
  $('farm-close').addEventListener('click', closeFarm);

  // ---- Violette: the farm's healer ----
  function violette() { healer(VIOLETTE); }

  // ---- Basile: the shepherd and his lost lambs ----
  function basile() {
    const q = quest('shepherd');
    const s = q.steps;
    if (q.stage === 'unknown') {
      show('Bonjour, magicien. Trois de mes agneaux se sont sauvés dans les collines… Ils ont peur de tout, mais ils ' +
        'suivent volontiers quelqu’un de gentil. Tu pourrais me les ramener jusqu’à l’enclos ?', [
        ['Je vais les chercher', () => {
          game.acceptQuest(QUEST_INDEX.shepherd);
          afterTick(() => show('Merci ! Approche-toi doucement d’eux : ils te suivront. Ramène-les jusque dans l’enclos.'));
        }, 'accept-quest'],
        ['Au revoir', close],
      ]);
    } else if (q.stage === 'active' && q.ready) {
      if (!s.room) {
        show('Tous mes agneaux sont rentrés ! Je voulais te confier un de mes Laini… mais ton équipe est pleine. ' +
          'Fais-lui de la place et reviens me voir.');
        return;
      }
      show('Tous mes agneaux sont rentrés ! Pour te remercier, je te confie un de mes Laini. Prends-en bien soin.', [
        ['Merci, Basile', () => {
          game.completeQuest(QUEST_INDEX.shepherd);
          afterTick(() => show('Il s’appelle… comme tu voudras ! Il adore qu’on lui gratte la laine.'));
        }, 'complete-quest'],
      ]);
    } else if (q.stage === 'active') {
      show(`${s.lambs} sur ${s.of} sont rentrés. Les autres errent dans les collines, plus loin de l’enclos.`);
    } else {
      villager(BASILE);
    }
  }

  // ---- Romane: a trainer in the woods ----
  function romane(c) {
    const wins = c.wins || 0;
    const challenge = () => {
      game.challenge(ROMANE);
      close();
    };
    const offer = c.challenge ? [['Combattre !', challenge, 'challenge']] : [];
    const why = !c.challenge ? ' Mais d’abord, fais sortir un monstre en forme.' : '';
    show(wins ? `Encore toi ! Tu m’as battue ${wins === 1 ? 'une fois' : `${wins} fois`}. Une revanche ?${why}`
      : 'Un magicien ! Ça tombe bien, je m’ennuyais. Ma Hulou, mon Chipou et mon Zapou ne demandent qu’à se battre. ' +
        `Trois contre toi : tu oses ?${why}`, [...offer, ['Une autre fois', close]]);
  }

  // ---- A trainer passing by: a battle, once ----
  function wanderer(c) {
    const challenge = () => {
      game.challenge(c.npc);
      close();
    };
    if (c.beaten) {
      show('Quel combat ! Tu m’as battu à la loyale. Je reprends la route… Peut-être qu’on se recroisera !');
      return;
    }
    const offer = c.challenge ? [['Combattre !', challenge, 'challenge']] : [];
    const why = c.challenge ? '' : ' Mais d’abord, fais sortir un monstre en forme.';
    show(`Salut ! Je suis dresseur, je voyage de région en région. Un petit combat, ça te dit ?${why}`,
      [...offer, ['Une autre fois', close]]);
  }

  // ---- Lucien: the mill that stopped ----
  function lucien() {
    const q = quest('mill');
    if (q.stage === 'unknown') {
      show('Le vent est tombé il y a une semaine, et mes ailes se sont coincées. Plus moyen de les faire repartir ! ' +
        'Il faudrait une bonne bourrasque… Un monstre de vent, qui vole, y arriverait peut-être ?', [
        ['Je vais essayer', () => {
          game.acceptQuest(QUEST_INDEX.mill);
          afterTick(() => show('Fais-le s’entraîner devant le moulin : qu’il vise les ailes, pas toi, avec son attaque à distance !'));
        }, 'accept-quest'],
        ['Au revoir', close],
      ]);
    } else if (q.stage === 'active' && q.ready) {
      show('Elles tournent ! Elles tournent ! Ah, quel bonheur d’entendre ce grincement…', [
        ['Bonne mouture !', () => {
          game.completeQuest(QUEST_INDEX.mill);
          afterTick(() => show('Tiens, pour toi. Et passe quand tu veux : il y aura toujours une miche de pain chaud.'));
        }, 'complete-quest'],
      ]);
    } else if (q.stage === 'active') {
      show('Un monstre de vent qui vole, comme une Hulou ou un Croa : entraîne-le devant les ailes, et vise-les sans viser ton magicien.');
    } else {
      villager(LUCIEN);
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
    const giant = quests.find((q) => q.id === 'giantChampi');
    if (n !== LISON && giant && giant.stage !== 'done' && n % 2 === 0) add(LORE.rumors.giantChampi, 0);
    if (piou.stage !== 'done') add(LORE.rumors.lostPiou, 0);
    else if (n % 2 === 1) add(LORE.rumors.lostPiouDone, 0);  // some of them thank the player
    if (stones.stage !== 'done') { if (n % 2 === 0) add(LORE.rumors.teleporters, 1); }
    else if (n % 3 === 0) add(LORE.rumors.teleportersDone, 0);
    if (n !== BASTIEN && n % 3 === 1) add(LORE.rumors.bastien, 0);  // some of them send the player to him
    if (n !== ODETTE && n % 3 === 2 && n <= ISIDORE) add(LORE.rumors.odette, 0);  // and to her
    if (n <= ISIDORE && n % 2 === 1) add(LORE.rumors.west, 0);
    for (const line of (LORE.npcs[n] || {}).chatter || []) pool.push(line);
    const k = said[n] = (said[n] || 0) + 1;
    show(pool[(k - 1) % pool.length] || 'Bonjour !', [['Encore un mot ?', () => villager(n)], ['Au revoir', close]]);
  }

  // what: 'shop' goes straight to Anselme's shop (the actions' menu, battle.js).
  function open(c, what) {
    if (!game || !c || !c.npc) return;
    talking = { index: c.index, npc: c.npc };
    speaker.textContent = npcName(c.npc);
    box.hidden = false;
    if (c.npc === MIRABELLE) mirabelle();
    else if (c.npc === GASPARD) gaspard();
    else if (c.npc === LISON) lison();
    else if (c.npc === BASTIEN) bastien(c);
    else if (c.npc === ODETTE) odette();
    else if (c.npc === ISIDORE) isidore();
    else if (c.npc === ANSELME) { if (what === 'shop') shop(); else anselme(); }
    else if (c.npc === CAPUCINE) capucine();
    else if (c.npc === VIOLETTE) violette();
    else if (c.npc === BASILE) basile();
    else if (c.npc === ROMANE) romane(c);
    else if (c.npc === LUCIEN) lucien();
    else if (c.wanderer) wanderer(c);
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
    if (event.key === 'Escape' && !farmPanel.hidden) {
      event.stopImmediatePropagation();
      closeFarm();
    } else if (event.key === 'Escape' && !box.hidden) close();
  }, true);

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
  let lastRescue = null;
  let before = null;  // the quests as they were
  // Another world is played (saves.js): its teleporters and quests are not news.
  // After the arrival of a new game: Isidore waits (said once, when the flight is over).
  let welcomed = true;
  window.addEventListener('monstres-created', () => { welcomed = false; });
  window.addEventListener('monstres-world', () => {
    known = null;
    before = null;
    lastRescue = null;
    close();
    if (!farmPanel.hidden) closeFarm();
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
    // Sent back to the farm by an aggressive monster.
    const rescue = game.battle().rescueTick;
    if (lastRescue !== null && rescue > lastRescue) {
      note('Ramené à l’exploitation !', 'Ta magie t’a téléporté loin du danger. Violette peut soigner tes monstres.');
    }
    lastRescue = rescue;
    // After the arrival: Isidore waits.
    const main = quests.find((q) => q.id === 'main');
    if (!welcomed && main && main.stage === 'unknown' && !game.introOn() && !game.needsCreation()) {
      welcomed = true;
      note('Bienvenue !', 'Isidore, le vieux magicien, t’attend sur la place du village.');
    }
  }

  const start = () => {
    game = window.monstres;
    watchWorld();
    setInterval(watchWorld, 300);
  };
  whenReady(start);
  window.monstresDialogue = { open, close, isOpen: () => !box.hidden, openFarm, closeFarm };
})();
