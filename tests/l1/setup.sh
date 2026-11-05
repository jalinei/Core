#!/usr/bin/env bash
## SPDX-License-Identifier: LGPL-2.1
## @file setup.sh
## @brief Install pinned host-test dependencies without changing existing checkouts.
## @return Nonzero on a conflicting dependency revision or failed environment setup.
## @par Usage
##   tests/l1/setup.sh
set -euo pipefail
repo=$(cd "$(dirname "$0")/../.." && pwd)
python3 "$repo/tests/l1/setup_dependencies.py"
if [ ! -x "$repo/.venv/bin/python" ]; then python3 -m venv "$repo/.venv"; fi
"$repo/.venv/bin/python" -m pip install -r "$repo/tests/l1/plant/requirements.txt"
