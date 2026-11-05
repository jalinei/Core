#!/usr/bin/env bash
## SPDX-License-Identifier: LGPL-2.1
## @file open_gui.sh
## @brief Build Core's actual firmware block and open its derived PulsimGUI model.
## @return GUI process status.
## @par Usage
##   tests/l1/open_gui.sh [--check|--check-run]
set -euo pipefail
repo=$(cd "$(dirname "$0")/../.." && pwd)
bash "$repo/tests/l1/firmware_block/build.sh"
exec "$repo/.venv/bin/python" "$repo/tests/l1/gui.py" "$@"
