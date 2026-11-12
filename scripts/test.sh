#!/usr/bin/env bash
# Runs the tests: native, WebAssembly under Node, then the browser tests if
# Playwright is installed. At most 7 cores (8 froze the machine).
#
#   scripts/test.sh            everything: sanitizers, three browsers (a few minutes)
#   scripts/test.sh --quick    the fast set: native release build, Chromium only
#   scripts/test.sh --no-e2e   everything but the browsers
set -euo pipefail
cd "$(dirname "$0")/.."

mode=full
run_e2e=1
for arg in "$@"; do
  case "$arg" in
    --quick) mode=quick ;;
    --no-e2e) run_e2e=0 ;;
    *) echo "Option inconnue : $arg" >&2; exit 2 ;;
  esac
done
jobs=7

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
