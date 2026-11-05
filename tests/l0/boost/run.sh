#!/usr/bin/env bash
# L0-BST: build and run the boost converter controller tests on the host.
source "$(dirname "$0")/../../common/l0_build.sh"
l0_build test_boost \
    --strict "$REPO_ROOT/tests/l0/boost/test_boost.cpp" \
             "$REPO_ROOT/src/boost_converter/boost.cpp" \
    --lib    "$LIB_ROOT/control_library/src/pid.cpp"
"$L0_BIN/test_boost"
