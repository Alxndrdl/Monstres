// Texts of the game world shown by the interface (ui.js, menu.js): names and
// descriptions of the creatures, elements, places. The game itself (C++)
// only knows identifiers ("slime", "water", "ruins"...).
window.MonstresLore = {
  species: {
    companion: {
      name: 'Pompon', kind: 'Renard des braises',
      description: 'Un petit renard tout rond dont la queue garde la chaleur d’une braise. ' +
        'Fidèle compagnon des apprentis magiciens, il te suit partout.',
      habitat: 'Toujours à tes côtés',
      genes: { fur: 'Pelage', ears: 'Oreilles', tails: 'Queue' },
      variants: {
        'fur.ember': { name: 'Pelage de braise', description: 'Roux vif, comme une braise qui couve.' },
        'fur.golden': { name: 'Pelage doré', description: 'Un roux pâle qui brille comme les blés au soleil.' },
        'fur.ash': { name: 'Pelage de cendre', description: 'Gris-brun, la couleur des feux éteints.' },
        'ears.pointed': { name: 'Oreilles pointues', description: 'Deux petites oreilles toujours aux aguets.' },
        'ears.long': { name: 'Grandes oreilles', description: 'Des oreilles immenses qui entendent un champignon pousser.' },
        'ears.floppy': { name: 'Oreilles tombantes', description: 'Elles pendent de chaque côté de la tête et lui donnent un air rêveur.' },
        'tails.one': { name: 'Une queue', description: 'Une seule queue touffue au bout crème.' },
        'tails.two': { name: 'Deux queues', description: 'On dit que les renards à deux queues ont déjà vécu une autre vie.' },
        chromatic: { name: 'Pompon de givre', description: 'Un pelage blanc-bleu, froid au regard mais chaud au toucher.' },
        elemental: { name: 'Pompon d’orage', description: 'Son pelage crépite : l’orage a remplacé la braise.' },
      },
    },
    slime: {
      name: 'Gloubi', kind: 'Gelée sauteuse',
      description: 'Une gelée vivante qui avance en rebondissant. On raconte qu’elle est née ' +
        'd’une goutte de potion renversée.',
      habitat: 'Prairies',
      genes: { color: 'Couleur', eyes: 'Yeux', top: 'Sommet' },
      variants: {
        'color.teal': { name: 'Gelée turquoise', description: 'La couleur de la potion d’où il serait né.' },
        'color.blue': { name: 'Gelée bleue', description: 'Un bleu pervenche, la couleur des potions de sommeil.' },
        'color.green': { name: 'Gelée verte', description: 'Vert pomme, et il sent un peu la pomme aussi.' },
        'eyes.two': { name: 'Deux yeux', description: 'Deux grands yeux ronds et curieux.' },
        'eyes.one': { name: 'Un œil', description: 'Un seul grand œil au milieu, qui ne cligne presque jamais.' },
        'eyes.three': { name: 'Trois yeux', description: 'Un troisième œil, plus haut, qui regarde vers le ciel.' },
        'top.smooth': { name: 'Sommet lisse', description: 'Une gelée toute ronde, sans rien sur la tête.' },
        'top.horn': { name: 'Corne', description: 'Une petite corne molle pousse sur son sommet.' },
        'top.antennae': { name: 'Antennes', description: 'Deux antennes au bout lumineux, qui sentent la pluie venir.' },
        chromatic: { name: 'Gloubi de cristal', description: 'Presque transparent, il scintille comme du verre.' },
        elemental: { name: 'Gloubi de braise', description: 'Une gelée brûlante qui laisse de petites étincelles en sautant.' },
      },
    },
    mushroom: {
      name: 'Champi', kind: 'Champignon dandinant',
      description: 'Il se promène lentement en se dandinant sous son grand chapeau. ' +
        'Ses spores auraient des vertus magiques.',
      habitat: 'Prairies et sous-bois',
      genes: { cap: 'Chapeau', shape: 'Forme', legs: 'Pattes' },
      variants: {
        'cap.red': { name: 'Chapeau rouge', description: 'Rouge à pois blancs, le plus classique.' },
        'cap.golden': { name: 'Chapeau doré', description: 'Un chapeau couleur d’automne.' },
        'cap.blue': { name: 'Chapeau bleu', description: 'Il ne pousse qu’à l’ombre des vieilles pierres des ruines.' },
        'shape.round': { name: 'Chapeau rond', description: 'Un chapeau bombé bien rond.' },
        'shape.wide': { name: 'Chapeau plat', description: 'Un large chapeau plat, pratique pour s’abriter de la pluie.' },
        'shape.pointed': { name: 'Chapeau pointu', description: 'Un chapeau pointu, comme celui d’un magicien.' },
        'legs.short': { name: 'Pattes courtes', description: 'De petites pattes qui le font se dandiner.' },
        'legs.long': { name: 'Pattes longues', description: 'Juché sur de grandes pattes, il voit loin dans les herbes.' },
        chromatic: { name: 'Champi d’ivoire', description: 'Un chapeau blanc comme la lune.' },
        elemental: { name: 'Champi luminescent', description: 'Son chapeau brille doucement : il éclaire les sous-bois la nuit.' },
      },
    },
    bird: {
      name: 'Piou', kind: 'Oiseau craintif',
      description: 'Il sautille dans l’herbe et s’enfuit à tire-d’aile dès qu’on approche.',
      habitat: 'Prairies',
      genes: { plumage: 'Plumage', crest: 'Huppe', wings: 'Ailes' },
      variants: {
        'plumage.yellow': { name: 'Plumage jaune', description: 'Jaune comme un bouton d’or.' },
        'plumage.blue': { name: 'Plumage bleu', description: 'Bleu-gris, la couleur du ciel avant l’aube.' },
        'plumage.pink': { name: 'Plumage rose', description: 'Un rose saumon assez rare dans les prairies.' },
        'crest.none': { name: 'Sans huppe', description: 'Une tête toute ronde.' },
        'crest.crest': { name: 'Huppé', description: 'Trois plumes dressées sur la tête, qu’il hérisse quand il a peur.' },
        'wings.small': { name: 'Petites ailes', description: 'Des ailes courtes : il préfère courir.' },
        'wings.large': { name: 'Grandes ailes', description: 'De grandes ailes qui l’emportent loin à la moindre alerte.' },
        chromatic: { name: 'Piou d’argent', description: 'Des plumes gris argent qui brillent au soleil.' },
        elemental: { name: 'Piou orageux', description: 'Un plumage sombre d’où jaillissent des étincelles.' },
      },
    },
    villager: {
      name: 'Villageois', kind: 'Habitant du village',
      description: 'Se promène autour de la place et salue les passants.',
      habitat: 'Village',
    },
    crow: {
      name: 'Croa', kind: 'Corbeau des toits',
      description: 'Il guette depuis les toits du village et le sommet de la vieille tour. ' +
        'Rien ne lui échappe, et il s’envole dès qu’on s’approche.',
      habitat: 'Village et ruines de la Tour',
      genes: { eyes: 'Yeux', horns: 'Cornes', wings: 'Ailes' },
      variants: {
        'eyes.black': { name: 'Yeux noirs', description: 'Des yeux noirs et brillants, comme deux perles.' },
        'eyes.white': { name: 'Yeux blancs', description: 'Des yeux pâles qui semblent voir à travers les murs.' },
        'eyes.red': { name: 'Yeux rouges', description: 'Des yeux rouges qui luisent dans la pénombre.' },
        'horns.none': { name: 'Sans cornes', description: 'Une tête de corbeau ordinaire.' },
        'horns.horns': { name: 'Cornu', description: 'Deux petites cornes d’os sur la tête. Personne ne sait d’où elles viennent.' },
        'wings.normal': { name: 'Ailes ordinaires', description: 'Des ailes noires taillées pour planer entre les toits.' },
        'wings.long': { name: 'Longues ailes', description: 'De longues ailes qui l’emportent jusqu’en haut de la tour.' },
        chromatic: { name: 'Croa blanc', description: 'Un corbeau tout blanc. Les villageois disent qu’il porte chance.' },
        elemental: { name: 'Croa d’aube', description: 'Ses plumes dorées brillent comme le soleil levant.' },
      },
    },
    fish: {
      name: 'Bloup', kind: 'Poisson du lac',
      description: 'Il nage près de la surface et saute parfois hors de l’eau pour attraper les lucioles.',
      habitat: 'Lac du village',
      genes: { scales: 'Écailles', fins: 'Nageoires', lure: 'Leurre' },
      variants: {
        'scales.blue': { name: 'Écailles bleues', description: 'Bleu comme l’eau du lac.' },
        'scales.red': { name: 'Écailles rouges', description: 'Rouge corail, il se voit de loin sous l’eau.' },
        'scales.green': { name: 'Écailles vertes', description: 'Vert algue, il se cache entre les plantes du fond.' },
        'fins.small': { name: 'Petites nageoires', description: 'De petites nageoires orange.' },
        'fins.veil': { name: 'Nageoires voilées', description: 'De longues nageoires qui ondulent comme des voiles.' },
        'lure.none': { name: 'Sans leurre', description: 'Rien sur la tête.' },
        'lure.lantern': { name: 'Lanterne', description: 'Une petite lanterne pend devant sa bouche et attire les lucioles.' },
        chromatic: { name: 'Bloup doré', description: 'Des écailles d’or : un trésor du lac.' },
        elemental: { name: 'Bloup électrique', description: 'Il crépite quand il saute hors de l’eau. Mieux vaut ne pas se baigner à côté.' },
      },
    },
  },
  // Order of the species in the game (Species in src/core/world.hpp).
  speciesOrder: ['companion', 'slime', 'mushroom', 'bird', 'villager', 'crow', 'fish'],

  elements: {
    fire: { name: 'Feu', color: '#ff9a5a' },
    water: { name: 'Eau', color: '#6cc4ff' },
    nature: { name: 'Nature', color: '#8fd16a' },
    air: { name: 'Air', color: '#d6f0ff' },
    shadow: { name: 'Ombre', color: '#b39ad9' },
    light: { name: 'Lumière', color: '#fff3c4' },
    electric: { name: 'Électrique', color: '#ffd23f' },
    normal: { name: 'Normal', color: '#cfc6b8' },
  },

  // Kinds of creatures (Family in src/core/variants.hpp).
  families: {
    beast: 'Bête', bird: 'Oiseau', fish: 'Poisson', reptile: 'Reptile', insect: 'Insecte',
    plant: 'Végétal', ooze: 'Gelée', construct: 'Automate',
  },

  rarities: {
    common: { name: 'Commune', color: '#c9c4d8' },
    uncommon: { name: 'Peu commune', color: '#8fd16a' },
    rare: { name: 'Rare', color: '#6cc4ff' },
    veryRare: { name: 'Très rare', color: '#f2c94c' },
  },

  // Attacks of the monsters (species_moves in src/core/battle.cpp). byElement:
  // the name when the attack is of another element (elemental forms).
  moves: {
    bite: { name: 'Morsure', description: 'Un petit coup de dents rapide. On peut l’enchaîner.' },
    ember: {
      name: 'Boule de braise', byElement: { electric: 'Boule d’orage' },
      description: 'Il crache une boule de son élément, qui file droit sur l’ennemi.',
    },
    blaze: {
      name: 'Tourbillon de flammes', byElement: { electric: 'Tourbillon d’éclairs' },
      description: 'Il tourne sur lui-même et brûle tout ce qui est autour de lui.',
    },
    bounce: { name: 'Rebond', description: 'Il rebondit sur son adversaire.' },
    splash: { name: 'Jet de gelée', description: 'Une boule de gelée lancée de loin.' },
    flood: { name: 'Raz-de-gelée', description: 'Il s’écrase au sol : la gelée gicle tout autour.' },
    headbutt: { name: 'Coup de chapeau', description: 'Un coup de tête avec son grand chapeau.' },
    spores: { name: 'Spores', description: 'Un nuage de spores serré, soufflé vers l’ennemi.' },
    spore_cloud: { name: 'Nuage de spores', description: 'Il secoue son chapeau : des spores partout autour de lui.' },
    peck: { name: 'Coup de bec', description: 'Un coup de bec vif et précis.' },
    feather: { name: 'Plume tranchante', description: 'Il lance une plume dure comme une lame.' },
    gust: { name: 'Bourrasque', description: 'Il bat des ailes de toutes ses forces : le vent repousse tout.' },
    shade: { name: 'Ombre', description: 'Une boule d’ombre qui glace ce qu’elle touche.' },
    flock: { name: 'Nuée', description: 'Des plumes noires tourbillonnent autour de lui.' },
    tail_slap: { name: 'Coup de queue', description: 'Une gifle avec sa queue mouillée.' },
    bubble: { name: 'Bulle', description: 'Une grosse bulle qui éclate au visage.' },
    wave: { name: 'Vague', description: 'Il fait jaillir une vague autour de lui.' },
  },
  moveShapes: { melee: 'Corps à corps', projectile: 'À distance', burst: 'Tout autour' },

  zones: {
    meadow: 'Prairie',
    village: 'Village',
    lake: 'Lac du village',
    ruins: 'Ruines de la Tour',
    forest: 'Forêt des Murmures',
    cave: 'Grotte aux Cristaux',
    castle: 'Château de Corbeval',
  },
  // What the signposts say (web/ui.js adds the way to each place).
  signs: {
    village: 'Bienvenue au village !',
    other: 'Un vieux panneau de bois.',
  },
};

// Every monster is a male or a female (first gene): the same texts for all.
for (const id of window.MonstresLore.speciesOrder) {
  const species = window.MonstresLore.species[id];
  if (!species.variants) continue;
  species.genes = { sex: 'Sexe', ...species.genes };
  species.variants = {
    'sex.male': { name: 'Mâle', description: 'Les mâles sont plus grands et plus costauds que les femelles.' },
    'sex.female': { name: 'Femelle', description: 'Les femelles sont plus petites et plus fines que les mâles.' },
    ...species.variants,
  };
}
