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
        'plumage.emerald': { name: 'Plumage émeraude', description: 'Un vert profond qui chatoie au soleil : à peine un Piou sur cent.' },
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
    spark: {
      name: 'Zapou', kind: 'Hérisson-éclair',
      description: 'Un petit hérisson gris dont les piquants crépitent et s’illuminent quand il est content… ' +
        'ou fâché. Les nuits d’orage, on voit ses piquants briller dans les collines. ' +
        'Certains apprentis magiciens le choisissent pour compagnon.',
      habitat: 'Collines, vieilles pierres',
      genes: { quills: 'Piquants', spines: 'Longueur', ears: 'Oreilles' },
      variants: {
        'quills.blue': { name: 'Étincelles bleues', description: 'Ses piquants brillent d’un bleu d’éclair.' },
        'quills.yellow': { name: 'Étincelles dorées', description: 'Des piquants qui luisent comme des braises dorées.' },
        'quills.violet': { name: 'Étincelles violettes', description: 'Une lueur violette, comme les éclairs des grands orages.' },
        'spines.short': { name: 'Piquants courts', description: 'Une petite boule de piquants bien serrés.' },
        'spines.long': { name: 'Piquants longs', description: 'De longs piquants qui crépitent au moindre souffle de vent.' },
        'ears.round': { name: 'Oreilles rondes', description: 'Deux petites oreilles rondes et douces.' },
        'ears.tufted': { name: 'Oreilles en plumet', description: 'Des oreilles hautes, terminées par une touffe de poils.' },
        chromatic: { name: 'Zapou des neiges', description: 'Tout blanc, ses étincelles sont blanches comme la neige.' },
        elemental: { name: 'Zapou d’ombre', description: 'Un pelage sombre et des étincelles violettes, presque noires.' },
      },
    },
    axolotl: {
      name: 'Ploufi', kind: 'Axolotl des berges',
      description: 'Il marche lentement au bord de l’eau en souriant à tout le monde. Ses branchies ' +
        'en plumeau frémissent quand il est content. Certains apprentis magiciens le choisissent pour compagnon.',
      habitat: 'Bords de l’eau, sous-bois humides',
      genes: { skin: 'Peau', gills: 'Branchies', tail: 'Queue' },
      variants: {
        'skin.pink': { name: 'Peau rose', description: 'Rose tendre, comme une dragée.' },
        'skin.blue': { name: 'Peau bleue', description: 'Un bleu pâle de lac au petit matin.' },
        'skin.gold': { name: 'Peau dorée', description: 'Doré comme du miel : il brille au soleil.' },
        'gills.small': { name: 'Petites branchies', description: 'Trois petits plumeaux de chaque côté de la tête.' },
        'gills.large': { name: 'Grandes branchies', description: 'De grands plumeaux qui ondulent comme des algues.' },
        'tail.short': { name: 'Queue courte', description: 'Une petite queue plate, bien pratique pour nager.' },
        'tail.long': { name: 'Longue queue', description: 'Une longue queue qui ondule derrière lui.' },
        chromatic: { name: 'Ploufi de lune', description: 'Tout blanc, presque transparent : on le croirait fait de lumière de lune.' },
        elemental: { name: 'Ploufi des mousses', description: 'Vert comme la mousse des berges : des feuilles poussent sur sa queue.' },
      },
    },
    cristi: {
      name: 'Cristi', kind: 'Boule de cristaux',
      description: 'Une petite boule de poils bleu nuit, deux grands yeux ronds, des oreilles pointues et des ' +
        'cristaux magiques plantés dans le dos. Immobile sur les parois, au sol ou au plafond de la grotte, ' +
        'on le prend pour un tas de cristaux… jusqu’à ce qu’on s’approche trop. Il grimpe partout, même la tête en bas.',
      habitat: 'Grotte aux Cristaux',
      genes: { crystal: 'Cristaux', ears: 'Oreilles', crown: 'Couronne' },
      variants: {
        'crystal.violet': { name: 'Cristaux violets', description: 'Ils luisent d’un violet profond, comme ceux de la grotte.' },
        'crystal.cyan': { name: 'Cristaux bleus', description: 'Une lueur bleu glacier, froide comme l’eau qui suinte des parois.' },
        'crystal.rose': { name: 'Cristaux roses', description: 'Un rose très rare : les mineurs en rêvent.' },
        'ears.pointed': { name: 'Oreilles pointues', description: 'Deux oreilles en pointe, toujours aux aguets.' },
        'ears.long': { name: 'Grandes oreilles', description: 'De longues oreilles qui tremblent au moindre bruit.' },
        'crown.three': { name: 'Trois cristaux', description: 'Trois cristaux serrés au milieu du dos.' },
        'crown.crown': { name: 'Couronne', description: 'Une couronne de petits cristaux a poussé autour des trois premiers.' },
        chromatic: { name: 'Cristi de neige', description: 'Une fourrure toute blanche, des cristaux blancs comme la lune.' },
        elemental: { name: 'Cristi d’or', description: 'Une fourrure dorée et des cristaux de lumière : il brille comme un trésor.' },
      },
    },
    raccoon: {
      name: 'Chipou', kind: 'Chapardeur masqué',
      description: 'Un raton gris au masque noir, gourmand et sans gêne. Les Chipou raflent les pommes des vergers ' +
        'et les œufs des poulaillers, puis filent en ricanant. En bande, ils deviennent agressifs : gare à qui les dérange !',
      habitat: 'Exploitation, terres sauvages, bois de l’Ouest',
      genes: { coat: 'Pelage', tail: 'Queue', ears: 'Oreilles' },
      variants: {
        'coat.grey': { name: 'Pelage gris', description: 'Gris cendre, avec son masque noir de voleur.' },
        'coat.rust': { name: 'Pelage roux', description: 'Roux comme une feuille d’automne.' },
        'coat.silver': { name: 'Pelage argenté', description: 'Un gris d’argent qui brille sous la lune.' },
        'tail.ringed': { name: 'Queue rayée', description: 'Une queue annelée de gris et de noir.' },
        'tail.bushy': { name: 'Queue touffue', description: 'Une énorme queue en panache : il s’en sert de couverture.' },
        'ears.round': { name: 'Oreilles rondes', description: 'Deux oreilles rondes bordées de blanc.' },
        'ears.tufted': { name: 'Oreilles en pinceau', description: 'Des oreilles hautes terminées par un pinceau de poils.' },
        chromatic: { name: 'Chipou pie', description: 'Noir et blanc, comme une pie : un voleur de grand chemin.' },
        elemental: { name: 'Chipou doré', description: 'Un pelage d’or : on dit qu’il ne vole que des choses qui brillent.' },
      },
    },
    owl: {
      name: 'Hulou', kind: 'Chouette du grand arbre',
      description: 'Une chouette ronde aux grands yeux d’ambre, perchée au sommet de l’arbre creux du bois de l’Ouest. ' +
        'Elle s’envole au moindre bruit, en faisant tourbillonner le vent sous ses ailes.',
      habitat: 'Cime de l’arbre creux, bois de l’Ouest',
      genes: { plumage: 'Plumage', tufts: 'Aigrettes', eyes: 'Yeux' },
      variants: {
        'plumage.brown': { name: 'Plumage brun', description: 'Brun moucheté, comme l’écorce du grand arbre.' },
        'plumage.snowy': { name: 'Plumage neigeux', description: 'Presque blanc : on la croirait venue des montagnes.' },
        'plumage.tawny': { name: 'Plumage fauve', description: 'Un fauve chaud, couleur de feuilles mortes.' },
        'tufts.tufts': { name: 'Aigrettes', description: 'Deux touffes de plumes dressées comme des oreilles.' },
        'tufts.none': { name: 'Tête ronde', description: 'Une tête toute ronde, sans aigrettes.' },
        'eyes.amber': { name: 'Yeux d’ambre', description: 'De grands yeux couleur de miel.' },
        'eyes.blue': { name: 'Yeux bleus', description: 'Des yeux bleu glacier, très rares.' },
        chromatic: { name: 'Hulou cendrée', description: 'Grise comme la brume du matin.' },
        elemental: { name: 'Hulou de nuit', description: 'Sombre comme une nuit sans lune : son vent est un vent d’ombre.' },
      },
    },
    sheep: {
      name: 'Laini', kind: 'Mouton nuage',
      description: 'Une boule de laine sur quatre pattes, douce comme un nuage. Les Laini vivent en troupeau ' +
        'avec le berger Basile ; quelques-uns broutent librement dans les collines.',
      habitat: 'Troupeau du berger, collines',
      genes: { wool: 'Laine', horns: 'Cornes', fleece: 'Toison' },
      variants: {
        'wool.white': { name: 'Laine blanche', description: 'Blanche comme un nuage d’été.' },
        'wool.black': { name: 'Laine noire', description: 'Un mouton noir : il y en a un dans chaque troupeau.' },
        'wool.pink': { name: 'Laine rose', description: 'Une laine rose bonbon, introuvable ailleurs.' },
        'horns.none': { name: 'Sans cornes', description: 'Une tête toute douce, sans cornes.' },
        'horns.curled': { name: 'Cornes enroulées', description: 'Deux belles cornes enroulées en spirale.' },
        'fleece.fluffy': { name: 'Toison épaisse', description: 'Une bonne épaisseur de laine.' },
        'fleece.cloud': { name: 'Toison nuage', description: 'Tellement de laine qu’on ne voit presque plus ses pattes.' },
        chromatic: { name: 'Laini d’or', description: 'Une toison dorée, comme dans les vieilles légendes.' },
        elemental: { name: 'Laini d’orage', description: 'Sa laine bleutée crépite : gare à l’électricité statique !' },
      },
    },
    mole: {
      name: 'Taupi', kind: 'Taupe fouisseuse',
      description: 'Une petite taupe au pelage de velours, presque aveugle, au museau rose et aux grosses pattes ' +
        'pour creuser. Les Taupi creusent des galeries interminables sous les champs et dans le terrier de l’Ouest.',
      habitat: 'Terrier, champs de l’exploitation',
      genes: { fur: 'Pelage', claws: 'Griffes', nose: 'Museau' },
      variants: {
        'fur.brown': { name: 'Pelage brun', description: 'Un velours brun foncé, couleur de terre.' },
        'fur.grey': { name: 'Pelage gris', description: 'Gris ardoise, doux comme de la soie.' },
        'fur.golden': { name: 'Pelage doré', description: 'Une taupe dorée : les mineurs la disent porte-bonheur.' },
        'claws.small': { name: 'Petites griffes', description: 'Des pattes de fouisseuse, déjà bien solides.' },
        'claws.big': { name: 'Grandes griffes', description: 'D’énormes pattes avant : elle creuse plus vite qu’on ne marche.' },
        'nose.pink': { name: 'Museau rose', description: 'Un petit museau rose qui frétille sans arrêt.' },
        'nose.star': { name: 'Museau étoilé', description: 'Une étoile de petits doigts roses autour du nez : très rare.' },
        chromatic: { name: 'Taupi de lune', description: 'Toute blanche, elle ne sort que les nuits de pleine lune.' },
        elemental: { name: 'Taupi de braise', description: 'Rouge comme une braise : elle a creusé trop près du feu de la terre.' },
      },
    },
  },
  // The three monsters to start a game with (kStarters), and their element.
  starterElements: { companion: 'fire', spark: 'electric', axolotl: 'water' },
  // Order of the species in the game (Species in src/core/world.hpp).
  speciesOrder: ['companion', 'slime', 'mushroom', 'bird', 'villager', 'crow', 'fish', 'spark', 'axolotl', 'cristi',
    'raccoon', 'owl', 'sheep', 'mole'],

  elements: {
    fire: { name: 'Feu', color: '#ff9a5a' },
    water: { name: 'Eau', color: '#6cc4ff' },
    nature: { name: 'Nature', color: '#8fd16a' },
    earth: { name: 'Terre', color: '#c9a06e' },  // only in the guide for now
    air: { name: 'Air', color: '#d6f0ff' },
    ice: { name: 'Glace', color: '#a8e6f0' },    // only in the guide for now
    shadow: { name: 'Ombre', color: '#b39ad9' },
    light: { name: 'Lumière', color: '#fff3c4' },
    electric: { name: 'Électrique', color: '#ffd23f' },
    normal: { name: 'Normal', color: '#cfc6b8' },
    arcane: { name: 'Arcane', color: '#e08cff' },
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
  // The magician's spells (src/core/battle.hpp).
  spells: {
    heal: { name: 'Soin', text: 'Soigne ton monstre de 10 PV, ou le réveille s’il est K.O.' },
    shield: { name: 'Bouclier', text: 'Protège ton monstre : aucun coup ne le touche pendant 1 seconde.' },
    mercy: { name: 'Retenue', text: 'Jusqu’à la fin du combat, le prochain coup de ton monstre qui mettrait ' +
      'son adversaire K.O. le laisse à 1 PV : idéal pour le capturer.' },
    detect: { name: 'Détection', text: 'Pendant 1 minute, le cristal de ton bâton cherche les monstres très rares à ' +
      'moins de 50 m : variante très rare, forme chromatique ou élémentaire, micro ou alpha. S’il en sent un, ' +
      'sa lumière pointe vers lui.' },
    wayOut: { name: 'Fil d’Ariane', text: 'Dans une grotte ou un bâtiment, pendant 1 minute, la lumière de ton bâton ' +
      'montre le chemin vers l’entrée par laquelle tu es venu : elle suit les tunnels et contourne les parois, ' +
      'pas juste la direction de la sortie. Gratuit.' },
    home: { name: 'Retour', text: 'Ta magie te ramène aussitôt à l’exploitation de Capucine, ton monstre avec toi, ' +
      'même du fond d’une grotte ou d’un bâtiment. Gratuit, mais une fois toutes les 5 minutes.' },
    // The capture spells (during a capture: the circle of runes).
    replay: { name: 'Écho', text: 'Les runes de la série se rallument dans l’ordre : reprends-la depuis la première. ' +
      'Celles déjà validées ne font pas avancer la jauge une seconde fois.' },
    guard: { name: 'Protection', text: 'Une erreur dans la série en cours ne fait rien perdre à la jauge de capture.' },
    boost: { name: 'Emprise', text: 'Chaque rune juste de la série en cours fait avancer la jauge moitié plus.' },
    newOrder: { name: 'Nouvelle série', text: 'Une autre série de runes, tout de suite, sans rien perdre.' },
    steady: { name: 'Apaisement', text: 'Le monstre se calme : la jauge revient à mi-chemin, 50 %. ' +
      'Seulement si elle est en dessous.' },
    hint: { name: 'Indice', text: 'La prochaine rune à toucher se met à briller.' },
  },
  // Builds of the monsters (Form in src/core/world.hpp): variants of every species.
  builds: {
    micro: { name: 'Micro', sign: 'µ', description: 'Toute petite et plus faible que les autres, mais vive et ' +
      'pleine de charme. Seules les femelles naissent micro.' },
    alpha: { name: 'Alpha', sign: 'α', description: 'Plus grand et plus fort que ses semblables. On n’en croise ' +
      'que très rarement.' },
    omega: { name: 'Oméga', sign: 'Ω', description: 'Un géant, fort et résistant. Seuls des mâles deviennent oméga ; ' +
      'on ne les voit que lors d’événements exceptionnels.' },
  },
  // A monster's gifts (in this order), from 0 to 20: no effect yet.
  gifts: ['Force', 'Dextérité', 'Constitution', 'Charisme', 'Intelligence'],
  // What variants change (Stat in src/core/variants.hpp).
  stats: {
    strength: 'force', dexterity: 'dextérité', constitution: 'constitution', charisma: 'charisme',
    intelligence: 'intelligence', hp: 'points de vie', attack: 'attaque', defense: 'défense', speed: 'vitesse',
    magic: 'magie',
  },
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
    feather: { name: 'Rafale', description: 'Il bat des ailes et lance une rafale de vent tourbillonnante.' },
    gust: { name: 'Bourrasque', description: 'Il bat des ailes de toutes ses forces : le vent repousse tout.' },
    shade: { name: 'Ombre', description: 'Une boule d’ombre qui glace ce qu’elle touche.' },
    flock: { name: 'Nuée', description: 'Des plumes noires tourbillonnent autour de lui.' },
    tail_slap: { name: 'Coup de queue', description: 'Une gifle avec sa queue mouillée.' },
    bubble: { name: 'Bulle', description: 'Une grosse bulle qui éclate au visage.' },
    wave: { name: 'Vague', description: 'Il fait jaillir une vague autour de lui.' },
    zap: { name: 'Petit choc', description: 'Il fonce piquants en avant : ça pique, et ça grésille !' },
    thunderbolt: {
      name: 'Éclair', byElement: { shadow: 'Éclair noir' },
      description: 'Ses piquants crépitent, puis un éclair file droit sur l’ennemi.',
    },
    discharge: {
      name: 'Décharge', byElement: { shadow: 'Décharge d’ombre' },
      description: 'Il se roule en boule, ses piquants s’illuminent : l’électricité frappe tout autour de lui.',
    },
    nip: { name: 'Mordillement', description: 'Une petite morsure, sans dents mais pas sans conviction.' },
    water_jet: {
      name: 'Jet d’eau', byElement: { nature: 'Jet de sève' },
      description: 'Il crache un jet d’eau puissant sur l’ennemi.',
    },
    geyser: {
      name: 'Geyser', byElement: { nature: 'Racines jaillissantes' },
      description: 'L’eau jaillit du sol tout autour de lui.',
    },
    tumble: { name: 'Roulé-boulé', description: 'Il se roule en boule et fonce dans l’ennemi.' },
    shard: {
      name: 'Éclat de cristal', byElement: { light: 'Éclat de lumière' },
      description: 'Son cristal s’illumine et lance un éclat tranchant droit sur l’ennemi.',
    },
    prism: {
      name: 'Prisme', byElement: { light: 'Prisme solaire' },
      description: 'Son cristal s’embrase : la magie jaillit en éclats tout autour de lui.',
    },
    swipe: { name: 'Chaparde', description: 'Un coup de patte vif : il chipe tout ce qui dépasse.' },
    pilfer: {
      name: 'Pomme volée', byElement: { light: 'Pièce d’or' },
      description: 'Il lance de toutes ses forces ce qu’il vient de voler.',
    },
    smoke_bomb: {
      name: 'Écran de fumée', byElement: { light: 'Éclat aveuglant' },
      description: 'Un nuage noir explose autour de lui, le temps de filer.',
    },
    talon: { name: 'Serres', description: 'Elle fond sur l’ennemi, serres en avant.' },
    squall: {
      name: 'Coup de vent', byElement: { shadow: 'Vent d’ombre' },
      description: 'Ses ailes lancent une bourrasque tourbillonnante droit sur l’ennemi. On dit qu’elle ferait tourner un moulin.',
    },
    hoot: {
      name: 'Hululement', byElement: { shadow: 'Cri de la nuit' },
      description: 'Un cri qui fait trembler l’air tout autour d’elle.',
    },
    ram: { name: 'Coup de tête', description: 'Il fonce tête baissée dans sa laine.' },
    fluff: {
      name: 'Boule de laine', byElement: { electric: 'Laine électrique' },
      description: 'Une boule de laine bien tassée, lancée de loin.',
    },
    wool_wave: {
      name: 'Vague de laine', byElement: { electric: 'Orage de laine' },
      description: 'Il s’ébroue : un nuage de laine déferle tout autour de lui.',
    },
    scratch: { name: 'Griffure', description: 'Un coup de ses grosses griffes de fouisseuse.' },
    clod: {
      name: 'Motte de terre', byElement: { fire: 'Motte de braise' },
      description: 'Elle creuse et lance une motte de terre sur l’ennemi.',
    },
    tremor: {
      name: 'Secousse', byElement: { fire: 'Éruption' },
      description: 'Elle frappe le sol : la terre tremble tout autour d’elle.',
    },
  },
  // Doors (interior.hpp): what the panel says near one. Houses by their kind.
  doors: {
    cave: 'Grotte aux Cristaux',
    tower: 'Tour des ruines',
    roof: 'Toit de la tour',
    house: { cottage: 'Maison', chimney: 'Grande maison', hut: 'Hutte', farmhouse: 'Ferme de Capucine', cabin: 'Cabane du bûcheron' },
    burrow: 'Terrier',
    hollowTree: 'Arbre creux',
    lighthouse: 'Phare',
    roofs: { tower: 'Toit de la tour', hollowTree: 'Cime de l’arbre creux', lighthouse: 'Galerie du phare' },
    actions: { enter: 'Entrer', leave: 'Sortir', climb: 'Monter', descend: 'Descendre' },
  },
  // Every monster's fourth move.
  parry: {
    name: 'Parer',
    description: 'Se met en garde : le prochain coup ne passe pas, et un adversaire dont la morsure est parée reste étourdi un instant.',
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
    coast: 'Côte des Embruns',
    hills: 'Contreforts du Nord',
    plains: 'Terres sauvages',
    farm: 'Exploitation de Capucine',
    woods: 'Bois de l’Ouest',
    crater: 'Cratère de l’Étoile',
    river: 'Rivière',
  },
  // The regions of the continent (src/core/continent.hpp), for the map. Not
  // reachable yet: names to refine.
  regions: {
    start: 'Val Verdoyant',
    mountains: 'Monts Givrés',
    volcano: 'Presqu’île du Volcan',
    desert: 'Désert d’Ocre',
    plains: 'Grandes Plaines',
    forest: 'Forêt Profonde',
    marsh: 'Marais des Brumes',
    lake: 'Lac Miroir',
    loneMountain: 'Mont Solitaire',
    capital: 'La Capitale',
  },
  // Teleporters, by place (src/core: TeleporterId).
  teleporters: {
    village: 'Village', ruins: 'Ruines de la Tour', forest: 'Forêt des Murmures', cave: 'Grotte aux Cristaux',
    castle: 'Château de Corbeval', farm: 'Exploitation de Capucine', menhirs: 'Cercle des Menhirs',
  },
  // The west's places (west.hpp), for the map.
  westPlaces: {
    cart: 'Charrette d’Anselme', bridge: 'Pont de la Route', pathBridge: 'Pont du Sentier', cove: 'Crique de la Cascade',
    waterMill: 'Moulin à eau', farm: 'Exploitation', mill: 'Moulin à vent', pasture: 'Bergerie',
    burrow: 'Terrier', woods: 'Bois de l’Ouest', cabin: 'Cabane du bûcheron', camp: 'Camp de Romane',
    hollowTree: 'Arbre creux', lighthouse: 'Phare', menhirs: 'Cercle des Menhirs', crater: 'Cratère de l’Étoile',
  },
  // The bag's items (src/core: Item).
  items: {
    coin: { name: 'Pièces', one: 'pièce', description: 'La monnaie du pays. Le marchand Anselme en veut contre ses potions.' },
    manaPotion: { name: 'Potion de mana', one: 'potion', description: 'Un flacon bleu qui pétille : rend 15 points de mana à ton bâton.' },
    strangeStone: { name: 'Pierre étrange', one: 'pierre', description: 'Un colis d’Isidore pour l’exploitation de Capucine : une pierre tiède qui vibre doucement, et une lettre scellée.' },
    goods: { name: 'Marchandises', one: 'ballot', description: 'Les ballots tombés de la charrette d’Anselme.' },
  },

  // The villagers with a name (Creature::npc), and what they say (web/dialogue.js).
  npcs: {
    1: { name: 'Mirabelle', chatter: ['Je fais les plus belles confitures de mirabelles du pays, tu sais.'] },
    2: { name: 'Gaspard', chatter: ['Ces runes… On dirait qu’elles respirent.'] },
    3: { name: 'Lison', chatter: ['Les Gloubi aiment la pluie : ils sautent dans les flaques en riant.',
      'Ma grand-mère disait que le magicien de la Tour parlait aux étoiles.'] },
    4: { name: 'Bastien', trainer: true, chatter: ['Ne t’approche pas trop des corbeaux du château : ils sont bien plus forts que ceux d’ici.',
      'Le soir, les Champi de la forêt brillent comme des lanternes.'] },
    5: { name: 'Odette', healer: true, chatter: ['Le puits n’a jamais tari, même pendant la grande sécheresse.',
      'Un Bloup a sauté dans mon seau ce matin ! Je l’ai remis au lac, bien sûr.'] },
    6: { name: 'Félix', chatter: ['Personne n’est entré dans la grotte aux Cristaux depuis des années. On dit qu’elle chante la nuit.',
      'Un vieux Piou peut voler jusqu’au château sans s’arrêter.'] },
    7: { name: 'Rosalie', chatter: ['Les monstres sont plus forts loin du village. Prends soin du tien !',
      'Il paraît qu’il existe des monstres aux couleurs jamais vues : on les dit chromatiques.'] },
    8: { name: 'Anatole', chatter: ['De mon temps, le château de Corbeval avait un seigneur. Maintenant, il n’y a plus que des monstres.',
      'Le sentier traverse tout le pays, du nord au sud. Suis-le, tu ne te perdras pas.'] },
    9: { name: 'Isidore', mentor: true, chatter: ['Un bon magicien écoute ses monstres autant qu’ils l’écoutent.',
      'La tour en ruine, au sud… J’y ai appris la magie, il y a bien longtemps.',
      'Les orages font sortir les Zapou de leurs terriers. On les voit courir sur les collines.'] },
    10: { name: 'Anselme', merchant: true, chatter: ['Des potions, des potions ! Fraîches du matin !',
      'J’ai vendu des potions jusqu’à la Capitale, tu sais. Mais ici, au moins, on prend le temps de vivre.'] },
    11: { name: 'Capucine', chatter: ['Mes vaches sont parties à la Capitale ; je garde les monstres des magiciens, maintenant. C’est bien plus amusant.',
      'Les pommes de mon verger sont les meilleures de l’Ouest. Les Chipou le savent bien, hélas.'] },
    12: { name: 'Violette', healer: true, chatter: ['Odette, au village ? C’est ma cousine ! C’est elle qui m’a tout appris.',
      'Le lait de Laini, ça soigne tout. Enfin… presque tout.'] },
    13: { name: 'Basile', chatter: ['Un bon berger connaît chacun de ses moutons par son nom. J’en ai vingt-trois… euh, huit.',
      'Quand le vent souffle de la mer, les Laini deviennent tout frisés.'] },
    14: { name: 'Firmin', chatter: ['Ce bois est vieux comme le monde. L’arbre creux, au milieu, était déjà géant quand mon grand-père était petit.',
      'Il y a des chouettes, tout là-haut, sur l’arbre creux. On y monte par l’intérieur, à ce qu’on dit.'] },
    15: { name: 'Romane', trainer: true, chatter: ['Je campe ici pour m’entraîner loin de tout. Le bois de l’Ouest regorge de monstres coriaces !',
      'Ma Hulou vient du sommet de l’arbre creux. Je l’ai attrapée après trois jours d’attente !'] },
    16: { name: 'Lucien', chatter: ['Mon moulin tourne depuis cent ans… enfin, il tournait.',
      'Sans vent, pas de farine. Sans farine, pas de pain. Sans pain, pas de Lucien heureux.'] },
    17: { name: 'Yvon', chatter: ['Une nuit, j’ai vu une étoile tomber au sud. Le lendemain, il y avait un cratère, et des cristaux qui brillent.',
      'Le cercle de menhirs, au nord… Les anciens disaient qu’il permettait de voyager en un clin d’œil.',
      'Mon phare guide les bateaux depuis quarante ans. Monte voir la vue, si tu n’as pas le vertige !'] },
    // Trainers passing by (npc::kWanderer...): met now and then in the wilds.
    18: { name: 'Hugo', trainer: true, wanderer: true, chatter: ['Je fais le tour du pays avec mes monstres.'] },
    19: { name: 'Maëlle', trainer: true, wanderer: true, chatter: ['Les routes sont plus sûres avec un bon compagnon.'] },
    20: { name: 'Théo', trainer: true, wanderer: true, chatter: ['Je cherche des adversaires dignes de ce nom.'] },
    21: { name: 'Lou', trainer: true, wanderer: true, chatter: ['Mes monstres et moi, on ne recule devant rien.'] },
    // The river (west.hpp).
    22: { name: 'Mathurin', chatter: ['La rivière descend de la Forêt Profonde, à l’est. Son eau est fraîche même en plein été.',
      'Près de la grotte aux cristaux, la rivière passe au pied d’une cascade. On raconte qu’il y a quelque chose derrière…',
      'Les Ploufi adorent la rivière. D’habitude ils sont gentils, mais quand ils s’énervent, gare à toi !'] },
  },
  // What the other villagers say about the quests (any of them, see web/dialogue.js).
  rumors: {
    lostPiou: ['Mirabelle a perdu son Piou… La pauvre, elle le cherche partout.',
      'Le Piou de Mirabelle a un plumage vert émeraude. Je n’en avais jamais vu de pareil !',
      'Il paraît qu’un Piou tout vert rôde autour du village. Ce serait celui de Mirabelle ?'],
    lostPiouDone: ['Merci d’avoir retrouvé le Piou de Mirabelle ! Elle ne le quitte plus.',
      'Mirabelle a fait des confitures pour fêter le retour de son Piou. Tu devrais goûter !',
      'Tu es le magicien qui a retrouvé Émeraude ? Bravo !'],
    teleporters: ['Gaspard ne quitte plus le cercle de pierres, sur la place. Il se demande à quoi il sert.',
      'Gaspard dit qu’il a vu le même cercle de pierres près du château.',
      'Ces vieilles pierres à runes, sur la place… Gaspard est persuadé qu’elles sont magiques.'],
    odette: ['Si tes monstres sont épuisés, va voir Odette : elle les soigne pour rien.',
      'Odette a toujours une tisane pour les monstres fatigués.',
      'Odette a un bâton de magicienne, comme toi. Elle peut te rendre ton mana.'],
    giantChampi: ['Lison jure qu’elle a vu un Champi grand comme elle dans la forêt. Elle exagère sûrement…',
      'Il paraît qu’au cœur de la forêt, les Champi sont plus grands qu’ailleurs. Demande à Lison !'],
    bastien: ['Bastien s’entraîne tous les jours avec son Champi, au bord du village. Il cherche un adversaire !',
      'Si tu veux te mesurer à un vrai dresseur, va voir Bastien, à l’entrée du village.'],
    teleportersDone: ['Gaspard raconte à tout le monde que tu as disparu dans un éclair de lumière !',
      'Alors ces cercles de pierres mènent d’un endroit à l’autre ? Quelle merveille !'],
    west: ['À l’ouest, la route mène à l’exploitation de Capucine, et plus loin jusqu’au phare.',
      'Un marchand a versé sa charrette sur la route de l’ouest. Le pauvre…',
      'On dit que des Chipou rôdent autour du verger de Capucine.'],
  },
  // The quests (src/core/quests.hpp): the journal, in the grimoire.
  quests: {
    lostPiou: {
      title: 'Le Piou de Mirabelle', giver: 1, xp: 60,
      summary: 'Mirabelle, au village, a perdu son Piou. Il a un plumage émeraude, une couleur très rare. Il rôde sans doute autour du village.',
      steps: { find: 'Trouver le Piou au plumage émeraude et le capturer', bring: 'Le rendre à Mirabelle' },
      done: 'Mirabelle a retrouvé son Piou, Émeraude. Il ne la quitte plus.',
    },
    teleporters: {
      title: 'Les cercles de pierres', giver: 2, xp: 40,
      summary: 'Gaspard se demande à quoi sert le cercle de pierres de la place du village. Il en a vu un pareil près du château.',
      steps: {
        discover: 'Trouver le cercle de pierres près du château',
        go: 'Aller du village au château par le cercle de pierres',
        back: 'Revenir au village de la même façon',
        tell: 'Raconter tout à Gaspard',
      },
      done: 'Les cercles de pierres sont des téléporteurs ! Gaspard n’en revient pas.',
    },
    giantChampi: {
      title: 'Le Champi géant', giver: 3, xp: 80,
      summary: 'Lison a vu un Champi gigantesque au cœur de la forêt, entouré d’autres Champi plus grands que d’habitude. ' +
        'Un Champi oméga ? Il est très costaud : il faudra bien l’affaiblir pour le capturer.',
      steps: { catch: 'Capturer le Champi géant, au cœur de la forêt', show: 'Le montrer à Lison' },
      done: 'Le Champi géant fait partie de ton équipe. Lison n’en revient pas.',
      again: 'Tu l’avais relâché : cette fois, ni la quête ni sa capture ne rapportent d’expérience.',
    },
    combat: {
      title: 'Initiation au combat', giver: 4, xp: 30,
      summary: 'Bastien, le dresseur à l’entrée du village, veut t’apprendre à combattre : attaques, esquives, parades. ' +
        'Rien de tel qu’un vrai combat de dresseurs contre lui.',
      steps: { win: 'Gagner un combat contre Bastien', tell: 'En reparler avec lui' },
      done: 'Bastien est fier de toi : « Tu as tout compris. Reviens quand tu veux pour une revanche ! »',
    },
    healing: {
      title: 'Initiation aux soins', giver: 5, xp: 25,
      summary: 'Odette, la guérisseuse, veut t’apprendre à soigner tes monstres toi-même : ton bâton connaît le sort de Soin.',
      steps: { heal: 'Lancer le sort de Soin sur un de tes monstres blessé', tell: 'Le dire à Odette' },
      done: 'Odette est rassurée : tu sais soigner tes monstres, même loin du village.',
    },
    main: {
      title: 'L’apprenti du village', giver: 9, xp: 100,
      summary: 'Isidore, le vieux magicien du village, t’a accueilli. Avant de t’enseigner davantage, il veut que tu aides ' +
        'les villageois : chacun t’apprendra quelque chose dont un magicien a besoin.',
      steps: {
        lostPiou: 'Capturer : retrouver le Piou de Mirabelle',
        teleporters: 'Voyager : percer le secret des cercles de pierres avec Gaspard',
        combat: 'Combattre : l’initiation de Bastien',
        healing: 'Soigner : l’initiation d’Odette',
        tell: 'Revenir voir Isidore',
      },
      done: 'Isidore est content de toi. La suite de ton apprentissage viendra bientôt…',
    },
    merchant: {
      title: 'La charrette renversée', giver: 10, xp: 40,
      summary: 'Anselme, un marchand ambulant, a versé sa charrette sur la route de l’ouest. Ses ballots de marchandises ' +
        'se sont éparpillés tout autour.',
      steps: { goods: 'Ramasser les ballots tombés de la charrette', tell: 'Les rapporter à Anselme' },
      done: 'La charrette est de nouveau sur ses roues. Anselme vend maintenant ses potions de mana.',
    },
    delivery: {
      title: 'La pierre étrange', giver: 9, xp: 60, main: true,
      summary: 'Isidore t’a confié un colis : une pierre étrange et une lettre, à livrer à Capucine, la propriétaire ' +
        'de l’exploitation agricole, à l’ouest. Suis la route qui part du sentier, près du départ.',
      steps: { stone: 'Porter la pierre étrange à Capucine, à l’exploitation', given: 'La lui remettre' },
      done: 'La pierre est une pierre de transfert, liée à ta magie : les monstres capturés au-delà de six seront envoyés à l’exploitation.',
    },
    transfer: {
      title: 'Essai de transfert', giver: 11, xp: 50, main: true,
      summary: 'Capucine veut vérifier que la pierre de transfert fonctionne : capture un monstre alors que ton équipe ' +
        'est complète (six monstres), puis viens le récupérer auprès d’elle.',
      steps: { sent: 'Capturer un monstre avec une équipe complète', took: 'Récupérer un monstre auprès de Capucine', tell: 'En parler à Capucine' },
      done: 'Le transfert fonctionne ! Capucine garde tes monstres, et tu peux les échanger quand tu veux.',
    },
    orchard: {
      title: 'Les pillards du verger', giver: 11, xp: 150, main: true,
      summary: 'Une bande de Chipou agressifs pille le verger de Capucine. Ils attaquent dès qu’on s’approche. ' +
        'Si tu perds ou si tu fuis, ta magie te ramènera à l’exploitation.',
      steps: { beaten: 'Battre les Chipou du verger', tell: 'Prévenir Capucine' },
      done: 'Le verger est sauvé ! Capucine t’en sera reconnaissante longtemps.',
    },
    shepherd: {
      title: 'Les agneaux perdus', giver: 13, xp: 60,
      summary: 'Trois agneaux du berger Basile se sont égarés dans les collines. Approche-toi d’eux : ils te suivront ' +
        'jusqu’à la bergerie.',
      steps: { lambs: 'Ramener les agneaux dans l’enclos', tell: 'Le dire à Basile' },
      done: 'Le troupeau est au complet. Pour te remercier, Basile t’a confié un Laini.',
    },
    waterWheel: {
      title: 'La roue bloquée', giver: 22, xp: 80,
      summary: 'Une bande de Ploufi en colère a coincé des branches dans la roue du moulin à eau de Mathurin, et ils ' +
        'montent la garde dans la rivière. Ils attaquent dès qu’on s’approche. Si tu perds ou si tu fuis, ta magie te ' +
        'ramènera à l’exploitation.',
      steps: { beaten: 'Battre les Ploufi de la roue', tell: 'Le dire à Mathurin' },
      done: 'La roue tourne de nouveau, et le moulin à eau moud le grain. Mathurin t’a récompensé.',
    },
    mill: {
      title: 'Le moulin arrêté', giver: 16, xp: 50,
      summary: 'Le vent est tombé et les ailes du moulin de Lucien sont coincées. Un monstre de vent, qui vole, pourrait ' +
        'les relancer : pendant un entraînement, sans viser ton magicien, lance son attaque à distance dans les ailes.',
      steps: { turning: 'Relancer les ailes du moulin', tell: 'Le dire à Lucien' },
      done: 'Les ailes tournent de nouveau. Lucien va pouvoir moudre son grain.',
    },
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
