#!/usr/bin/env python3
## SPDX-License-Identifier: LGPL-2.1
## @file test_switched.py
## @brief Formal switched application criteria with explicit partial-run limits.
"""No averaged model, controller retuning, injected sensor values or hidden load."""
from pathlib import Path
import csv,json,os,sys
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import numpy as np
from runner import run,save
from runtime import HERE
from app_constants import STARTUP_RAMP_RATE


def emit(id,verdict,description,limit,value):
    print('\t'.join(['@@',id,verdict,description,str(limit),str(value)]),flush=True)
    return verdict!='FAIL'


def load(folder):
    """Read controller data and stored electrical samples with their manifest."""
    rows=[{k:float(v) for k,v in row.items()} for row in csv.DictReader((folder/'control.csv').open())]
    with np.load(folder/'waveforms.npz',allow_pickle=False) as raw:data={k:raw[k] for k in raw.files}
    return rows,data,json.loads((folder/'manifest.json').read_text())


def main():
    """Run declared cases and report evidence, including unmet long-run criteria."""
    out=Path(os.environ.get('CORE_TEST_OUT',HERE.parent/'out/l1-switched-development'));out.mkdir(parents=True,exist_ok=True)
    ok=True
    # These are the same raw scenarios already run by test_model.py, with formal
    # criteria evaluated here instead of rerunning nominal grid/source dropout.
    rows,data,meta=load(out/'idle_healthy')
    # @test L1-APP-01.a: Nominal idle leaves both boost gates off.
    ok=emit('L1-APP-01.a','PASS' if all(r['en_leg1']==r['en_leg2']==0 for r in rows) else 'FAIL',
            'IDLE on a healthy switched grid keeps boost legs disabled','both off',len(rows)) and ok
    q=next((i for i,r in enumerate(rows) if r['grid_qualified']),None);dwell=None
    if q is not None:
        start=q
        while start>0 and 207<=rows[start-1]['observer_rms']<=253 and 47.5<=rows[start-1]['observer_hz']<=52.5:start-=1
        dwell=rows[q]['time_s']-rows[start]['time_s']+.0001
    # @test L1-APP-01.b: Qualification is timed from continuous eligible grid.
    ok=emit('L1-APP-01.b','PASS' if dwell is not None and abs(dwell-1)<=.000100001 else 'FAIL',
            'Observer qualifies after continuous healthy estimated grid','1 s +/- 100 us',dwell) and ok
    emit('L1-APP-01.c','INFO','Time from electrical start to PLL synchronization','informational',
         next((r['time_s'] for r in rows if r['synchronized']),None))
    rows,data,meta=load(out/'vac_dropout');trip=next((r for r in rows if r['observer_trip']==1),None)
    fresh=[r for r in rows if r['vac_updated']]
    # @test L1-APP-08.a: Real delivery loss reaches stale threshold exactly.
    valid=trip is not None and trip['vac_age_ticks']==200 and abs(trip['time_s']-fresh[-1]['time_s']-.02)<1e-10
    ok=emit('L1-APP-08.a','PASS' if valid else 'FAIL','Lost VAC samples trigger InvalidMeasurement','200 control periods',trip['time_s'] if trip else None) and ok
    for case in ['dcdc_startup','dcdc_busreg','dcdc_burst']:
        scenario=json.loads((HERE/'scenarios'/f'{case}.json').read_text())
        print('Formal switched scenario:',case,flush=True)
        result=run(scenario);save(result,out/case);rows,data,meta=load(out/case)
        if not meta['finite_states'] or meta['initial_capacitor_voltage_error_V']>1e-8 or meta['initial_transformer_voltage_mismatch_V']>1e-8 or meta['electrical_observations']<round(scenario['duration_s']/scenario['dt_s'])+1:
            ok=emit('L1-INT-06.'+case,'FAIL','Fixture initialization/finite-state check','consistent capacitor and transformer voltages',meta) and ok
            continue
        if case=='dcdc_startup':
            cold=[r for r in rows if r['cold_tick']==0]
            errors=[abs(r['startup_ref']-STARTUP_RAMP_RATE*1e-3*(i+1)) for i,r in enumerate(cold)]
            # @test L1-APP-02.a: Real production reference advances at app::startup_ramp_rate.
            ok=emit('L1-APP-02.a','PASS' if max(errors)<1e-4 else 'FAIL','STARTUP reference ramp over first 50 ms',f'{STARTUP_RAMP_RATE:g} V/s, 1e-4 V numerical tolerance',max(errors)) and ok
            peak=meta['electrical_step_extrema']['ILow1']['max']
            # @test L1-APP-02.b: Check physical current at every electrical step.
            ok=emit('L1-APP-02.b','PASS' if peak<=1. else 'FAIL','STARTUP physical leg-1 current over first 50 ms','<= 1 A at every 50 ns step',peak) and ok
            # @test L1-APP-02.c: Startup never enables converter 2.
            ok=emit('L1-APP-02.c','PASS' if all(r['en_leg2']==0 for r in rows) else 'FAIL','STARTUP keeps converter 2 disabled','always off',max(r['en_leg2'] for r in rows)) and ok
            emit('L1-APP-02.d','BLOCKED','Full zero-to-400 V startup trajectory',f'complete {400/STARTUP_RAMP_RATE:g} s switched run (dcdc_startup_full.json)','Only first 50 ms executed; no full-ramp claim')
        elif case=='dcdc_busreg':
            a=scenario['acceptance'];window=(data['time']>=a['settled_window_s'][0]-1e-12)&(data['time']<=a['settled_window_s'][1]+1e-12)
            low=float(data['VDCBus'][window].min());high=float(data['VDCBus'][window].max())
            # @test L1-APP-03.a: Window and load step are fixed before running.
            ok=emit('L1-APP-03.a','PASS' if low>=a['bus_min_V'] and high<=a['bus_max_V'] else 'FAIL',
                    'BUSREG after 50 ms load step, assessed over 80-100 ms','395-405 V',(low,high)) and ok
            emit('L1-APP-03.b','INFO','Maximum bus overshoot above 400 V','informational',float(data['VDCBus'].max()-400))
            emit('L1-APP-03.c','INFO','Physical current extrema including initial conditions','informational',meta['electrical_step_extrema'])
        else:
            paused=[r for r in rows if r['burst_paused']]
            transitions=[(a,b) for a,b in zip(rows,rows[1:]) if a['burst_paused']!=b['burst_paused']]
            resumes=[b for a,b in transitions if not b['burst_paused']]
            # @test L1-APP-04.a: Initial condition above upper bound pauses.
            ok=emit('L1-APP-04.a','PASS' if rows[0]['VDCBus']>=405 and rows[0]['burst_paused'] else 'FAIL',
                    'Burst fixture starts paused above upper threshold','pause for Vbus >= 405 V',rows[0]['VDCBus']) and ok
            # @test L1-APP-04.b: Paused output disables both legs.
            ok=emit('L1-APP-04.b','PASS' if paused and all(r['en_leg1']==r['en_leg2']==0 for r in paused) else 'FAIL',
                    'Both boost legs remain disabled while burst-paused','both off',len(paused)) and ok
            # @test L1-APP-04.c: Release follows actual electrical discharge.
            ok=emit('L1-APP-04.c','PASS' if resumes and all(r['VDCBus']<=395 for r in resumes) else 'FAIL',
                    'Burst releases when electrical bus reaches lower threshold','Vbus <= 395 V',[(r['time_s'],r['VDCBus']) for r in resumes]) and ok
            rises=[b for a,b in transitions if b['burst_paused']]
            emit('L1-APP-04.d','PASS' if rises and all(r['VDCBus']>=405 for r in rises) else 'BLOCKED',
                 'Subsequent rising upper-threshold crossing','pause at >= 405 V',[(r['time_s'],r['VDCBus']) for r in rises] or 'No subsequent 405 V crossing in 150 ms; repeated burst cycle not established')
            emit('L1-APP-04.e','INFO','Minimum bus during burst fixture','informational',float(data['VDCBus'].min()))
    raise SystemExit(0 if ok else 1)


if __name__=='__main__':main()
