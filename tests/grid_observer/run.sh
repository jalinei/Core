#!/usr/bin/env bash
set -euo pipefail
repo_root=$(cd "$(dirname "$0")/../.." && pwd)
test_dir=$(mktemp -d)
trap 'rm -rf "$test_dir"' EXIT
"${CXX:-g++}" -std=c++17 -Wall -Wextra -Werror -fsanitize=undefined \
    -I"$repo_root/src/grid_observer" \
    "$repo_root/tests/grid_observer/test_grid_observer.cpp" \
    "$repo_root/src/grid_observer/gridObserver.cpp" \
    -o "$test_dir/test_grid_observer"
"$test_dir/test_grid_observer"
