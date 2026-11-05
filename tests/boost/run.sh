#!/usr/bin/env bash
set -euo pipefail

repo_root=$(cd "$(dirname "$0")/../.." && pwd)
test_dir=$(mktemp -d)
trap 'rm -rf "$test_dir"' EXIT

# Host-only shims for CMSIS types and Zephyr logging; use the real PID source.
mkdir -p "$test_dir/zephyr/logging"
cat > "$test_dir/arm_math.h" <<'HEADER'
#pragma once
#include <cstdint>
typedef float float32_t;
HEADER
cat > "$test_dir/zephyr/logging/log.h" <<'HEADER'
#pragma once
#define LOG_MODULE_DECLARE(...)
#define LOG_ERR(...)
#define LOG_DBG(...)
HEADER

"${CXX:-g++}" -std=c++17 -Wall -Wextra -Werror \
    -fsanitize=undefined \
    -I"$test_dir" -I"$repo_root/src/boost_converter" \
    -I"$repo_root/owntech/lib/USB/control_library/src" \
    "$repo_root/tests/boost/test_boost.cpp" \
    "$repo_root/src/boost_converter/boost.cpp" \
    "$repo_root/owntech/lib/USB/control_library/src/pid.cpp" \
    -o "$test_dir/test_boost"
"$test_dir/test_boost"
printf 'Boost control tests passed.\n'
