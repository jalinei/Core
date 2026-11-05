#!/usr/bin/env python3
## SPDX-License-Identifier: LGPL-2.1
## @file test_pv.py
## @brief Native PV source, passive-load verification and unchanged Core coupling.
"""Integration evidence only; tracking efficiency is an observation, not a pass."""
from pathlib import Path
import json,os,sys
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import numpy as np
import pulsim as p
from plant.pv import PROFILE, reference, current
from runner import run,save
from runtime import HERE


def emit(id,ok,description,value,status=None):
    print('\t'.join(['@@',id,status or ('PASS' if ok else 'FAIL'),description,'observations hold',str(value)]),flush=True)
    return ok


def main():
    """Check real electrical source/load and short full-model step refinement."""
    q=json.loads(PROFILE.read_text())['parameters'];ref=reference(q)
    b=p.CircuitBuilder();b.add_current_source('light','j','0',q['IL_A'])
    b.add_shockley_diode('junction','j','0',I_S=q['I0_A'],n=1.,V_T=q['a_V'],G_min=1e-12)
    b.add_resistor('shunt','j','0',q['Rsh_ohm']);b.add_resistor('series','j','pv',q['Rs_ohm'])
    b.add_resistor('load','pv','0',ref['Vmp_V']/ref['Imp_A'])
    r=p.simulate(b,t_end=1e-4,dt=1e-6,engine='pwl',max_newton_iterations=100)
    v=float(np.asarray(r.states)[-1,b.state_var_names().index('V(pv)')])
    # @test L1-INT-05.a: Coupled passive load reaches independently calculated MPP.
    ok=emit('L1-INT-05.a',abs(v-ref['Vmp_V'])<1e-5,'Native PV source and passive load match independent MPP',v)
    out=Path(os.environ.get('CORE_TEST_OUT',HERE.parent/'out/l1-pv-development'))
    base=json.loads((HERE/'scenarios/pv_mppt.json').read_text());base['duration_s']=.002
    runs=[]
    for dt in [50e-9,25e-9]:
        scenario={**base,'dt_s':dt};r=run(scenario);save(r,out/('pv-'+str(round(dt*1e9))+'ns'));runs.append(r)
    _,signals,rows,meta=runs[0]
    # @test L1-INT-05.b: Actual Core MPPT drives a source with free terminal voltage.
    ok=emit('L1-INT-05.b',meta['finite_states'] and meta['native_cblock'] and len(rows)==21 and
            all(r['boost_mode']==3 and r['block_error']==r['en_bridge']==r['relay_requested']==0 for r in rows)
            and np.ptp(signals['pv_voltage'])>.001 and np.any(signals['en_leg1']),
            'PV switched model executes actual Core MPPT, with bridge and relay off',len(rows)) and ok
    errors={n:float(np.max(abs(runs[0][1][n]-runs[1][1][n]))) for n in ['pv_voltage','VDCBus']}
    # @test L1-INT-05.c: Limited 2 ms step-refinement check, not global convergence proof.
    ok=emit('L1-INT-05.c',runs[1][3]['finite_states'] and max(errors.values())<.1,
            '50/25 ns PV integration agrees within 0.1 V over the initial 2 ms',errors) and ok
    # @test L1-INT-05.d: Report power without claiming MPPT acceptance or hiding a shortfall.
    emit('L1-INT-05.d',True,'Short PV run mean delivered power / available MPP (not settled acceptance)',
         float(np.mean(signals['pv_power'])/ref['Pmp_W']),status='INFO')
    raise SystemExit(0 if ok else 1)


if __name__=='__main__':main()
