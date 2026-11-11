# Monstres

Petit jeu en C++ compilé en WebAssembly avec Emscripten, rendu WebGL2
(OpenGL ES 3.0), installable en PWA et jouable hors ligne.

Le joueur est un apprenti magicien (robe étoilée, chapeau pointu, bâton à
cristal lumineux) qui parcourt un monde magique de 150 m de côté avec son
compagnon Pompon, un renard des braises tout rond qui le suit et saute derrière
lui. Dans les prairies vallonnées vivent des monstres sauvages : Gloubi la
gelée sauteuse, Champi le champignon qui se dandine et Piou l'oiseau qui
s'enfuit quand on approche.

Au nord, le sentier mène au village : chaumières, maisons à cheminée qui fume,
huttes rondes, puits, barrières, panneaux, tonneaux et lanternes, des
villageois variés qui se promènent et saluent de la main, des corbeaux (Croa)
perchés sur les toits, et un lac où nagent des poissons (Bloup) qui sautent
hors de l'eau.

Au sud, sur une butte au bord du sentier, se dressent les ruines de la Tour :
la tour abandonnée d'un ancien magicien (visible de loin), les restes d'une
enceinte, une colonnade à moitié effondrée, des éboulis et quatre pierres à
runes qui brillent encore, entourées d'étincelles magiques.

Partout : herbes qui ondulent au vent, fleurs, arbres, sapins, rochers,
troncs, souches et buissons ; forêt au-delà des bords, montagnes enneigées,
nuages, ciel et soleil.

Contrôles :
- joystick en bas à gauche, ou ZQSD / flèches : se déplacer (par rapport à la caméra) ;
- glisser sur l'écran (doigt ou souris) : regarder autour de soi ;
- bouton en bas à droite, ou Espace : sauter.
- toucher ou cliquer sur un monstre (ou un villageois) : sa fiche s'affiche en
  haut à gauche (nom, famille, éléments, description, ses variantes, niveau,
  ce qu'il fait, distance) et un anneau doré le marque au sol ; toucher
  ailleurs ou × la ferme. Le menu contextuel, à droite, propose ce qu'on peut
  faire avec lui : « Combattre » un monstre (voir Combats) ;
- livre en haut à gauche (ou touches C, E, B, M) : le grimoire, qui met le jeu en
  pause, avec quatre onglets :
  - Personnage : portrait du magicien, nom (modifiable, enregistré dans la
    sauvegarde), niveau, expérience, caractéristiques, où l'on se trouve ;
  - Équipe : les monstres de l'équipe (le compagnon d'abord, 6 au plus) ;
    toucher l'un d'eux affiche sa fiche complète : niveau, expérience, points
    de vie, caractéristiques à son niveau, famille, éléments, variantes et ses
    trois attaques ;
  - Bestiaire : tous les monstres, avec pour chacun un portrait, la famille,
    le ou les éléments, une description, l'habitat, la taille, la vitesse, les
    caractéristiques et combien vivent dans ce monde ; le bouton ADN
    « Variantes » montre toutes ses variantes : « ? » si jamais vue, grisée si
    aperçue (un monstre qui la porte est passé à moins de 20 m), en clair si
    capturée ; toucher une variante découverte affiche sa fiche à droite de la grille
    (rareté, où la trouver, description) ;
  - Carte : locale (autour du joueur, avec les créatures proches) ou globale ;
  Échap le ferme ;
- engrenage en haut à droite : paramètres (exporter / importer la sauvegarde,
  afficher les FPS ; ce choix est retenu par le navigateur) et options de
  debug : un curseur pour choisir l'heure de la journée, et « Figer l'heure ».

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
web/                                index.html (gabarit Emscripten), controls.js (joystick,
                                    saut, regard, clavier), ui.js (paramètres, FPS, fiche
                                    des monstres), menu.js (grimoire : personnage,
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
1 m tirée de la graine du monde (collines, plus hautes vers les bords ; plat
autour du village ; fond du lac creusé ; petites bosses atténuées le long du
sentier). Entre deux points, le sol est fait de deux triangles par case,
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

## Modèles 3D

Les modèles sont des fichiers glTF binaires (`assets/models/*.glb`) : le jeu
les charge au démarrage (bibliothèque cgltf) et n'en contient aucune
géométrie. Ils sont générés par un script Python sans dépendance :

```bash
python3 tools/make_models.py           # régénère assets/models/
python3 tools/make_models.py --check   # vérifie qu'ils sont à jour (aussi lancé par ctest)
```

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
créature : ils ne changent rien d'autre au monde. Le bestiaire retient pour
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
peut échanger le compagnon, pas le rappeler. Un faisceau de lumière de la
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

## Combats

Règles dans `src/core/battle.hpp` / `battle.cpp`, interface dans
`web/battle.js`. Depuis le menu contextuel, « Combattre » lance un combat entre
le compagnon et le monstre visé (à moins de 15 m du joueur). Les Croa
(perchés ou en vol) et les Bloup du lac restent à leur place pendant le combat :
on les atteint avec les attaques à distance (projectiles visés en hauteur,
qui traversent le toit où se tient le corbeau) ou l'onde si on est assez près,
pas avec une morsure ; eux mordent ce qui s'approche et lancent leurs
projectiles. La caméra se place alors derrière
le compagnon, tournée vers l'ennemi : le joystick déplace le compagnon (en
avant : vers l'ennemi, sur les côtés : on tourne autour), le magicien attend.

Boutons en bas à droite pendant un combat :
- esquive (le bouton de saut, Espace) : un bond rapide dans la direction du
  déplacement (en arrière à l'arrêt), pendant lequel on ne peut pas être touché ;
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
rejoint l'équipe. Une victoire ou une capture donne de l'expérience au
compagnon (niveaux) et au magicien. Un compagnon K.O. revient avec 1 point de
vie ; hors combat, l'équipe regagne 1 point de vie par seconde. Les monstres
sauvages ont un niveau (1 à 3, 3 à 5 dans les ruines). Pas de forces ni de
faiblesses entre éléments pour l'instant. Les combats ne sont pas sauvegardés :
une partie rechargée pendant un combat reprend sans lui.

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
`monstres.step(60)`, `monstres.setCamera(yaw, pitch)`.

## Tests

```bash
scripts/test.sh            # natif + WebAssembly + navigateurs
scripts/test.sh --no-e2e   # sans les navigateurs
ctest --preset native-debug -L unit   # une seule catégorie : unit, functional, regression
```

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
