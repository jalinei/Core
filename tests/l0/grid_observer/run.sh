#!/usr/bin/env bash
# L0-GOB: build and run the grid observer tests on the host.
source "$(dirname "$0")/../../common/l0_build.sh"
l0_build test_grid_observer \
    --strict "$REPO_ROOT/tests/l0/grid_observer/test_grid_observer.cpp" \
             "$REPO_ROOT/src/grid_observer/gridObserver.cpp"
"$L0_BIN/test_grid_observer"
