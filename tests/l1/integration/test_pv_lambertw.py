#!/usr/bin/env python3
## SPDX-License-Identifier: LGPL-2.1
## @file test_pv_lambertw.py
## @brief L1-INT-07: the Lambert W panel reproduces the diode panel, so long PV runs can use it.
"""The diode panel (Shockley diode solved by Newton at every step) is the reference.
The Lambert W panel is one current source set at every step from the terminal voltage
of the previous step: same single-diode equation, explicit, about 4x faster."""
from pathlib import Path
import json,os,sys
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import numpy as np
from plant.pv import PROFILE,reference,current,current_lambertw
import runner,runtime

DUTY=0.45        ##< Fixed duty that pulls the panel from open circuit through its MPP (open loop).
RUN_S=0.01       ##< Run length, s.
TOL_V=0.01       ##< Panel and bus voltage agreement, V.
TOL_A=0.01       ##< Panel current agreement, A.


def emit(id,verdict,description,limit,value):
    """Report one criterion in the campaign's line format."""
    print('\t'.join(['@@',id,verdict,description,limit,str(value)]),flush=True)
    return verdict!='FAIL'


def loaded_run(model,out):
    """@brief 10 ms with both legs at a fixed duty: the firmware block runs but is not obeyed.
    @param model 'diode' or 'lambertw'. @return (time, signals, manifest)."""
    class Fixed(runtime.Actuators):
        def __init__(self):super().__init__();self.enabled=[True,True];self.duties=[DUTY,DUTY]
        def apply(self,out):pass
    runner.Actuators=Fixed
    sc=json.loads((runtime.HERE/'scenarios/pv_mppt.json').read_text())
    sc.update(name=f'pv-{model}-fixed-duty',title=f'PV panel ({model}) at a fixed duty of {DUTY}',
              purpose=f'Open loop: both legs at duty {DUTY} from t = 0, bus precharged to 380 V with the 2.8 kΩ PV test load. '
                      'The panel is pulled from open circuit through its maximum power point. Compares the Lambert W panel with the diode panel (L1-INT-07).',
              criteria=['L1-INT-07'],duration_s=RUN_S,pv_model=model,
              commands=[{'time_s':0,'mode':0,'sync_request':1,'v_bus_ref':400,'vac_updated':1}])
    t,s,rows,meta=runner.run(sc);runner.save((t,s,rows,meta),out/sc['name'])
    return t,s,meta


def main():
    out=Path(os.environ.get('CORE_TEST_OUT',runtime.HERE.parent/'out/l1-development'));ok=True
    q=json.loads(PROFILE.read_text())['parameters'];ref=reference(q)
    v=np.linspace(0,1.05*ref['Voc_V'],400)
    err=float(np.max(np.abs(np.array([current(x,q) for x in v])-current_lambertw(v,q))))
    # @test L1-INT-07.a: The explicit Lambert W current equals the implicit single-diode solution.
    ok=emit('L1-INT-07.a','PASS' if err<=1e-9 else 'FAIL','Lambert W current equals the implicit diode equation, 0 to 1.05 Voc',
            '<= 1e-9 A',err) and ok
    d=loaded_run('diode',out);w=loaded_run('lambertw',out)
    diff={k:float(np.max(np.abs(d[1][k]-w[1][k]))) for k in ['pv_voltage','pv_current','VDCBus']}
    good=d[2]['finite_states'] and w[2]['finite_states'] and diff['pv_voltage']<=TOL_V and diff['VDCBus']<=TOL_V and diff['pv_current']<=TOL_A
    # @test L1-INT-07.b: Same switched circuit and firmware block, loaded through the MPP: same waveforms.
    ok=emit('L1-INT-07.b','PASS' if good else 'FAIL','Lambert W and diode panels agree, loaded through the MPP at a fixed duty',
            f'panel and bus <= {TOL_V} V, panel current <= {TOL_A} A at every stored point',diff) and ok
    emit('L1-INT-07.c','INFO','Speed of the Lambert W panel relative to the diode panel','informational',
         f"{d[2]['wall_s']/w[2]['wall_s']:.2f}x ({d[2]['wall_s']:.0f} s vs {w[2]['wall_s']:.0f} s)")
    return 0 if ok else 1


if __name__=='__main__':sys.exit(main())
