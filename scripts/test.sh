#!/usr/bin/env bash
# Runs the tests: native, WebAssembly under Node, then the browser tests if
# Playwright is installed. At most 7 cores (8 froze the machine).
#
#   scripts/test.sh --only MOT  pendant le travail : les tests d'un sujet (quelques secondes)
#                               - natifs : les fichiers dont le nom contient MOT (unit_battle...)
#                                 et les cas dont le titre le contient ;
#                               - navigateur (Chromium) : les tests dont le titre le contient.
#   scripts/test.sh --quick     avant un commit : natif optimisé, site, Chromium seul (~4 min)
#   scripts/test.sh             avant un push : sanitizers, WebAssembly, trois navigateurs (~10 min)
#   scripts/test.sh --no-e2e    sans les navigateurs (combinable)
set -euo pipefail
cd "$(dirname "$0")/.."

mode=full
run_e2e=1
only=''
while [[ $# -gt 0 ]]; do
  case "$1" in
    --quick) mode=quick ;;
    --no-e2e) run_e2e=0 ;;
    --only)
      [[ $# -ge 2 && -n $2 ]] || { echo "--only attend un mot (ex. : --only battle)" >&2; exit 2; }
      mode=only
      only=$2
      shift ;;
    *) echo "Option inconnue : $1" >&2; exit 2 ;;
  esac
  shift
done
jobs=7

if [[ $mode == only ]]; then
  echo "== Natif (release) : « $only » =="
  cmake --preset native-release >/dev/null
  cmake --build --preset native-release -j$jobs
  # The files whose name has the word...
  if ctest --preset native-release -N -R "$only" | grep -q "Test *#"; then
    ctest --preset native-release -R "$only"
  fi
  # ...and the cases whose title has it, in every file (doctest: case-insensitive).
  for kind in unit functional regression; do
    out=$(build/native-release/tests/monstres_${kind}_tests --test-case="*$only*" --no-version 2>&1) || {
      echo "$out"; exit 1; }
    grep -q "test cases: *0 |" <<<"$out" || grep -E "test cases:" <<<"$out" | sed "s/^/$kind : /"
  done
  if [[ $run_e2e == 1 && -d tests/e2e/node_modules/@playwright/test ]]; then
    echo "== Navigateur (Chromium) : « $only » =="
    emcmake cmake --preset web-release >/dev/null
    cmake --build --preset web-release -j$jobs
    (cd tests/e2e && npx playwright test --project=chromium -g "$only" --pass-with-no-tests)
  fi
  exit 0
fi

if [[ $mode == quick ]]; then
  echo "== Natif (release, rapide) =="
  cmake --preset native-release >/dev/null
  cmake --build --preset native-release -j$jobs
  ctest --preset native-release
else
  echo "== Natif (debug + sanitizers) =="
  cmake --preset native-debug >/dev/null
  cmake --build --preset native-debug -j$jobs
  ctest --preset native-debug
fi

[[ $mode == quick ]] && echo "== WebAssembly (le site) ==" || echo "== WebAssembly sous Node =="
emcmake cmake --preset web-release >/dev/null
cmake --build --preset web-release -j$jobs
[[ $mode == quick ]] || ctest --preset web-release

if [[ $run_e2e == 1 ]]; then
  echo "== Navigateurs (Playwright) =="
  if [[ -d tests/e2e/node_modules/@playwright/test ]]; then
    if [[ $mode == quick ]]; then
      (cd tests/e2e && npx playwright test --project=chromium)
    else
      (cd tests/e2e && npx playwright test)
    fi
  else
    echo "Playwright n'est pas installé : voir README.md (section Tests). Ignoré."
  fi
fi
