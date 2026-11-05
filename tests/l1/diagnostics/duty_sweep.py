#!/usr/bin/env python3
## SPDX-License-Identifier: LGPL-2.1
## @file duty_sweep.py
## @brief Finding #3, H3: power the switched DC/DC delivers at 400 V for fixed duties, without the controller.
## @par Usage
##   .venv/bin/python tests/l1/diagnostics/duty_sweep.py --out tests/out/duty-sweep [--duties 0.1 0.2 ...]
##   .venv/bin/python tests/l1/diagnostics/duty_sweep.py --out tests/out/duty-sweep --plot-only
##   Writes duty_sweep.json and duty_sweep.png (review-page conventions, procedure §8.5).
## @return Prints INFO only; never changes a verdict.
"""Open loop: the C-Block still runs (IDLE) but its outputs are ignored; both legs
switch at the same fixed duty through the same gate adapter as the campaign
(runtime.Actuators.gates). The bus starts at 400 V with the BUSREG load after the
step (20 kohm || 10 kohm, plus the model's 200 kohm bleed)."""
from pathlib import Path
from multiprocessing import Pool
import argparse,json,sys
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import numpy as np

LOAD_OHM=20000*10000/30000      ##< BUSREG load after the 50 ms step.
BLEED_OHM=200e3                 ##< Bleed resistor already in the model.
WINDOW=(0.02,0.04)              ##< Analysis window, s, after the start-up transient.

def one(duty):
    """@brief Run one fixed-duty case. @param duty Applied duty on both legs. @return Result dict."""
    import runner,runtime
    class Fixed(runtime.Actuators):
        """Holds both legs enabled at a fixed duty; ignores the firmware's outputs."""
        def __init__(self):super().__init__();self.enabled=[True,True];self.duties=[duty,duty]
        def apply(self,out):
            if out[runtime.INDEX['block_error']]:raise RuntimeError('C-Block error')
    runner.Actuators=Fixed
    scenario={'name':f'duty-sweep-{duty:g}','duration_s':WINDOW[1],'dt_s':5e-8,'store_period_s':1e-6,
              'fixture':{'initial_bus_V':400,'load_ohm':LOAD_OHM},
              'commands':[{'time_s':0,'mode':0,'sync_request':0,'v_bus_ref':400,'vac_updated':1}]}
    t,s,rows,meta=runner.run(scenario)
    from pulsimgui.models.project import Project
    c={a.name:a for a in Project.load(runtime.HERE/'plant/full_model.pulsim').get_active_circuit().components.values()}
    cbus=float(c['Cbus'].parameters['capacitance'])
    w=(t>=WINDOW[0])&(t<=WINDOW[1]);v=s['VP(DC_bus)'][w];tw=t[w]
    slope=float(np.polyfit(tw,v,1)[0]);r=1/(1/LOAD_OHM+1/BLEED_OHM)
    return {'duty':duty,'bus_start_V':float(v[0]),'bus_end_V':float(v[-1]),'bus_slope_V_per_s':slope,
            'source_power_W':float(np.mean(s['VP(DC1_Vin)'][w])*abs(np.mean(s['Is(Vsource)'][w]))),
            'delivered_power_W':float(np.mean(v**2)/r+cbus*np.mean(v)*slope),
            'i_leg1_mean_A':float(np.mean(s['IP(DC1_Iin)'][w])),'i_leg2_mean_A':float(np.mean(s['IP(DC2_Iin)'][w])),
            'i_leg1_minmax_A':[float(s['IP(DC1_Iin)'][w].min()),float(s['IP(DC1_Iin)'][w].max())],
            'finite':meta['finite_states'],'wall_s':meta['wall_s']}

