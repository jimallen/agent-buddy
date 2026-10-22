#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
mkdir -p build
"${CXX:-clang++}" -std=c++17 -O1 -g -Wall -Wextra -Werror \
  -fsanitize=address,undefined -fno-omit-frame-pointer \
  tests/test_rig_fleet.cpp firmware/AgentCompanion/src/RigFleet.cpp \
  firmware/AgentCompanion/src/CharacterMotion.cpp firmware/AgentCompanion/src/SpriteMotion.cpp \
  firmware/AgentCompanion/src/Motion.cpp \
  -o build/test-rig-fleet
build/test-rig-fleet
