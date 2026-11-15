# Monstres

Petit jeu en C++ compilé en WebAssembly avec Emscripten, rendu WebGL2
(OpenGL ES 3.0), installable en PWA et jouable hors ligne.

Le joueur est un apprenti magicien (robe étoilée, chapeau pointu, bâton à
cristal lumineux) qui parcourt un monde magique de 280 m de côté avec son
premier monstre, choisi en début de partie (Pompon le renard des braises,
Zapou le hérisson-éclair ou Ploufi l'axolotl), qui le suit et saute derrière
lui. Dans les prairies vallonnées vivent des monstres sauvages : Gloubi la
gelée sauteuse, Champi le champignon qui se dandine, Piou l'oiseau qui
s'enfuit quand on approche, et plus rarement Zapou, Ploufi et Pompon.

Au nord, le sentier mène au village : chaumières, maisons à cheminée qui fume,
huttes rondes, puits, barrières, panneaux, tonneaux et lanternes, des
villageois variés qui se promènent et saluent de la main, des corbeaux (Croa)
perchés sur les toits, et un lac où nagent des poissons (Bloup) qui sautent
hors de l'eau.

Au sud, sur une butte au bord du sentier, se dressent les ruines de la Tour :
la tour abandonnée d'un ancien magicien (visible de loin), les restes d'une
enceinte, une colonnade à moitié effondrée, des éboulis et quatre pierres à
runes qui brillent encore, entourées d'étincelles magiques.

À l'ouest ou à l'est (de l'autre côté du sentier que les ruines), la forêt
des Murmures : arbres et sapins serrés, quelques géants, sous-bois sombre,
Champi et Piou. Tout au nord, la grotte aux Cristaux : une colline rocheuse
aux falaises abruptes, une ravine qui mène à une arche de pierre, un creux
sombre éclairé par des cristaux lumineux. Tout au sud, sur une butte, le
château de Corbeval : remparts crénelés, quatre tours à drapeau, une porte
fortifiée qu'on franchit, un donjon aux fenêtres éclairées la nuit. Plus on
s'éloigne du départ, plus les monstres sont forts.

Partout : herbes qui ondulent au vent, fleurs, arbres, sapins, rochers,
troncs, souches et buissons ; forêt au-delà des bords, montagnes enneigées,
ciel et soleil, et une météo qui change d'un endroit à l'autre (voir Météo).

Une partie commence par la **création du personnage** (voir Nouvelle
partie), puis une courte cinématique d'arrivée ; Isidore, le vieux magicien
du village, accueille le joueur et lui confie la quête principale.

Contrôles :
- joystick en bas à gauche, ou ZQSD / flèches : se déplacer (par rapport à la caméra) ;
- glisser sur l'écran (doigt ou souris) : regarder autour de soi ;
- bouton en bas à droite, ou Espace : sauter. Les petits obstacles (un rocher bas, une
  bûche, une souche, un buisson : jusqu'à 75 cm) ne bloquent pas : on monte dessus en marchant ;
- boutons à droite du joystick, ou R et G : lancer ses sorts (voir Sorts) ;
- toucher ou cliquer sur un monstre (ou un villageois) : sa fiche s'affiche en
  haut à gauche (nom, famille, éléments, description, ses variantes, niveau,
  ce qu'il fait, distance) et un anneau doré le marque au sol ; toucher
  ailleurs ou × la ferme. Le menu contextuel, à droite, propose ce qu'on peut
  faire avec lui : « Combattre » un monstre (voir Combats), « S'entraîner »
  avec son propre compagnon ;
- toucher un panneau du village (à moins de 25 m) : ce qui y est écrit, avec
  la direction (flèche, nord en haut) et la distance de chaque lieu du monde ;
- toucher un villageois puis « Parler » (à moins de 5 m) : la discussion
  s'affiche en bas de l'écran (voir Quêtes) ;
- toucher un téléporteur (cercle de pierres à runes) : « Se téléporter » quand
  on se tient au milieu, puis choisir sa destination sur la carte ;
- livre en haut à gauche (ou touches C, E, B, M) : le grimoire, qui met le jeu en
  pause, avec six onglets :
  - Personnage : portrait du magicien, nom (choisi à la création de la
    partie), niveau, expérience, mana (barre bleue), où l'on se trouve, et
    ses statistiques : monstres capturés, combats gagnés, monstres battus ;
  - Équipe : les monstres de l'équipe (le compagnon d'abord, 6 au plus) ;
    toucher l'un d'eux affiche sa fiche : son sexe (♂ bleu, ♀ rose), famille,
    éléments, niveau, expérience, points de vie, ses variantes (chacune de la
    couleur de sa rareté), taille, poids, âge, ses aptitudes en toile
    d'araignée (voir Variantes) et ses trois attaques ; « Relâcher » (confirmé
    une seconde fois) le rend à la nature : il sort du cristal et s'en va ;
  - Sorts : les quatre emplacements et le sort de chacun (voir Sorts) ;
  - Quêtes (touche J) : le journal, les quêtes en cours puis terminées ;
    chacune avec qui l'a demandée, son histoire, ses étapes (cochées une fois
    faites) et sa récompense ;
  - Bestiaire : tous les monstres, avec pour chacun un portrait, la famille,
    le ou les éléments, une description, sa taille, son poids et sa vitesse
    moyens, ses caractéristiques et son carnet (variantes capturées et
    aperçues, monstres capturés, dans l'équipe, battus, relâchés) ; le
    compteur ADN de la liste donne les variantes capturées au moins une
    fois ; le bouton « Habitat » montre à la place de sa fiche la carte de
    la zone jouable, les zones où il vit clignotant ; le bouton
    « Variantes » montre toutes ses variantes : « ? » si jamais vue, grisée
    si aperçue (un monstre qui la porte est passé à moins de 20 m), en clair
    si capturée ; toucher une variante découverte affiche sa fiche à droite
    de la grille (rareté, effets sur les aptitudes s'il y en a, où la
    trouver si elle n'est que dans certaines zones, description) ;
  - Carte : une seule carte, du terrain autour du joueur jusqu'au continent
    entier (molette, pincer, boutons − et + ; la faire glisser au doigt ou à
    la souris, sans pouvoir l'éloigner du continent ; ⌖ la recentre sur le
    joueur, une barre donne l'échelle ; ces outils sont posés sur la carte).
    De près les lieux de la zone jouable, de loin les régions du continent ;
    les téléporteurs connus, et les icônes des villageois proches (« ? »,
    « ! », « + », « × ») ; le bouton « Légende » affiche ou cache la légende ;
  Échap le ferme ;
- engrenage en haut à droite : paramètres (gérer les sauvegardes, voir
  Sauvegardes ; afficher les FPS et le détail du temps de calcul, voir Performances ; ces
  choix sont retenus par le navigateur) et options de debug : un curseur pour
  choisir l'heure de la journée, « Figer l'heure », et un curseur de météo.

Sur ordinateur, les touches sont rappelées sous le joystick et le bouton ;
elles s'adaptent à la disposition du clavier quand le navigateur le permet.

Sur téléphone et tablette, on joue en mode paysage : l'application installée
s'ouvre en paysage, et en portrait un message demande de tourner l'appareil.

## Organisation

```
CMakeLists.txt, CMakePresets.json   build natif et web
cmake/                              options de compilation communes
src/core/        monstres_core      règles du jeu, sauvegarde : C++ pur, sans navigateur
src/render/      monstres_render    monde -> instances à dessiner, caméra, modèles,
                                    personnages animés par parties (characters.cpp)
  gles3/         monstres_render_gles3   backend OpenGL ES 3.0 / WebGL2
src/platform/web/                   main(), boucle, caméra, IndexedDB, API JS (pre.js, post.js)
web/                                index.html (gabarit Emscripten), util.js (chargé en
                                    premier : aides communes aux scripts), controls.js (joystick,
                                    saut, regard, clavier), ui.js (paramètres, FPS, fiche
                                    des monstres), saves.js (les sauvegardes), menu.js (grimoire : personnage,
                                    bestiaire, cartes), lore.js (noms et textes du monde),
                                    sw.js, manifeste, icônes
assets/models/                      modèles 3D .glb chargés par le jeu (générés par
                                    tools/make_models.py, remplaçables par des exports Blender)
tests/unit/                         tests unitaires (doctest)
tests/functional/                   scénarios de jeu complets
tests/regression/                   rejeux + anciennes sauvegardes comparés à des références
tests/e2e/                          tests navigateurs Playwright (Chromium, Firefox, WebKit)
tools/                              make_models.py (modèles), make_icons.py (icônes PWA)
scripts/test.sh                     lance tous les tests
```

## Relief

Le sol est une carte de hauteurs (`src/core/heightmap.cpp`) : une grille de
2 m tirée de la graine du monde (collines, plus hautes vers les bords ; plat
autour du village ; fond du lac creusé ; petites bosses atténuées le long du
sentier ; buttes à sommet plat pour les ruines et le château ; colline de la
grotte aux falaises infranchissables, entaillée d'une ravine jusqu'à l'arche). Entre deux points, le sol est fait de deux triangles par case,
exactement ceux que dessine le moteur : on marche sur ce qu'on voit. Les
pentes de plus de 45° (`kMaxWalkSlope`) ne se montent pas à pied, il faut
sauter. Les objets sont posés sur le sol (légèrement enfoncés), l'herbe, les
ombres et le sentier suivent la pente, et la caméra ne passe jamais sous une
colline.

La simulation (60 pas par seconde) est déterministe : entrées en entiers,
générateur aléatoire PCG32 à graine, sinus/cosinus maison
(`src/core/include/monstres/core/math.hpp`) et `-ffp-contract=off`. Une même
partie donne exactement le même état en natif et en WebAssembly ; les tests de
régression le vérifient.

## Le monde et le continent

Le monde jouable fait 1 160 m de côté (±580 m, `rules::kWorldHalfSize`), soit
environ 16 fois l'ancienne surface. Au milieu, la Vallée du Départ : l'ancien
monde de 280 m, inchangé (même chemin, mêmes lieux, mêmes objets ; seule la
crête qui fermait ses bords a disparu). Autour, les terres sauvages, avec
leurs zones : la Côte des Embruns à l'ouest (une plage, puis la mer, à
environ 450 m du départ ; on ne peut pas entrer dans l'eau), les Contreforts
du Nord vers les montagnes (pins et rochers), et les Terres sauvages
ailleurs (bois en bosquets, buissons, rochers, souches, et un bord de la
grande forêt à l'est). Les monstres y vivent comme dans la vallée (Gloubi sur
la plage, Piou et Champi sur les collines…), de plus en plus forts en
s'éloignant (un niveau de plus tous les 110 m). Le chemin continue au nord et
au sud.

Tout cela est sur une île géante d'environ 4 km sur 4
(`src/core/continent.hpp`). Au nord, les Monts Givrés et leurs pics enneigés ;
à l'est, une presqu'île avec un volcan actif (lave dans le cratère, panache
de fumée) ; au sud, un désert de dunes et de mesas ; au centre, les Grandes
Plaines avec une forêt, un marais, un lac, le Mont Solitaire (un volcan
éteint) et la Capitale. L'océan entoure l'île. Au-delà du monde jouable, ces
régions sont vides et inaccessibles, mais on les voit de loin et sur la
carte. Les noms (`regions` dans `web/lore.js`) sont provisoires.

Le relief :
- le continent vient de la graine (bruit déterministe) sur une grille de
  10 m. Il vaut 0 dans la vallée, qui garde son propre relief ;
- le relief jouable (une grille de 2 m sur ±640 m, `Heightmap`) ajoute au
  continent les petites collines et les lieux de la vallée. Les collines
  s'effacent avant ±640 m, où le terrain lointain prend le relais avec
  exactement les mêmes hauteurs ;
- la mer est là où le continent lui-même passe sous son niveau (−2 m).
  Un creux des collines plus bas que la mer reste de la terre sèche (le
  départ de la graine 42 en est un).

Les terres sauvages ont environ 12 000 à 17 000 objets (`generate_wilds`),
selon la graine. Ils sont tirés de la graine, avec leur propre flux aléatoire,
**et ne sont pas sauvegardés** : seuls ceux de la vallée le sont, et les
autres sont tirés de nouveau à chaque chargement. La sauvegarde reste à
environ 62 Ko (60 Ko avant l'agrandissement).

Ce qui tient le passage à l'échelle :
- **index des objets** (`ObjectIndex`, dans `World`, recalculé quand les
  objets changent) : leurs boîtes rangées par cases de 8 m. Les collisions,
  les projectiles, les perchoirs, l'apparition des monstres, la caméra et le
  toucher ne regardent que les objets proches ; plus rien ne parcourt tous les
  objets à chaque pas ;
- **sol en tuiles** de 64 m (`GroundTiles`, `src/render/terrain_mesh.hpp`) :
  - un sol grossier en mailles de 8 m pour tout le relief, construit au
    chargement ;
  - en mailles de 2 m (les triangles mêmes sur lesquels on marche) sur la
    tuile du joueur et les huit autour, en 4 m deux ou trois tuiles plus loin ;
  - quand le joueur change de tuile, celles qui manquent sont construites en
    avance, quelques-unes par image (2 ms au plus), puis toutes dessinées
    d'un coup ; d'ici là, les précédentes restent (après un téléporteur,
    tout est construit tout de suite). Les tuiles construites sont gardées
    (120 au plus, les plus anciennes oubliées) ;
  - peu d'appels de dessin : les tuiles dessinées forment un seul maillage,
    et les tuiles grossières restantes sont remises en ordre (leurs indices
    seulement), les deux rangés par secteurs autour du joueur. Celles qui
    sont dans le champ se suivent : deux à cinq appels pour tout le sol ;
  - le bord d'une tuile voisine d'une plus grossière suit le bord de
    celle-ci : pas de fente. Une bande descend le long du bord du terrain
    lointain ;
  - les tuiles et le sol grossier sont indexés : chaque point est un seul
    sommet, partagé par jusqu'à six triangles (cinq fois moins de mémoire,
    et la carte graphique ne calcule chaque sommet qu'une fois) ;
  - l'eau de la mer dans le relief est en mailles de 16 m, découpée en
    parties comme le reste ;
- **paysage par morceaux** de 32 m (`LandscapeCache`,
  `src/render/landscape.hpp`) : les objets de chaque morceau (leurs modèles
  de près, à mi-distance et de loin, leur ombre) et son herbe sont calculés
  une fois, en avance quand le joueur s'approche, et gardés. Reconstruire le
  paysage tous les 16 m ne fait plus que recopier ;
- **seuls les objets proches sont dessinés** : les arbres et les rochers à
  moins de 215 m, rapetissant entre 160 et 198 m (pas d'apparition brusque).
  Les bâtiments sont dessinés partout. Les effets (fumées, lanternes,
  étincelles) ne parcourent que les objets de la vallée ;
- **seul ce que la caméra voit est dessiné** (`src/render/cull.hpp`) : le
  paysage est rangé par secteurs autour du joueur, le sol, le chemin et le
  continent sont découpés en parties, chacune avec sa boîte. À chaque image,
  les parties hors du champ de la caméra sont laissées de côté (les voisines
  visibles sont dessinées d'un seul appel). Les créatures hors champ ne sont
  pas dessinées non plus ;
- **la carte** met ses données en cache, dessine le monde à 1 pixel par
  mètre et le continent à 1 pixel pour 10 m.

Rendu lointain (`src/render/far_terrain.hpp`) :
- le sol du continent est un seul maillage indexé (chaque point une seule
  fois) en anneaux carrés autour du relief jouable : mailles de 10 m jusqu'à
  840 m du centre, puis 20, 40 et 80 m ; le bord fin suit le bord grossier.
  L'océan profond n'a pas de sol, son eau est opaque ;
- il est dessiné dans une passe lointaine (`DrawList::far`), avec sa propre
  plage de profondeur (8 m à 9,5 km). La profondeur est ensuite effacée, et la
  scène proche (0,1 m à 2 km : tout le relief jouable, même depuis un coin)
  est dessinée par-dessus. Chaque passe garde ainsi une profondeur précise.
  C'est correct parce que tout ce qui est lointain est hors du carré du relief
  et la caméra dedans ;
- le brouillard est exponentiel (à moitié à 1,8 km, complet à 9 km).

Mesures :
- natif (graine 21) : 14 500 objets, monde construit en environ 120 ms,
  0,01 ms par pas de simulation dans les terres sauvages, sauvegarde de
  62 Ko écrite et relue en environ 120 ms ;
- continent : grille en environ 50 ms, maillages en environ 20 ms ;
  53 000 triangles, 2,4 Mo ;
- dans le navigateur (Chromium sans carte graphique) :
  - de 127 000 à 166 000 triangles par image selon les lieux, contre
    460 000 à 520 000 avant le tri selon la vue et les tuiles ; l'herbe et
    les fleurs en font de 5 000 à 25 000 ;
  - de 59 à 93 appels de dessin par image ;
  - simulation environ 0,25 ms par image ;
  - la mise à jour du paysage et du sol ne dépasse plus environ 3,5 ms (le
    travail en avance est limité à 2 ms par image).

  À revérifier sur téléphone (détail du temps de calcul).

Des tests vérifient le budget :
- `test_wilds.cpp` : construction, temps d'un pas, taille de la sauvegarde ;
  l'index trouve ce que trouverait un parcours complet ;
- `test_continent.cpp` : triangles et temps.

Il reste à faire, quand le monde grandira encore ou s'ouvrira au continent :
- un relief et des objets de la simulation par tronçons (le monde est encore
  construit en entier au chargement, environ 120 ms) ;
- d'autres maillages indexés (les modèles, l'eau, le chemin).

## Un monde vivant autour du joueur

Le monde est découpé en carrés de 40 m (`update_population`,
`src/core/world.cpp`). Les monstres sauvages d'un carré apparaissent quand le
joueur s'en approche (à moins de 50 m) et disparaissent quand il s'en éloigne
(au-delà de 80 m) : seuls les environs du joueur sont simulés. En revenant,
d'autres monstres sont là (tirés de la graine, du carré et du nombre de
visites). Espèces et niveaux dépendent des lieux : 1 à 3 dans la prairie, 2 à
4 en forêt, 3 à 5 aux ruines, 4 à 6 à la grotte, 5 à 7 au château ; corbeaux
sur les toits et les tours, poissons dans le lac. Les villageois restent au
village (immobiles quand le joueur est loin). Un monstre en combat ne
disparaît jamais. Chaque créature a un identifiant (`Creature::id`) pour la
suivre pendant que d'autres vont et viennent.

L'affichage suit le même principe (`build_landscape`,
`src/render/landscape.cpp`) : autour du joueur (64 m), tout est dessiné avec
les ombres, et l'herbe et les fleurs poussent (56 m), de plus en plus
clairsemées au-delà de 20 m (des touffes un peu plus larges compensent) et,
au-delà de 22 m, en modèles plus simples (un triangle par brin : 4 et 6
triangles au lieu de 21 et 33, des fleurs de 6 triangles au lieu de 14). Les
arbres et les buissons sont entiers à moins de 26 m, puis simplifiés
(`tree_mid`, 92 triangles au lieu de 332 ; `bush_far`, 40 au lieu de 320).
Au-delà de 64 m, seuls les points marquants restent (maisons, tours,
remparts, grands rochers, ruines) et les arbres et sapins sont remplacés par
des modèles très simplifiés (`tree_far`, 28 triangles). Le paysage détaillé
est reconstruit quand le joueur a avancé de 16 m ; l'herbe et les petits
objets rétrécissent jusqu'à disparaître avant le bord (dans le shader), si
bien que rien n'apparaît d'un coup.

## Nouvelle partie

`web/create.js` : au tout premier lancement (aucune sauvegarde, et aucun
`?new` ni `?seed` dans l'adresse), la création passe avant tout et ne peut
pas être annulée ; plus tard, « Nouvelle partie » dans un emplacement vide
des sauvegardes l'ouvre aussi. Le monde de la page n'est pas dessiné pendant
ce temps. À gauche, le magicien (dessiné par le moteur, `build_preview` dans
`src/render/scene.cpp`) : on le fait tourner en glissant, on zoome (molette,
deux doigts, boutons) et on le déplace (deux doigts, clic droit ou Maj), avec
« Visage » et « En pied ». À droite, cinq étapes :

- **Nom** (obligatoire, ne change plus ensuite) ;
- **Apparence** : homme ou femme (la silhouette change : épaules, robe,
  cils), 6 couleurs de peau, cheveux courts, longs, en chignon ou rasés et
  8 couleurs, 6 couleurs d'yeux ; le chapeau est retiré de l'aperçu pendant
  cette étape ;
- **Équipement** : bâton ou baguette, 8 couleurs de robe, chapeau pointu,
  capuche ou rien ;
- **Compagnon** : Pompon (feu), Zapou (électricité) ou Ploufi (eau), du même
  sexe que le magicien et sous leur forme la plus commune ;
- **Monde** : la graine, tirée au hasard par défaut, à changer à la main ou
  avec le dé.

Côté jeu : `WizardLook` et `NewGame` (`src/core/world.hpp`, l'apparence est
dans la sauvegarde depuis la version 19), `wizard_rig`
(`src/render/characters.cpp` : les pièces blanches du magicien sont teintes
de la couleur choisie ; sous le chapeau ou la capuche, des cheveux ajustés
qui ne les traversent pas ; le bâton tenu devant la manche et penché vers
l'extérieur, la baguette pointée vers l'avant). Après la création, la caméra
part de très haut au-dessus du magicien et descend vers lui en tournant un
peu, pendant 7 s, jusqu'à la caméra du jeu (`src/render/intro.cpp`) ; tout
le reste de l'écran est caché et « Passer » l'interrompt. Une note signale ensuite qu'Isidore attend sur la
place du village.

## Quêtes, villageois et téléporteurs

Règles dans `src/core/quests.hpp` / `quests.cpp`, dialogues dans
`web/dialogue.js`, textes dans `web/lore.js` (`npcs`, `rumors`, `quests`).
Les huit villageois ont un nom (`Creature::npc`) : Mirabelle, Gaspard, Lison,
Bastien, Odette, Félix, Rosalie et Anatole ; Isidore, le vieux magicien à la
barbe blanche et à la robe violette, se tient sur la place.

Au-dessus des villageois, des icônes lumineuses qui tournent disent ce
qu'ils ont pour le joueur (`npc_marks`, `src/core/quests.cpp`) : « ? » blanc,
une quête à prendre ; « ! » doré, une quête en cours ; « + » vert, des soins
(Odette) ; « × » rouge, des combats (Bastien). Elles peuvent s'additionner
(Odette avant son initiation : « ? » et « + »). La carte les montre aussi,
pour les villageois à moins de 200 m du joueur.

La quête principale, « L'apprenti du village » (Isidore) : sa première
partie consiste à aider les villageois, qui apprennent chacun quelque chose
au joueur : capturer (le Piou de Mirabelle), voyager (les cercles de pierres
de Gaspard), combattre (l'initiation de Bastien : gagner un combat contre
lui, puis lui en reparler, 30 XP / 20) et soigner (l'initiation d'Odette :
lancer le sort de Soin sur un de ses monstres blessé, puis le lui dire,
25 XP / 15). Revenir ensuite voir Isidore : 100 XP au magicien, 60 au
compagnon ; la suite viendra plus tard. Les initiations peuvent se faire
dans n'importe quel ordre, avant même de parler à Isidore. Les autres parlent de la vie du
pays, et des quêtes en cours ou finies (« Mirabelle a perdu son Piou… »,
« Gaspard ne quitte plus le cercle de pierres… », puis des remerciements).

Téléporteurs : un cercle de pierres à runes au village, aux ruines, dans la
forêt, devant la grotte et devant le château. S'en approcher (6 m) le fait
connaître pour toujours : ses runes s'allument et tournent, une note le
signale. Debout au milieu d'un téléporteur connu, on peut aller vers
n'importe quel autre déjà découvert, en le choisissant sur la carte
(`Command::Teleport`) ; le compagnon suit.

Le Piou de Mirabelle : Mirabelle a perdu son Piou au plumage émeraude, une
couleur très rare (environ un Piou sur 128). Il rôde à une trentaine de
mètres du village et y revient chaque fois qu'on approche tant qu'il n'est pas
capturé. Lui parler propose la quête ; lui « montrer un monstre » : un autre
Piou n'est pas le sien (même émeraude : elle reconnaîtrait le sien entre
mille), un autre monstre n'est même pas un Piou. Rendu, il reste ensuite à
côté d'elle (on ne peut plus le combattre). Récompense : 60 XP au magicien,
40 au compagnon.

Les cercles de pierres : Gaspard, à côté du téléporteur du village, se
demande à quoi il sert ; il en a vu un près du château. Il faut trouver
celui du château, aller du village au château par téléporteur, revenir, puis
tout lui raconter. Récompense : 40 XP au magicien, 25 au compagnon.

Le Champi géant : Lison a vu un Champi gigantesque au cœur de la forêt. Un
Champi oméga (niveau 8) y vit, à côté du téléporteur, avec deux Champi alpha
(niveau 5) et deux normaux (niveau 4) ; le groupe revient chaque fois qu'on
approche tant que l'oméga n'est pas capturé (il rejoint le combat avec les
siens). Une fois capturé, il faut le montrer à Lison. Récompense : 80 XP au
magicien, 50 au compagnon. Capturé, il ne réapparaît plus. Relâché, il vit
dans la nature ; quand il a disparu (le joueur s'est éloigné), Lison propose
de nouveau la quête et il revient au cœur de la forêt, mais ni sa capture ni
la quête ne rapportent plus d'expérience.

Un monstre déjà battu une fois, ou relâché, ne rapporte plus d'expérience,
ni en le battant de nouveau ni en le capturant (et même relâché ensuite).

## Modèles 3D

Les modèles sont des fichiers glTF binaires (`assets/models/*.glb`) : le jeu
les charge au démarrage (bibliothèque cgltf) et n'en contient aucune
géométrie. Ils sont générés par un script Python sans dépendance :

```bash
python3 tools/make_models.py           # régénère assets/models/
python3 tools/make_models.py --check   # vérifie qu'ils sont à jour (aussi lancé par ctest)
```

Le script construit chaque modèle à partir de quelques outils (`Mesh`) :
boîtes, boules cabossées (`blob`), troncs de cône (`frustum`) et surfaces de
révolution d'une seule pièce (`lathe` : un profil qui tourne autour d'un axe,
pour un chapeau, une robe, une manche). Chaque triangle dit comment il est
éclairé, et `Mesh.finish` calcule les normales :

- **à facettes** (`FLAT`) : le décor garde son allure anguleuse (arbres,
  rochers, nuages, planches) ;
- **lisse** (`ROUND`) : ce qui est rond en vrai (côté d'un cylindre de 7 pans
  ou plus, chapeau, robe, tours) ; les normales des faces voisines sont
  moyennées, sauf de part et d'autre d'une arête de plus de 62° qui reste
  franche (le bord d'un chapeau) ;
- **boule** (`LUMP`) : lisse sur les personnages et leurs parties (magicien,
  villageois, monstres), à facettes ailleurs.

Les bouts d'un `frustum` sont fermés (pas de trou par où voir à travers),
sauf `open_ends` pour les troncs d'arbres, dessinés par milliers et dont
les bouts sont dans le sol et le feuillage. Le magicien, vu de près, a une
tête de 320 faces et un chapeau, une robe et des manches d'une seule pièce.

Les conventions (origine, taille, couleurs par sommet, transparence) sont
décrites en tête du script. Un modèle peut être remplacé par un export Blender
en `.glb` qui les respecte ; il faut alors le retirer du script, sinon
`--check` le signalera. Le sol et le sentier dépendent de la graine du monde et
sont construits par le jeu (`src/render/terrain_mesh.cpp`).

Les portraits du grimoire sont dessinés par le moteur lui-même, hors écran
(`build_portrait` dans `src/render/scene.cpp`, `draw_offscreen` du rendu).
Les caractéristiques des monstres et du magicien sont des règles du jeu
(`monster_stats`, `wizard_stats` dans `src/core/world.cpp`) ; les noms et
descriptions, eux, sont dans `web/lore.js`.

## Espèces

Monstres : Pompon (renard des braises, feu), Gloubi (gelée, eau), Champi
(champignon, nature), Piou (oiseau, air), Croa (corbeau, ombre et air), Bloup
(poisson, eau), Zapou (hérisson aux piquants lumineux, électricité : collines, ruines, grotte,
château) et Ploufi (axolotl, eau : bords de l'eau, côte, forêt). Pompon,
Zapou et Ploufi sont les trois compagnons qu'on peut choisir en début de
partie ; Pompon se rencontre aussi, rarement, dans les prairies. Les
caractéristiques, attaques et façons de combattre de chaque espèce sont dans
`src/core/world.cpp` (`kStats`) et `src/core/battle.cpp` (`kMoves`,
`kTempers`), ses gènes dans `src/core/variants.cpp`, ses textes dans
`web/lore.js`.

## Variantes

Chaque monstre porte des gènes (`src/core/variants.hpp`) : d'abord le sexe
(mâle ou femelle, à parts égales ; les femelles sont dessinées 14 % plus
petites), puis pour chaque autre gène de son espèce (couleur du pelage ou des yeux, forme des oreilles, nombre de
queues ou d'yeux, cornes, antennes, huppe, nageoires...), un allèle tiré selon
sa rareté (commune, peu commune, rare) ; certains allèles ne se trouvent que
dans certaines zones (le chapeau bleu des Champi, dans les ruines). Deux
formes très rares (1 sur 256 chacune, jamais les deux à la fois) s'y ajoutent : chromatique (couleurs
inhabituelles, par exemple le Croa blanc) et élémentaire (un autre élément,
entouré d'étincelles). Les gènes sont tirés depuis la graine, un flux par
créature : ils ne changent rien d'autre au monde.

Gabarit : chaque monstre est normal, ou très rarement micro (une femelle sur
128 : 62 % de la taille) ou alpha (1 sur 256 : 140 % de la taille) ; les oméga
(230 %, mâles seulement) ne se trouvent pas dans la nature, seulement lors d'événements
spéciaux (le Champi géant de la forêt, voir Quêtes). Le gabarit est tiré après
les autres gènes, et ses trois variantes (micro, alpha, oméga) viennent après
les formes rares dans le bestiaire (« Gabarits »). Comme les autres variantes,
un gabarit ne change que les bonus des aptitudes (voir plus bas), pas les
points de vie, l'attaque, la défense, la vitesse ni la magie ; seule sa taille
à l'écran change.

Un monstre qui rejoint l'équipe reçoit ses traits, tirés de la graine et de
son identifiant (`roll_traits`) : sa taille et son poids (selon l'espèce, le
sexe, le gabarit et un peu de hasard), son âge (qui grandit avec les jours du
jeu) et cinq aptitudes : force, dextérité, constitution, charisme,
intelligence. Chacune a une base tirée de 5 à 15 (sauvegardée), que ses
variantes modifient (`variant_mods`), entre 0 et 20 :
- gabarit : alpha +2 partout ; micro +5 dextérité et charisme, -5 force et
  constitution ; oméga l'inverse ;
- forme chromatique +2 charisme ; élémentaire +1 charisme et +1 intelligence ;
  une couleur très rare (le Piou émeraude) +1 charisme ;
- Gloubi (les autres espèces viendront) : antennes +1 intelligence ; trois yeux
  +1 intelligence et -1 constitution ; un œil -1 intelligence et +1
  constitution ; corne -1 charisme et +2 en défense (une caractéristique de
  combat).

Elles n'ont pas encore d'effet. Sur la fiche d'un monstre (Équipe), sous
« Aptitudes », elles forment une toile d'araignée (le 0 un peu écarté du
centre, la plage possible en pointillés) ; à sa droite, le détail de
l'aptitude choisie (la première au départ ; toucher, cliquer ou sélectionner
au clavier en choisit une autre, la survoler la montre un instant) : sa
valeur, sa base, son minimum et son maximum, et chaque modificateur avec sa
source. Dans le bestiaire, la fiche d'une variante dit ses effets sur les
aptitudes.

Dans le grimoire, les pages en deux colonnes (Équipe, Sorts, Quêtes,
Bestiaire) font défiler la liste de gauche et la fiche de droite chacune de
son côté.

Le bestiaire retient pour
chaque variante si elle est inconnue, aperçue ou capturée (dans la
sauvegarde) ; le compagnon est capturé dès le départ. Les espèces ont aussi
une famille (bête, oiseau, poisson, reptile, insecte, végétal, gelée,
automate) et un ou deux éléments (le Croa est Ombre et Air).

L'apparence d'une variante est construite par `creature_look`
(`src/render/characters.cpp`) : pièces redimensionnées, ajoutées (`horn`,
`bulb`, `eye_iris`) ou retirées, couleurs des emplacements de teinte. Les noms
et descriptions des variantes sont dans `web/lore.js`.

## Équipe, invocation et rappel

Les monstres de l'équipe vivent dans le cristal du bâton du magicien. À
gauche de l'écran, une icône par monstre (niveau, points de vie) :
toucher un monstre resté dans le cristal l'invoque à la place du compagnon,
qui y retourne (le nouveau prend sa place exacte) ; toucher le compagnon le
rappelle, et plus aucun monstre ne suit le magicien. Pendant un combat, on
peut échanger le compagnon, pas le rappeler. Depuis la fiche d'équipe du
grimoire, « Relâcher » rend un monstre à la nature (il en faut toujours un
dans l'équipe, et pas pendant un combat) : il sort du cristal, ou quitte le
magicien s'il le suivait, et s'enfuit vivre sa vie (un Bloup retourne au
lac). Le recapturer ou le battre ne rapporte plus d'expérience. Un faisceau de lumière de la
couleur de l'élément du monstre relie le cristal au monstre, qui apparaît ou
disparaît dans la lumière (0,4 s). Un monstre capturé est d'abord retenu par
un cercle de runes (0,3 s), puis rejoint le cristal de la même façon
(`add_crystal_effects`, `src/render/characters.cpp` ; modèle `rune_circle`).
Les espèces lentes suivent quand même le magicien (au moins 6 m/s en courant). Un Croa compagnon
vole à hauteur d'épaule, un Bloup nage dans l'air, aussi pendant les combats.

## Jour et nuit

Une heure de jeu dure une minute réelle : une journée, 24 minutes. L'heure
fait partie du monde (`World::clock`, dans la sauvegarde) ; une partie
commence à 8 h. Le rendu (`sky_at`, `sky_directions` et `world_lights` dans
`src/render/`) en tire les couleurs du ciel et de la lumière (aube vers 6 h,
crépuscule vers 18 h 30, nuit de 20 h 30 à 4 h 30), la course du soleil et
celle de la lune, les étoiles. La nuit, les fenêtres et les lanternes
s'allument (couleurs des modèles d'alpha 0,985, voir `tools/make_models.py`),
les lanternes éclairent autour d'elles, le cristal du bâton du magicien
aussi, de même que les monstres lumineux (formes élémentaires, antennes des
Gloubi, lanterne des Bloup) et les projectiles des combats : jusqu'à 12
lumières, les plus proches. Les étincelles et les parties lumineuses des
personnages gardent leur éclat la nuit (`kGlowing`).

## Météo

Sept temps : ensoleillé, quelques nuages, couvert (on ne voit plus le ciel),
pluie, tempête avec orage, brouillard, neige (sur l'herbe verte : pas de
saisons pour l'instant). Ils ne changent rien au jeu pour l'instant, ni aux
combats ni aux monstres.

**Le temps qu'il fait** (`src/core/weather.cpp`) est un champ sur tout le
pays et le continent : des cellules de 800 m, chacune avec son temps pendant
6 minutes (le changement prend une minute), qui dérivent avec le vent
(3 m/s, sa direction dépend de la graine) et se fondent l'une dans l'autre à
leurs bords. Il pleut donc là-bas pendant qu'il fait beau ici, et l'orage
qu'on voit au loin peut arriver. Tout est tiré de la graine et du temps du
jeu : rien à sauvegarder, le même temps sur tous les appareils. Les
premières minutes d'une partie sont toujours au beau. Chaque temps est un
mélange de quelques valeurs (nuages, nuages sombres, pluie, neige,
brouillard, orage, vent : `Weather`), qui se mélangent d'un temps à l'autre.

**À l'écran** (`src/render/weather_view.cpp`) :

- le ciel dessine lui-même une couche de nuages au-dessus de la tête (bruit
  qui dérive avec le vent), autant que le temps en met ; couvert, il devient
  gris, le soleil, la lune et les étoiles disparaissent ;
- vers l'horizon et au loin, des nuages en modèles, partout jusqu'à 3,4 km,
  autant qu'il en faut là où chacun se trouve (plus simples au-delà de
  700 m) ; ceux d'orage sont sombres et montent haut ; sous les nuages de
  pluie ou de neige, un rideau de pluie qu'on voit de loin ;
- autour de la caméra, la pluie ou la neige tombe (des boîtes de gouttes ou
  de flocons répétées autour d'elle), penchée par le vent ;
- les nuages cachent le soleil : la lumière baisse et devient grise ; pluie,
  neige et brouillard épaississent la brume (le brouillard cache tout à
  une centaine de mètres) ; le vent agite plus l'herbe ;
- l'orage lance des éclairs (environ un toutes les trois secondes, là où il
  y a de l'orage) : un éclair proche illumine le paysage un instant.

Le curseur « Météo » des paramètres (debug) choisit le temps autour du
joueur (jusqu'à 2,6 km ; plus loin, le temps naturel) ; « Orage au loin »
met le beau temps ici et un orage à 1,1 km devant la caméra. Dans la console :
`monstres.setWeather(k)` (-1 naturel, 0 à 6, 7 orage au loin) et
`monstres.state().weather`. Coût mesuré (Chromium, logiciel) : environ
30 000 triangles de plus quand le ciel est couvert, 40 000 sous la pluie,
moins d'un demi-milliseconde de calcul par image.

## Combats

Règles dans `src/core/battle.hpp` / `battle.cpp`, interface dans
`web/battle.js`. Depuis le menu contextuel, « Combattre » lance un combat entre
le compagnon et le monstre visé (à moins de 15 m du joueur). Les monstres de la
même espèce à moins de 7 m de lui se joignent au combat (3 adversaires au
plus). Les Croa s'envolent et tournent autour du compagnon, à 2 m du sol, et
piquent pour mordre : en l'air, on les atteint avec les attaques à distance
(projectiles visés en hauteur) ou l'onde, pas avec une morsure. Les Bloup du
lac restent dans l'eau. La caméra se place derrière le compagnon, tournée
vers sa cible : le joystick déplace le compagnon (en avant : vers la cible,
sur les côtés : on tourne autour), le magicien attend.

Viser : les icônes des adversaires s'affichent en haut de l'écran, la cible
entourée de rouge. Toucher une autre icône (ou le monstre lui-même) le vise ;
toucher la cible actuelle ne vise plus personne : mode libre, le compagnon se
déplace par rapport à la caméra (qu'on tourne à nouveau librement), ses
projectiles partent droit devant (vers un adversaire s'il est juste en face),
sa morsure touche ce qui est devant lui, son onde tout ce qui est autour. Au
clavier, T passe à l'adversaire suivant, puis au mode libre. Quand la cible
est battue, le compagnon vise le plus proche des autres.

Les adversaires ont les mêmes délais d'attaque que le compagnon (le temps de
recharge compte depuis le début de leur élan). Ils tournent autour du
compagnon, s'approchent quand leur morsure est prête et reculent sinon,
s'écartent les uns des autres, et esquivent parfois les projectiles qui
arrivent sur eux. Chaque espèce a sa façon de se battre (`species_temper`) :
le Gloubi reste au contact et bondit sur son adversaire, le Champi garde ses
distances et lance ses spores (il ne mord que ce qui s'approche), le Piou mord
puis s'enfuit et esquive souvent, le Croa harcèle depuis les airs (3 m) et
pique pour mordre, le Bloup reste dans le lac. Un Croa de l'équipe vole, y
compris au-dessus du lac : tant que le bouton de saut (ou Espace) est enfoncé
sans bouger, il monte (jusqu'à 5 m de plus), puis redescend doucement quand on
le relâche.

Boutons en bas à droite pendant un combat :
- esquive (le bouton de saut, Espace) : un bond rapide dans la direction du
  déplacement (à l'arrêt : un saut sur place ; un Croa, lui, s'élève), pendant
  lequel on ne peut pas être touché ;
- au-dessus, trois attaques (touches 1, 2, 3) : une attaque rapide au corps à
  corps, qu'on peut enchaîner, et deux attaques spéciales avec un temps de
  recharge (un projectile de son élément, une onde tout autour de lui) ;
- à gauche de l'esquive, parer (F) : la prochaine attaque est bloquée, et un
  ennemi dont le coup est paré reste étourdi un moment.

L'ennemi montre ses attaques avant de frapper (anneau rouge qui bat, zone de
l'onde, boule qui grossit) : c'est le moment d'esquiver ou de parer. Dans le
menu contextuel : « Fuir » (le compagnon revient vers le joueur, puis le
combat s'arrête et le joueur reprend la main) et « Capturer », possible quand
l'ennemi n'a plus que 25 % de ses points de vie (trait doré sur sa barre) : il
rejoint l'équipe (il faut viser celui qu'on veut capturer). Chaque monstre
battu ou capturé rapporte 8 points d'expérience par niveau, partagés entre les monstres de l'équipe
qui ont combattu selon les dégâts infligés par chacun : XP × dégâts du monstre
/ (1 + dégâts de toute l'équipe), arrondi ; le magicien en gagne la moitié.
Un monstre déjà battu une fois, ou relâché par le joueur, ne rapporte plus
rien : l'écran de victoire en donne la raison (« Déjà battu », « Il avait été
relâché »).
L'écran de victoire montre la barre d'expérience de chacun qui se remplit,
niveau après niveau, puis la caméra quitte doucement le compagnon pour revenir
au magicien (elle glisse aussi au début et à la fin des combats). Le monstre
battu reste allongé K.O. (étoiles au-dessus de la tête) une dizaine de
secondes, puis se relève et rentre chez lui. Les monstres sauvages ont un
niveau selon leur lieu (voir plus haut).

K.O. et soins : un compagnon K.O. en combat retourne dans le cristal, et on en
fait sortir un autre de l'équipe (barre à gauche) ; le combat n'est perdu que
quand plus aucun monstre n'est debout (le dernier reste alors allongé). Les
points de vie ne reviennent plus tout seuls : il faut un sort de soin, ou
Odette, au village, qui soigne et réveille toute l'équipe et remplit le mana
du magicien, gratuitement (en lui parlant). On la reconnaît à son bâton de
magicienne, au cristal vert entouré d'étincelles. Un monstre K.O. ne peut pas sortir du cristal en combat ; hors
combat, il en sort mais reste à terre jusqu'à ce qu'on le soigne, et retourne
dans le cristal si le magicien s'éloigne à plus de 15 m.

Trop loin : si le compagnon s'éloigne à plus de 25 m de ses adversaires (ou de
son magicien à l'entraînement), un avertissement laisse 5 secondes pour
revenir à portée avant la fin du combat.

Entraînement : toucher son compagnon puis « S'entraîner ». On le contrôle comme
en combat, mais face au magicien, qui lève son bâton et se protège d'un
bouclier de lumière : chaque coup s'y brise (le nombre affiché est ce que
l'attaque aurait infligé à un monstre de même niveau), personne n'est blessé.
L'icône du magicien en haut de l'écran (ou le magicien lui-même) se vise ou se
« dévise » comme un adversaire. L'écran est celui d'un combat : le magicien
en haut (sans points de vie : son bouclier encaisse tout), les points de vie
du compagnon en bas. « Arrêter l'entraînement » (ou rappeler le compagnon) y
met fin. Pas de forces ni de
faiblesses entre éléments pour l'instant. Les combats ne sont pas sauvegardés :
une partie rechargée pendant un combat reprend sans lui.

### Sorts

Le magicien lance des sorts. Onglet « Sorts » du grimoire (touche P) : deux
sorts de combat (utilisés en combat et à l'entraînement) et deux sorts
d'aventure (le reste du temps), qu'on change hors combat. Ils se lancent avec
les deux boutons à côté des attaques (touches R et G), qui montrent leur
recharge ; un sort qui dure brille tant qu'il agit.

Chaque sort coûte du mana (en bas de l'écran, juste à droite des points de
vie du compagnon, dans le même cadre avec une barre bleue). Le mana ne
revient pas tout seul : Odette le remplit. Il vaut 20 au niveau 1 et grandit
de 5 par niveau du magicien (un nouveau niveau ajoute ces 5 points).
- Soin (combat et aventure, 4 mana) : +10 points de vie, ou réveille un
  monstre K.O. avec 10 points de vie ; recharge 5 s.
- Bouclier (combat, 3 mana) : aucun coup ne touche le monstre pendant 1 s ;
  recharge 5 s.
- Retenue (combat, 5 mana) : jusqu'à la fin du combat, le prochain coup du
  monstre qui mettrait un adversaire K.O. le laisse à 1 point de vie (« Retenu
  ! 1 PV ») : de quoi le capturer. Inutile à l'entraînement.
- Détection (aventure, 6 mana) : pendant 1 minute, le cristal du bâton cherche
  les monstres très rares à moins de 50 m (une variante très rare, une forme
  chromatique ou élémentaire, un micro ou un alpha) ; il pulse, et s'il en sent
  un, une traînée de lumière dorée part vers le plus proche.

Les sorts choisis, le mana et la Détection en cours sont sauvegardés
(sauvegarde v18).

### Combats de dresseurs

Bastien, au bord du village, a un Champi (niveau 4) à côté de lui et un Gloubi
(niveau 5) en réserve. En lui parlant, « Combattre ! » lance un combat de
dresseurs (`Command::Challenge`). Les deux monstres vont se placer face à
face, à 10 m l'un de l'autre, juste à l'extérieur du village (un endroit plat
et dégagé, cherché autour du village à partir du côté opposé au lac) ; le
magicien et Bastien se mettent en retrait, sur le côté. Pendant ce temps, les
règles s'affichent, et « Prêt ! » (`Command::Ready`) commence le combat :
- un seul monstre de chaque côté se bat à la fois ; un monstre K.O. est
  éliminé, son dresseur envoie le suivant (le joueur choisit le sien dans sa
  barre d'équipe : les monstres K.O. y sont grisés et barrés) ;
- le dresseur qui n'a plus de monstre en état de se battre perd ;
- « Déclarer forfait » (ou « Renoncer » avant le début) termine le combat
  tout de suite : l'adversaire gagne ;
- on voit combien de monstres a l'adversaire, mais leur portrait ne
  s'affiche qu'une fois entrés en jeu (sinon « ? ») ; les K.O. sont grisés et
  barrés ; on vise ou on passe en mode libre comme dans la nature ;
- pas de capture ; seule la première victoire contre Bastien rapporte de
  l'expérience (8 par niveau de ses monstres), le nombre de victoires est
  sauvegardé (sauvegarde v16).

Après le combat, les monstres K.O. du joueur le restent (sort de soin ou
Odette), et le Champi de Bastien revient à ses côtés. Une partie sauvegardée en plein
combat reprend sans lui, le Champi à sa place.

## Performances

Paramètres → « Afficher les FPS » puis « Détail du temps de calcul » : un
panneau montre, sur la dernière demi-seconde, le temps moyen et le pire d'une
image, et où il passe :
- processeur (le jeu en WebAssembly) : logique du jeu (pas de simulation par
  image), scène (personnages, animations, effets), caméra et lumières, envoi au
  GPU (appels WebGL), reconstruction du paysage, sauvegarde ;
- carte graphique : son temps de dessin, quand le navigateur le mesure
  (extension `EXT_disjoint_timer_query_webgl2`, souvent absente sur
  téléphone) ; sinon « hors du jeu » : attente de l'écran, du GPU et du
  reste de la page ;
- les triangles dessinés par catégorie (sol, herbe, décor, personnages,
  ombres et effets, ciel), les appels de dessin, les objets, les pixels
  (taille × densité de l'écran), les lumières, les créatures simulées, les
  données envoyées au GPU ;
- une phrase qui dit ce qui limite le plus (processeur ou carte graphique).

Mesures faites dans `src/platform/web/main.cpp` (`Profile`) et le rendu
(`RenderStats`).

Résolution : l'image a au plus 1,6 million de pixels (les écrans de téléphone
sont très denses). Quand le jeu passe sous 48 images par seconde pendant une
seconde, la résolution baisse par paliers (jusqu'à 60 %), et remonte quand il
redevient fluide ; elle ne remonte pas, pendant une demi-minute, à un palier
trouvé trop lent. Paramètres → « Baisser la résolution quand le jeu
ralentit » (activé par défaut) : décoché, l'image garde toujours sa
résolution. Le panneau de détail montre la résolution du moment.

## Personnages et animations

Chaque personnage est un assemblage de pièces rigides (fichiers `player_*`,
`companion_*`, `slime_*`, `mushroom_*`, `bird_*`, `eye` dans `assets/models/`),
chacune avec son origine à l'articulation. `src/render/characters.cpp` décrit
les squelettes (quelle pièce est attachée où) et les anime par le code à partir
de l'état du corps : vitesse, au sol ou en l'air, temps (marche, trot, sauts
avec étirement et écrasement, battements d'ailes, queue qui remue,
clignements des yeux). L'animation est purement visuelle ; les déplacements et
comportements des créatures, eux, font partie de la simulation
(`src/core/world.cpp`) et de la sauvegarde.

## Compiler et lancer

```bash
emcmake cmake --preset web-debug
cmake --build --preset web-debug
python3 -m http.server 8080 --directory build/web-debug/site
```

Puis ouvrir http://localhost:8080. Paramètres d'URL utiles : `?seed=42`,
`?new` (ignore la sauvegarde), `?frozen` (le temps ne s'écoule pas, pour les
tests). Dans la console : `monstres.state()`, `monstres.input.move(0, 1)`,
`monstres.step(60)`, `monstres.setCamera(yaw, pitch)`, `monstres.teleport(x, z)`.

## Sauvegardes

La partie est sauvegardée automatiquement (toutes les 5 secondes, et à
l'ouverture d'un menu) dans IndexedDB. Il y a **trois emplacements**
(`/save/game.sav`, `game2.sav`, `game3.sav` ; `/save/current` dit lequel est
joué). « Gérer les sauvegardes », dans les paramètres, ouvre leur fenêtre
(`web/saves.js`, le temps s'arrête) ; pour chacun : le nom du magicien, son
niveau, la taille du fichier, la date et l'heure de la sauvegarde, et

- **Jouer** : la partie en cours est sauvegardée, l'autre est chargée ;
- **Exporter** : une copie dans un fichier `monstres.sav` ;
- **Supprimer** (après confirmation) : jamais la partie en cours, il faut
  d'abord en jouer une autre ;
- dans un emplacement vide, **Nouvelle partie** (la création du personnage,
  voir Nouvelle partie) ou **Importer** un fichier (il est vérifié en entier,
  puis rangé là sans être joué).

Rien n'écrit jamais par-dessus une sauvegarde existante. Le résumé d'une
sauvegarde est lu sans construire son monde (`peek_save`,
`src/core/save.cpp`). Côté jeu : `monstres_saves_json`, `monstres_play_slot`,
`monstres_delete_slot`, `monstres_new_game`, `monstres_import_save`
(`src/platform/web/main.cpp`) ; dans la console : `monstres.saves()`,
`monstres.playSave(n)`, `monstres.newGame(n, nom, graine, apparence, compagnon)`.

## Sécurité

Le jeu ne parle à aucun serveur ; ce qui vient de l'extérieur est le fichier
de sauvegarde importé, les paramètres d'URL et ce que la page envoie au jeu.

- **Sauvegarde importée** : tout y est vérifié à la lecture (`src/core/save.cpp` :
  tailles, espèces, gènes, positions dans le monde, vitesses et cibles
  plausibles, téléporteurs connus...) ; un fichier de plus de 8 Mo n'est pas
  lu. `test_save.cpp` abîme une sauvegarde au hasard : elle est refusée, ou
  donne un monde qui se joue, s'affiche et se sauvegarde (avec les sanitizers).
- **Page** : une `Content-Security-Policy` n'autorise que les fichiers du site
  (aucun script écrit dans la page, rien d'ailleurs) ; les textes venus du
  jeu sont posés avec `textContent`, jamais interprétés comme du HTML.
- **API du jeu** (`main.cpp`) : les nombres reçus de JavaScript sont bornés
  (taille des portraits, commandes, valeurs qui ne sont pas des nombres).

## Tests

```bash
scripts/test.sh --quick    # rapide : natif optimisé + site + Chromium seul
scripts/test.sh            # complet : sanitizers, WebAssembly, trois navigateurs
scripts/test.sh --no-e2e   # sans les navigateurs
ctest --preset native-release         # tous les tests natifs, en quelques secondes
ctest --preset native-debug -L unit   # une seule catégorie : unit, functional, regression
ctest --preset native-debug -R world  # un seul fichier (unit_world)
```

Chaque fichier de tests est un test ctest à part : ctest les lance côte à
côte (7 au plus, réglé dans `CMakePresets.json`). Les sanitizers (ASan, UBSan)
rendent le jeu environ 40 fois plus lent : sous eux, les plus longues parties
aléatoires sont raccourcies (`kSanitized`, `tests/support/test_helpers.hpp`)
et les mondes générés sont mis en cache par graine (`cached_game`) ; la
version optimisée joue tout en entier.

Après un changement **voulu** des règles, régénérer les références puis relire
le diff avant de valider :

```bash
MONSTRES_UPDATE_GOLDEN=1 ctest --preset native-debug -L regression
```

Les rejeux sont de petits scripts (`tests/regression/data/replays/*.replay`,
format décrit dans `tests/support/replay.hpp`).

Quand le format de sauvegarde change, augmenter `kSaveVersion`
(`src/core/include/monstres/core/save.hpp`) et lancer la commande ci-dessus
pour créer `v<N>_reference.sav`. Les fichiers des versions précédentes restent :
ils doivent soit se charger, soit être refusés proprement (leur `.expected`
contient alors `rejected <erreur>`, comme `v1_reference` du prototype 2D).

### Tests navigateurs (Playwright)

Nécessite npm (`sudo apt install npm`), puis une fois :

```bash
cd tests/e2e && npm install && npx playwright install chromium firefox webkit
```

Créer les captures de référence au premier lancement : `npm run test:update`.
