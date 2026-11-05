#!/usr/bin/env bash
# L0-GFL: build and run the grid-following inverter library tests on the host,
# against the copy PlatformIO fetched for the default (USB) environment.
source "$(dirname "$0")/../../common/l0_build.sh"
INV="$LIB_ROOT/single_phase_inverter/src"
CTL="$LIB_ROOT/control_library/src"
if [ ! -f "$INV/singlePhaseInverterFollowing.cpp" ]; then
    echo "single_phase_inverter not fetched: run a PlatformIO build first" >&2
    exit 1
fi
printf 'library commit: %s\n' "$(git -C "$INV" rev-parse --short HEAD 2>/dev/null || echo unknown)"
l0_build test_inverter_following \
    --strict "$REPO_ROOT/tests/l0/inverter_following/test_inverter_following.cpp" \
    --lib    "$INV/singlePhaseInverterFollowing.cpp" "$INV/sogi.cpp" \
             "$CTL/pid.cpp" "$CTL/transform.cpp" "$CTL/trigo.cpp"
"$L0_BIN/test_inverter_following"
