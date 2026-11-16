#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Local CI for Linux (also inside WSL/Ubuntu): the same steps as the "linux"
# job of .github/workflows/build.yml.
#   tools/ci-local.sh --deps   install the packages first (needs sudo)
#   tools/ci-local.sh          configure, build, test, translations, end to end
set -euo pipefail
cd "$(dirname "$0")/.."

if [ "${1:-}" = "--deps" ]; then
  sudo apt-get update
  sudo apt-get install -y git cmake g++ python3 ninja-build qt6-base-dev qt6-base-dev-tools qt6-tools-dev \
    qt6-tools-dev-tools qt6-l10n-tools qt6-qpa-plugins libgl1-mesa-dev libgl1-mesa-dri libeigen3-dev \
    nlohmann-json3-dev libgtest-dev xvfb libocct-foundation-dev libocct-modeling-data-dev \
    libocct-modeling-algorithms-dev libocct-data-exchange-dev libtbb-dev
  shift
fi

B=build-ci
step() { printf '\n==> %s\n' "$*"; }

step "Configure"
cmake -S . -B "$B" -G Ninja -DCMAKE_BUILD_TYPE=Release -DORV_WARNINGS_AS_ERRORS=ON
step "Build"
cmake --build "$B" --parallel
step "Unit tests"
ctest --test-dir "$B" --output-on-failure
step "Translations complete"
python3 tools/translate_es.py
git diff --exit-code -- translations/
step "End to end (the app driven through MCP)"
LIBGL_ALWAYS_SOFTWARE=1 xvfb-run -a -s "-screen 0 1600x1000x24" \
  python3 tests/e2e_mcp.py --app "$B/src/app/OpenRevers" --cli "$B/src/openrevers-cli"
printf '\nCI local: todo en verde.\n'
