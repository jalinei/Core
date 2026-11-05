# Sourced by tests/l0/*/run.sh. Builds one L0 test binary on the host.
#
#   source "$(dirname "$0")/../../common/l0_build.sh"
#   l0_build <name> --strict <our sources...> --lib <third-party sources...>
#   "$L0_BIN/<name>"
#
# Our sources (tests and src/) build with -Werror. Third-party library sources
# (owntech/lib/...) build without -Werror: their warnings are not ours to fix
# here, and must not hide behind a test failure.
#
# Include paths: hardware stand-ins, check.h, every src/ subfolder, and the
# libraries fetched by PlatformIO for the default (USB) environment.

set -euo pipefail

REPO_ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
LIB_ROOT="$REPO_ROOT/owntech/lib/USB"
L0_BIN=$(mktemp -d)
trap 'rm -rf "$L0_BIN"' EXIT

L0_CXX=${CXX:-g++}
L0_FLAGS=(-std=c++17 -O1 -g -fsanitize=undefined -fno-sanitize-recover=undefined)
L0_INCLUDES=(
    -I"$REPO_ROOT/tests/common/shim"
    -I"$REPO_ROOT/tests/common"
    -I"$REPO_ROOT/src"
    -I"$REPO_ROOT/src/boost_converter"
    -I"$REPO_ROOT/src/grid_observer"
    -I"$LIB_ROOT/control_library/src"
    -I"$LIB_ROOT/single_phase_inverter/src"
)

l0_build() {
    local name=$1; shift
    local mode=strict objects=() src obj
    for src in "$@"; do
        case "$src" in
            --strict) mode=strict; continue ;;
            --lib) mode=lib; continue ;;
        esac
        obj="$L0_BIN/$(echo "$src" | tr '/' '_').o"
        if [ "$mode" = strict ]; then
            "$L0_CXX" "${L0_FLAGS[@]}" -Wall -Wextra -Werror "${L0_INCLUDES[@]}" -c "$src" -o "$obj"
        else
            "$L0_CXX" "${L0_FLAGS[@]}" -w "${L0_INCLUDES[@]}" -c "$src" -o "$obj"
        fi
        objects+=("$obj")
    done
    "$L0_CXX" "${L0_FLAGS[@]}" "${objects[@]}" -lm -o "$L0_BIN/$name"
}
