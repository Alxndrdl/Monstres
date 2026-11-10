# Monstres

Petit jeu en C++ compilé en WebAssembly avec Emscripten, rendu WebGL2
(OpenGL ES 3.0), installable en PWA et jouable hors ligne.

Contrôles : flèches ou ZQSD pour se déplacer, Espace pour attaquer, Entrée pour
rejouer après une défaite.

## Organisation

```
CMakeLists.txt, CMakePresets.json   build natif et web
cmake/                              options de compilation communes
src/core/        monstres_core      règles du jeu, sauvegarde : C++ pur, sans navigateur
src/render/      monstres_render    monde -> liste de quads (testable en natif)
  gles3/         monstres_render_gles3   backend OpenGL ES 3.0 / WebGL2
src/platform/web/                   main(), boucle, clavier, IndexedDB, API JS (pre.js, post.js)
web/                                index.html (gabarit Emscripten), sw.js, manifeste, icônes
assets/                             meshes et textures sources (à venir)
tests/unit/                         tests unitaires (doctest)
tests/functional/                   scénarios de jeu complets
tests/regression/                   rejeux + anciennes sauvegardes comparés à des références
tests/e2e/                          tests navigateurs Playwright (Chromium, Firefox, WebKit)
tools/                              outils (génération des icônes, plus tard conversion d'assets)
scripts/test.sh                     lance tous les tests
```

Les règles n'utilisent que des entiers et un générateur aléatoire PCG32 à
graine : une même partie donne exactement le même état en natif et en
WebAssembly. Les tests de régression le vérifient.

## Compiler et lancer

```bash
emcmake cmake --preset web-debug
cmake --build --preset web-debug
python3 -m http.server 8080 --directory build/web-debug/site
```

Puis ouvrir http://localhost:8080. Paramètres d'URL utiles : `?seed=42`,
`?new` (ignore la sauvegarde), `?frozen` (le temps ne s'écoule pas, pour les
tests). Dans la console : `monstres.state()`, `monstres.step('right')`.

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

Quand le format de sauvegarde change, augmenter `kSaveVersion`
(`src/core/include/monstres/core/save.hpp`), garder la lecture des anciennes
versions, et lancer la commande ci-dessus pour créer `v<N>_reference.sav`.
Les fichiers des versions précédentes ne doivent jamais être supprimés.

### Tests navigateurs (Playwright)

Nécessite npm (`sudo apt install npm`), puis une fois :

```bash
cd tests/e2e && npm install && npx playwright install chromium firefox webkit
```

Créer les captures de référence au premier lancement : `npm run test:update`.
