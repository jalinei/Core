#!/usr/bin/env python3
## SPDX-License-Identifier: LGPL-2.1
## @file assess_pv_source.py
## @brief Standalone PV source feasibility bench, outside formal campaign verdicts.
"""Compare native Shockley PV circuit with an independent scalar equation.

Synthetic parameters demonstrate feasibility; they describe no particular panel.
Run from Core: .venv/bin/python tests/l1/plant/assess_pv_source.py
"""
import json
import numpy as np
import pulsim as p
from scipy.optimize import brentq, minimize_scalar


def main():
    """Sweep clamped terminal voltage and report source-equation agreement."""
    if p.__version__ != '2.0.0':
        raise RuntimeError('Assessment was defined for pulsim==2.0.0')
    light, a, shunt, series = 1.6, 2., 1000., .3
    saturation = light / np.expm1(45 / a)

    def current(v):
        return brentq(lambda i: light - saturation * np.expm1((v+i*series)/a)
                      - (v+i*series)/shunt - i, -10, light)

    observations = []
    for voltage in [0., 20., 35., 40., 45.]:
        b = p.CircuitBuilder()
        b.add_current_source('light', 'junction', '0', light)
        # Effective module a = n*Ns*k*T/q, passed as n=1 and V_T=a.
        b.add_shockley_diode('diode', 'junction', '0', I_S=saturation,
                            n=1., V_T=a, G_min=1e-12)
        b.add_resistor('shunt', 'junction', '0', shunt)
        b.add_resistor('series', 'junction', 'pv', series)
        b.add_voltage_source('clamp', 'pv', '0', voltage)
        result = p.simulate(b, t_end=1e-5, dt=1e-6, engine='pwl',
                            max_newton_iterations=100)
        actual = float(np.asarray(result.states)[-1,
                       b.state_var_names().index('Is(clamp)')])
        reference = current(voltage)
        observations.append(dict(voltage_V=voltage, native_current_A=actual,
                                 reference_current_A=reference,
                                 absolute_error_A=abs(actual-reference)))
    voc = brentq(current, 40, 46)
    mpp = minimize_scalar(lambda v: -v*current(v), bounds=(0, voc), method='bounded')
    print(json.dumps(dict(pulsim=p.__version__, synthetic=True,
                         parameters=dict(IL_A=light, I0_A=saturation, a_V=a,
                                         Rs_ohm=series, Rsh_ohm=shunt),
                         Voc_V=voc, Vmp_V=mpp.x, Imp_A=current(mpp.x),
                         Pmp_W=-mpp.fun, observations=observations), indent=2))
    if max(row['absolute_error_A'] for row in observations) > 1e-7:
        raise SystemExit('Native PV source disagrees with reference equation')


if __name__ == '__main__':
    main()
