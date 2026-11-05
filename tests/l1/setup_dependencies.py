#!/usr/bin/env python3
## SPDX-License-Identifier: LGPL-2.1
## @file setup_dependencies.py
## @brief Fetch exact test-library revisions; never reset an existing checkout.
"""The existing SINGLE_PHASE_INVERTER_DIR override is applied at build/run time."""
from pathlib import Path
import json,subprocess
HERE=Path(__file__).resolve().parent;REPO=HERE.parents[1]
for name,spec in json.loads((HERE/'dependencies.lock.json').read_text()).items():
    target=REPO/'owntech/lib/USB'/name
    if target.exists():
        commit=subprocess.check_output(['git','-C',str(target),'rev-parse','HEAD'],text=True).strip()
        if commit!=spec['commit']:raise SystemExit(f'{target}: existing revision {commit} differs from test lock; resolve explicitly, no checkout changed')
    else:
        target.parent.mkdir(parents=True,exist_ok=True)
        subprocess.run(['git','clone',spec['url'],str(target)],check=True)
        subprocess.run(['git','-C',str(target),'checkout','--detach',spec['commit']],check=True)
    print(name,spec['commit'])
