#!/usr/bin/env bash
# Runs every test: native (with sanitizers), WebAssembly under Node, then the
# browser tests if Playwright is installed.
#
#   scripts/test.sh            everything
#   scripts/test.sh --no-e2e   skip the browser tests
set -euo pipefail
cd "$(dirname "$0")/.."

run_e2e=1
[[ "${1:-}" == "--no-e2e" ]] && run_e2e=0

echo "== Natif (debug + sanitizers) =="
cmake --preset native-debug >/dev/null
cmake --build --preset native-debug -j"$(nproc)"
ctest --preset native-debug

echo "== WebAssembly sous Node =="
emcmake cmake --preset web-release >/dev/null
cmake --build --preset web-release -j"$(nproc)"
ctest --preset web-release

if [[ $run_e2e == 1 ]]; then
  echo "== Navigateurs (Playwright) =="
  if [[ -d tests/e2e/node_modules/@playwright/test ]]; then
    (cd tests/e2e && npx playwright test)
  else
    echo "Playwright n'est pas installé : voir README.md (section Tests). Ignoré."
  fi
fi
