#!/usr/bin/env bash
## SPDX-License-Identifier: LGPL-2.1
## @file study.sh
## @brief Launch a study detached from the terminal, keeping the machine awake until it ends.
## @par Usage
##   tests/l1/study.sh tests/l1/studies/<study>.json [--jobs N]
## @par Follow and stop
##   journalctl --user -fu l1-study          # progress
##   systemctl --user stop l1-study          # stop (finished cases are kept)
## @details Runs under systemd --user as unit l1-study, so closing VS Code or the terminal does not
## stop it; systemd-inhibit blocks suspend, idle sleep and the lid-close suspend while it runs (the machine must stay on mains power). The review page is
## tests/out/study_<name>_<date>_<time>/report.html, rewritten after every finished case.
set -euo pipefail
repo=$(cd "$(dirname "$0")/../.." && pwd)
[ $# -ge 1 ] || { sed -n 's/^## \{0,1\}//p' "$0"; exit 2; }
study=$(realpath "$1"); shift
systemd-run --user --unit=l1-study --collect --working-directory="$repo" \
    systemd-inhibit --what=sleep:idle:handle-lid-switch --who="Core L1 study" --why="Simulating $(basename "$study")" \
    "$repo/.venv/bin/python" "$repo/tests/l1/study.py" "$study" "$@"
echo "Started. Follow: journalctl --user -fu l1-study   Stop: systemctl --user stop l1-study"
