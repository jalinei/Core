#!/usr/bin/env python3
## SPDX-License-Identifier: LGPL-2.1
## @file busreg_balance.py
## @brief Finding #3 diagnostic: power balance, sampled vs averaged current and loop history of a saved run.
## @par Usage
##   .venv/bin/python tests/l1/diagnostics/busreg_balance.py tests/out/<campaign>/dcdc_busreg [t0 t1]
## @return Prints INFO only; never changes a verdict.
"""Reads waveforms.npz and control.csv from a scenario folder written by runner.py."""
from pathlib import Path
import csv,sys
import numpy as np

def main(folder, t0=0.08, t1=0.1):
    """@brief Print the diagnostic for one saved run.
    @param folder Scenario folder (waveforms.npz, control.csv).
    @param t0 Start of the analysis window, s.
    @param t1 End of the analysis window, s."""
    z=np.load(folder/'waveforms.npz');t=z['time'];w=(t>=t0)&(t<=t1)
    mean=lambda k:float(np.mean(z[k][w]))
    print(f'Window {t0*1e3:g}-{t1*1e3:g} ms (stored every {1e6*(t[1]-t[0]):g} us)')
    print('\n1. Power balance')
    p_in=mean('VLow')*abs(mean('Is(Vsource)'))
    vb=z['VDCBus'][w];dt=t[w][-1]-t[w][0]
    print(f'   source power          {p_in:8.2f} W')
    print(f'   bus voltage           {vb[0]:8.2f} -> {vb[-1]:.2f} V')
    print(f'   duty leg1 / leg2      {mean("duty_leg1"):8.3f} / {mean("duty_leg2"):.3f}')
    print(f'   current ref 1 / 2     {mean("i_ref1"):8.3f} / {mean("i_ref2"):.3f} A')
    print('\n2. Firmware current (sampled) vs physical current (averaged)')
    for s,p in [('ILow1','IP(DC1_Iin)'),('ILow2','IP(DC2_Iin)')]:
        print(f'   {s}: sampled mean {mean(s):8.4f} A, physical mean {mean(p):8.4f} A, physical min/max {z[p][w].min():.2f}/{z[p][w].max():.2f} A')
    print('\n3. Loop history (every 5 ms)')
    rows=list(csv.DictReader(open(folder/'control.csv')))
    print('   t (ms)   VDCBus   i_ref1  i_ref2  duty1   duty2')
    for r in rows[::50]:
        print(f"   {1e3*float(r['time_s']):6.1f}  {float(r['VDCBus']):7.2f}  {float(r['i_ref1']):6.3f}  {float(r['i_ref2']):6.3f}  {float(r['duty_leg1']):6.3f}  {float(r['duty_leg2']):6.3f}")

if __name__=='__main__':
    a=sys.argv[1:]
    if not a:sys.exit(__doc__)
    main(Path(a[0]),*(float(x) for x in a[1:3]))
