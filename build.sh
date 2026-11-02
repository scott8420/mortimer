#!/usr/bin/env bash
# build.sh -- build Mortimer (./build/mortimer and ./build/mortimer_selftest).
# Deps, Fedora: sudo dnf install gtkmm4.0-devel spdlog-devel cmake gcc-c++
#               (optional, faster: ninja-build ccache mold)
# Deps, Ubuntu: sudo apt install libgtkmm-4.0-dev libspdlog-dev cmake g++
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")"
GEN=()
# Ninja only when the build dir is fresh: CMake can't switch generators later.
if [[ ! -f build/CMakeCache.txt ]] && command -v ninja >/dev/null 2>&1; then GEN=(-G Ninja); fi
cmake -B build "${GEN[@]}"
cmake --build build -j"$(nproc)"
echo ""
echo "Built: ./build/mortimer, ./build/mortimer-gen and ./build/mortimer_selftest"
