#!/usr/bin/env python3
## SPDX-License-Identifier: LGPL-2.1
## @file app_constants.py
## @brief Read application constants from the production header, so L1 limits follow the firmware.
"""The C-Block does not report its tuning; parse `src/app/control_step.h` instead of copying values."""
from pathlib import Path
import re

HEADER = Path(__file__).resolve().parents[2] / 'src/app/control_step.h'


def app_constant(name):
    """@brief Value of `app::<name>` as declared in control_step.h.
    @param name Constant name, e.g. `startup_ramp_rate`.
    @return The value as a float; raises if the declaration is missing."""
    m = re.search(rf'\b{name}\s*=\s*([-+0-9.eE]+)F?\s*;', HEADER.read_text())
    if not m:
        raise SystemExit(f'{HEADER}: app::{name} not found')
    return float(m.group(1))


STARTUP_RAMP_RATE = app_constant('startup_ramp_rate')  ##< STARTUP bus reference slew rate, V/s.
