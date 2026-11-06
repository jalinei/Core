#!/usr/bin/env python3
## SPDX-License-Identifier: LGPL-2.1
## @file open_loop.py
## @brief Open-loop runs of the accepted switched model, firmware out of the loop (L1-MOD).
## @par What it does
##   The test imposes the gate pattern: fixed duty on the boost legs, or a fixed sinusoidal
##   modulation on the inverter bridge (unipolar, as the board's initInverterUnipolar: leg A
##   high side follows D, leg B high side follows 1−D, both center-aligned 200 kHz, 100 ns dead
##   time on rising edges by default). No C-Block is attached, so a step costs ~10 µs.
## @par Variants of the accepted model (each listed in the run's changes)
##   boost: bus load and initial bus voltage (plant/fixtures.py), inverter and relays off.
##   inverter: ideal bus source; output open (relay open), a resistor in place of the grid
##   source (relay closed), or the grid (relay closed); boost legs off.
"""Hardware modelling only: the duty or modulation is what the test imposes, nothing decides."""
from pathlib import Path
import json,hashlib
import numpy as np
import pulsim as p
from pulsimgui.models.project import Project
from pulsimgui.models.component import ComponentType as T

HERE=Path(__file__).resolve().parent
CONTRACT=json.loads((HERE.parent/'contract.json').read_text())
PWM_HZ=CONTRACT['actuators']['dc_pwm_hz'];DEADTIME=CONTRACT['actuators']['deadtime_s']
CONTROL_PERIOD=CONTRACT['control_period_s']
GRID_PEAK=325.2691193458119;GRID_HZ=50.


def accepted():
    meta=json.loads((HERE/'params.yaml').read_text())
    if hashlib.sha256((HERE/'full_model.pulsim').read_bytes()).hexdigest()!=meta['model_sha256']:
        raise ValueError('Accepted model hash no longer matches params.yaml')
    return Project.load(HERE/'full_model.pulsim')


def window(phase,d,period,deadtime):
    """@brief Center-aligned high window of width d·T, rising-edge dead time on both switches.
    @return (main switch on, complementary switch on)."""
    start=(1-d)*period/2;end=(1+d)*period/2
    return (start+deadtime<=phase<end),(phase<start or phase>=end+deadtime)


class Case:
    """@brief One open-loop run. kind: 'boost' or 'inverter'. See module doc."""
    def __init__(self,spec):
        self.spec=spec;self.changes=[]
        pr=accepted();c=pr.get_active_circuit();parts={a.name:a for a in c.components.values()}
        if spec['kind']=='boost':
            from plant.fixtures import apply
            if 'vin_V' in spec:  # input source other than the accepted 40 V (board comparisons)
                parts['Vsource'].parameters['waveform']['value']=spec['vin_V']
                for n in ('DC1_C7','DC2_C7','DC1_C2','DC2_C2'):parts[n].parameters['initial_voltage']=spec['vin_V']
                self.changes.append(f'input source {spec["vin_V"]:g} V')
            apply(pr,{'initial_bus_V':spec['initial_bus_V'],'load_ohm':spec['load_ohm']})
            self.changes+= [f'bus load {spec["load_ohm"]:.4g} Ω (DCDC_test_load), bus precharged to {spec["initial_bus_V"]:.4g} V '
                            '(converter capacitors initialized consistently, plant/fixtures.py)']
        else:
            vg=parts['Vgrid']
            if spec['output']=='resistor':
                vg.type=T.RESISTOR;vg.parameters={'resistance':spec['load_ohm']}
                self.changes.append(f'grid source replaced by a {spec["load_ohm"]:.4g} Ω resistor; relay closed')
            elif spec['output']=='open':self.changes.append('relay open: bridge and filter unloaded')
            else:self.changes.append('grid 230 V / 50 Hz (accepted model); relay closed')
            parts['Cbus'].parameters['initial_voltage']=spec['bus_V']
        from plant.model import Model
        self.model=Model(pr);b=self.model.builder
        if spec['kind']=='inverter':
            # Ideal bus: the boost is not part of this test.
            b.add_resistor('MOD_bus_r','MOD_bus','Vout',1e-3);b.add_voltage_source('MOD_bus_source','MOD_bus','0',spec['bus_V'])
            self.changes.append(f'ideal {spec["bus_V"]:g} V source on the DC bus through 1 mΩ; boost legs off')
        self.names=b.state_var_names();self.ix={n:i for i,n in enumerate(self.names)}
        self.dt=spec['dt_s'];self.T=1/PWM_HZ;self.td=spec.get('deadtime_s',DEADTIME)
        if spec['kind']=='inverter':self.changes.append(f'bridge dead time {self.td*1e9:g} ns')

    # --- gate patterns
    def duty_bridge(self,t):
        """@brief Leg A duty: held per control tick, computed at the tick's center (the board
        updates the duty every 100 µs; the half-tick advance cancels the hold delay)."""
        s=self.spec;k=np.floor((t+1e-12)/CONTROL_PERIOD)
        tc=(k+0.5)*CONTROL_PERIOD
        return 0.5+0.5*s['m']*np.sin(2*np.pi*GRID_HZ*tc+np.deg2rad(s.get('phase_deg',0.)))

    def gates(self,t):
        s=self.spec;phase=(t+1e-14)%self.T;g={}
        for i in (1,2):
            on=s['kind']=='boost' and i in s['legs']
            low,high=window(phase,s.get('duty',0.),self.T,DEADTIME) if on else (False,False)
            g[f'DC{i}_QL']=low;g[f'DC{i}_QH']=high
        if s['kind']=='inverter':
            d=self.duty_bridge(t)
            g['INV_AH'],g['INV_AL']=window(phase,d,self.T,self.td)
            g['INV_BH'],g['INV_BL']=window(phase,1-d,self.T,self.td)
            closed=s['output']!='open'
        else:
            for n in ['INV_AH','INV_AL','INV_BH','INV_BL']:g[n]=False
            closed=False
        g['Relay_L']=g['Relay_N']=closed
        return g

    def effective_duty(self):
        """@brief Fraction of a period the low switch is on, on the simulation's step grid."""
        n=int(round(self.T/self.dt));t=np.arange(n)*self.dt
        return float(np.mean([window((x+1e-14)%self.T,self.spec['duty'],self.T,DEADTIME)[0] for x in t]))

    def run(self):
        """@return (times, states) every store step."""
        b=self.model.builder;x=self.model.initial()
        if self.spec['kind']=='inverter':
            for n in ['Vout','MOD_bus']:
                if f'V({n})' in self.ix:x[self.ix[f'V({n})']]=self.spec['bus_V']
        mask=p.SwitchStateMask(b.graph.num_switches);sw={n:b.switch_index_of(n) for n in self.gates(0)}
        def switch(t):
            for n,v in self.gates(t).items():mask.set(sw[n],bool(v))
            return mask
        r=p.simulate(b,t_end=self.spec['duration_s'],dt=self.dt,engine='pwl',initial_state=x,switch_fn=switch,
                     store_every=self.spec.get('store_every',1),auto_regularize=False,max_event_iterations=32)
        return np.asarray(r.times),np.asarray(r.states)

    def v(self,states,node):
        return np.zeros(len(states)) if node=='0' else states[:,self.ix[f'V({node})']]
