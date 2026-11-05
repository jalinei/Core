#!/usr/bin/env bash
## SPDX-License-Identifier: LGPL-2.1
## Copyright (c) 2026 OWNTECH TECHNOLOGIES
##
## @file run.sh
## @brief L0-GOB: builds and runs the grid observer tests on the host.
##
## Compiles test_grid_observer.cpp and src/grid_observer/gridObserver.cpp with
## -Werror, then runs the binary.
## @return The test binary's exit code: 0 when every criterion passes.
## @see README.md in this folder
##
## @par Usage
##   bash tests/l0/grid_observer/run.sh
source "$(dirname "$0")/../../common/l0_build.sh"
l0_build test_grid_observer \
    --strict "$REPO_ROOT/tests/l0/grid_observer/test_grid_observer.cpp" \
             "$REPO_ROOT/src/grid_observer/gridObserver.cpp"
"$L0_BIN/test_grid_observer"
