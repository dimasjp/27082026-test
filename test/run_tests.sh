#!/usr/bin/env bash
# Build and run the host-side tests for the pure water-management logic.
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
out="$root/test/build"
mkdir -p "$out"

g++ -std=c++17 -Wall -Wextra -Werror -O1 \
    -I "$root/RumahPompa" \
    "$root/test/test_water_logic.cpp" \
    "$root/RumahPompa/water_logic.cpp" \
    -o "$out/test_water_logic"

"$out/test_water_logic"
