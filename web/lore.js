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
    },
    slime: {
      name: 'Gloubi', kind: 'Gelée sauteuse',
      description: 'Une gelée vivante qui avance en rebondissant. On raconte qu’elle est née ' +
        'd’une goutte de potion renversée.',
      habitat: 'Prairies',
    },
    mushroom: {
      name: 'Champi', kind: 'Champignon dandinant',
      description: 'Il se promène lentement en se dandinant sous son grand chapeau. ' +
        'Ses spores auraient des vertus magiques.',
      habitat: 'Prairies et sous-bois',
    },
    bird: {
      name: 'Piou', kind: 'Oiseau craintif',
      description: 'Il sautille dans l’herbe et s’enfuit à tire-d’aile dès qu’on approche.',
      habitat: 'Prairies',
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
    },
    fish: {
      name: 'Bloup', kind: 'Poisson du lac',
      description: 'Il nage près de la surface et saute parfois hors de l’eau pour attraper les lucioles.',
      habitat: 'Lac du village',
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
    light: { name: 'Lumière', color: '#ffe37a' },
  },

  zones: {
    meadow: 'Prairie',
    village: 'Village',
    lake: 'Lac du village',
    ruins: 'Ruines de la Tour',
  },
};