def plot(out, firmware_duty=None):
    """@brief Draw duty_sweep.png from duty_sweep.json.
    @param out Folder holding duty_sweep.json.
    @param firmware_duty Optional {label: duty} drawn as dotted markers (e.g. duty reached in a campaign)."""
    import matplotlib
    matplotlib.use('Agg')
    import matplotlib.pyplot as plt
    sys.path.insert(0,str(Path(__file__).resolve().parents[2]/'common'))
    from review_report import SERIES,INK2,MUTED,GRID,SURFACE
    d=json.loads((out/'duty_sweep.json').read_text());r=d['results']
    x=[q['duty'] for q in r]
    fig,axes=plt.subplots(2,1,sharex=True,figsize=(9,6),gridspec_kw={'height_ratios':[2.2,1]},facecolor=SURFACE)
    for ax in axes:
        ax.set_facecolor(SURFACE);ax.grid(True,color=GRID,linewidth=.6);ax.tick_params(colors=INK2,labelsize=8)
        for sp in ax.spines.values():sp.set_visible(False)
        for label,v in (firmware_duty or {}).items():
            ax.axvline(v,color=MUTED,linestyle=':',linewidth=1)
    ax=axes[0]
    ax.plot(x,[max(q['delivered_power_W'],.1) for q in r],color=SERIES[0],linewidth=2,marker='o',markersize=5,label='delivered power at the bus')
    ax.axhline(d['power_needed_W'],color=INK2,linestyle='--',linewidth=1,label=f"needed at 400 V ({d['power_needed_W']:.1f} W)")
    ax.set_yscale('log');ax.set_ylabel('W (log)',color=INK2,fontsize=9)
    ax.legend(loc='lower right',fontsize=8,frameon=False)
    for label,v in (firmware_duty or {}).items():
        ax.annotate(label,(v,1),xycoords=('data','axes fraction'),xytext=(3,-12),textcoords='offset points',fontsize=8,color=INK2)
    ax.set_title('Fixed duty on both legs, bus starting at 400 V, BUSREG load (open loop)',fontsize=10,color=INK2,loc='left')
    ax=axes[1]
    ax.plot(x,[q['bus_end_V'] for q in r],color=SERIES[1],linewidth=1.5,marker='o',markersize=4,label=f"bus voltage at {1e3*d['window_s'][1]:g} ms")
    ax.axhline(400,color=INK2,linestyle='--',linewidth=1)
    ax.set_ylabel('V',color=INK2,fontsize=9);ax.set_xlabel('duty',color=INK2,fontsize=9);ax.legend(loc='upper left',fontsize=8,frameon=False)
    fig.tight_layout();fig.savefig(out/'duty_sweep.png',dpi=110,facecolor=SURFACE);plt.close(fig)
    print('Plot:',out/'duty_sweep.png')

def main():
    a=argparse.ArgumentParser();a.add_argument('--out',type=Path,required=True)
    a.add_argument('--duties',type=float,nargs='+',default=[.05,.1,.2,.3,.4,.5,.6,.7,.8,.9])
    a.add_argument('--plot-only',action='store_true',help='redraw from an existing duty_sweep.json')
    a.add_argument('--mark',nargs=2,action='append',metavar=('LABEL','DUTY'),help='dotted marker, e.g. --mark "leg 1 at 100 ms" 0.22')
    args=a.parse_args();args.out.mkdir(parents=True,exist_ok=True)
    marks={k:float(v) for k,v in (args.mark or [])}
    if args.plot_only:
        plot(args.out,marks);return
    with Pool(min(4,len(args.duties))) as pool:res=pool.map(one,args.duties)
    need=400**2/LOAD_OHM+400**2/BLEED_OHM
    print(f'Power needed at 400 V: {need:.1f} W. Window {WINDOW[0]*1e3:g}-{WINDOW[1]*1e3:g} ms.')
    print(' duty  bus start->end (V)  slope (V/s)  source P (W)  delivered P (W)  I leg1/leg2 mean (A)')
    for r in res:
        print(f" {r['duty']:4.2f}  {r['bus_start_V']:7.1f} -> {r['bus_end_V']:6.1f}  {r['bus_slope_V_per_s']:10.0f}  {r['source_power_W']:12.2f}  {r['delivered_power_W']:15.2f}  {r['i_leg1_mean_A']:7.3f} / {r['i_leg2_mean_A']:.3f}")
    (args.out/'duty_sweep.json').write_text(json.dumps({'power_needed_W':need,'window_s':WINDOW,'results':res},indent=2)+'\n')
    plot(args.out,marks)

if __name__=='__main__':main()
