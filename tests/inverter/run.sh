#!/usr/bin/env bash
set -euo pipefail
repo_root=$(cd "$(dirname "$0")/../.." && pwd)
test_dir=$(mktemp -d)
trap 'rm -rf "$test_dir"' EXIT
mkdir -p "$test_dir/zephyr/logging"
# Host CMSIS math/type and logging shims; all control/filter code is production code.
cat > "$test_dir/arm_math.h" <<'HEADER'
#pragma once
#include <cstdint>
#include <cmath>
typedef float float32_t;
#define PI 3.14159265358979323846F
inline float arm_sin_f32(float x) { return std::sin(x); }
inline float arm_cos_f32(float x) { return std::cos(x); }
HEADER
cat > "$test_dir/arm_math_types.h" <<'HEADER'
#pragma once
#include "arm_math.h"
HEADER
cat > "$test_dir/my_types.h" <<'HEADER'
#pragma once
#include "arm_math.h"
HEADER
cat > "$test_dir/zephyr/logging/log.h" <<'HEADER'
#pragma once
#define LOG_MODULE_DECLARE(...)
#define LOG_ERR(...)
#define LOG_DBG(...)
HEADER
control_src="$repo_root/owntech/lib/USB/control_library/src"
inverter_src="$repo_root/src/single_phase_inverter/src"
"${CXX:-g++}" -std=c++17 -Wall -Wextra -Werror -fsanitize=undefined,float-cast-overflow -fno-sanitize-recover=all \
    -I"$test_dir" -I"$inverter_src" -I"$control_src" -I"$repo_root/src/grid_observer" \
    "$repo_root/tests/inverter/test_inverter.cpp" \
    "$inverter_src/singlePhaseInverterFollowing.cpp" "$inverter_src/singlePhaseInverterForming.cpp" "$inverter_src/sogi.cpp" \
    "$repo_root/src/grid_observer/gridObserver.cpp" \
    "$control_src/pid.cpp" "$control_src/trigo.cpp" "$control_src/transform.cpp" \
    -o "$test_dir/test_inverter"
"$test_dir/test_inverter"
