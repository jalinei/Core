#!/usr/bin/env python3
## SPDX-License-Identifier: LGPL-2.1
## @file test_model_open_loop.py
## @brief L1-MOD-01…08: does the model behave like the converter? Open loop, firmware out of the loop.
## @par Usage
##   .venv/bin/python tests/l1/model_validation/test_model_open_loop.py [--jobs N] [--only L1-MOD-05]
##   Writes a campaign folder tests/out/<date>_<commit>_<time>_model/ (l1_model.log result lines,
##   record.md, report.html with interactive figures, one folder per run with its traces).
## @par Cases (tests/catalogue.json, Model tab)
##   01–03 boost (leg 1, leg 2, both) duty sweep at 10/50/100 % load + a start-up from 0 V;
##   04 inverter unloaded; 05 into a resistor; 06 into the grid; 07 dead time; 08 measurement chain.
## @par Analytic references (the hand calculations the model is checked against)
##   Boost: Vbus = 6·VLow/(1−d), d the low switch's effective on-time fraction (100 ns dead time
##   included, on the simulation's step grid). Inverter: fundamental m·Vbus/√2 minus the first-
##   order dead-time loss, a square wave of 2·td·f·Vbus in phase with the current (fundamental
##   (4/π)·2·td·f·Vbus/√2 rms). Grid: phasor solution of the filter network at 50 Hz, driven by
##   the bridge voltage the gate pattern actually applies.
## @return 1 if a criterion fails.
"""Simulation verdicts check the model against hand calculations; the board comparison comes later."""
from pathlib import Path
from concurrent.futures import ProcessPoolExecutor
import argparse,datetime as dt,html,json,os,subprocess,sys,time,traceback
import numpy as np

HERE=Path(__file__).resolve().parent;L1=HERE.parent;REPO=L1.parents[1]
sys.path.insert(0,str(L1));sys.path.insert(0,str(REPO/'tests/common'))
CALLER=Path.cwd()  # --out is relative to where the script was started
os.chdir(L1)
from plant.open_loop import Case,window,PWM_HZ,DEADTIME,GRID_HZ,GRID_PEAK  # noqa: E402

VIN=40.;RATED_LEG=200.;RATED=400.;BUS=400.;R_AC=230.**2/RATED
DUTIES=sorted({round(x,2) for x in list(np.arange(0.05,0.451,0.05))+list(np.arange(0.30,0.461,0.02))})
LOAD_FRACTIONS=(0.1,0.5,1.0);M_SWEEP=(0.2,0.4,0.6,0.8,1.0);BLEED=200e3
CONFIGS={'L1-MOD-01':([1],RATED_LEG,'leg 1 alone'),'L1-MOD-02':([2],RATED_LEG,'leg 2 alone'),'L1-MOD-03':([1,2],RATED,'both legs')}
BOOST_S=0.15;INV_S=0.06;GRID_S=0.12


def eff_duty(d,dt_s=50e-9):
    T=1/PWM_HZ;n=int(round(T/dt_s))
    return float(np.mean([window((k*dt_s+1e-14)%T,d,T,DEADTIME)[0] for k in range(n)]))


# ---------------------------------------------------------------- case list
def cases():
    out=[]
    for cid,(legs,rated,_) in CONFIGS.items():
        for f in LOAD_FRACTIONS:
            R=BUS**2/(f*rated)
            for d in DUTIES:
                v=6*VIN/(1-eff_duty(d))
                if v>450:continue  # the sweep's ceiling: never above 450 V
                out.append({'case':cid,'name':f'{cid[-2:]}-load{int(f*100)}-d{d:.2f}','kind':'boost','legs':legs,'duty':d,
                            'load_ohm':R,'load_W':f*rated,'initial_bus_V':round(v,1),'dt_s':50e-9,'duration_s':BOOST_S,'store_every':100})
            out.append({'case':cid,'name':f'{cid[-2:]}-load{int(f*100)}-startup','kind':'boost','legs':legs,'duty':0.40,'startup':True,
                        'load_ohm':R,'load_W':f*rated,'initial_bus_V':0.,'dt_s':50e-9,'duration_s':BOOST_S,'store_every':10})
    for m in M_SWEEP:
        out.append({'case':'L1-MOD-04','name':f'04-open-m{m:.1f}','kind':'inverter','output':'open','m':m,'bus_V':BUS,'dt_s':50e-9,'duration_s':INV_S,'store_every':20})
        out.append({'case':'L1-MOD-05','name':f'05-resistor-m{m:.1f}','kind':'inverter','output':'resistor','load_ohm':R_AC,'m':m,'bus_V':BUS,'dt_s':50e-9,'duration_s':INV_S,'store_every':20})
    for td in (0.,100e-9,200e-9):
        out.append({'case':'L1-MOD-07','name':f'07-deadtime-{td*1e9:.0f}ns','kind':'inverter','output':'resistor','load_ohm':R_AC,'m':0.8,'bus_V':BUS,
                    'deadtime_s':td,'dt_s':10e-9,'duration_s':INV_S,'store_every':100})
    for P,Q in ((200,0),(400,0),(-200,0),(0,200),(0,-200)):
        m,ph=grid_command(P,Q)
        out.append({'case':'L1-MOD-06','name':f'06-grid-P{P:+d}-Q{Q:+d}','kind':'inverter','output':'grid','m':m,'phase_deg':ph,'target_P':P,'target_Q':Q,
                    'bus_V':BUS,'deadtime_s':0.,'dt_s':10e-9,'duration_s':GRID_S,'store_every':100})
    m,ph=grid_command(400,0)
    out.append({'case':'L1-MOD-06','name':'06-grid-P+400-deadtime100ns','kind':'inverter','output':'grid','m':m,'phase_deg':ph,'target_P':400,'target_Q':0,
                'bus_V':BUS,'deadtime_s':100e-9,'dt_s':10e-9,'duration_s':GRID_S,'store_every':100,'info_only':True})
    return out


# ---------------------------------------------------------------- filter network (50 Hz phasors)
def network(ron=0.04):
    """@brief Grid current phasor I = a·V1 + b·V2 (peak phasors), V1 bridge, V2 grid. Differential
    model: AC_L1+AC_L3 in series, damping branch at F1, FL1/FL2 differential inductance L1+L2−2M,
    C113 at F2, relays, Y capacitors in series at F3, C122 at F4. ron: bridge switch resistance."""
    w=2*np.pi*GRID_HZ;j=1j
    z1=ron+j*w*(0.25e-3+0.25e-3);y1=1/(4.7+1/(j*w*0.68e-6))
    zl=j*w*(0.005+0.005-2*0.004995);y2=j*w*0.12e-6;zr=0.002;y3=j*w*(4.7e-9/2);y4=j*w*0.12e-6
    def solve(V1,V2):
        # Unknown node voltages F1, F2, F3, F4 (differential). Branches: V1 -z1- F1 -zl- F2 -zr- F3 -zl- F4 = V2.
        Y=np.zeros((3,3),complex);I=np.zeros(3,complex)
        def series(i,k,z):  # branch between unknown nodes i and k (k None: to a known voltage handled by caller)
            Y[i,i]+=1/z
            if k is not None:Y[k,k]+=1/z;Y[i,k]-=1/z;Y[k,i]-=1/z
        series(0,None,z1);I[0]+=V1/z1;Y[0,0]+=y1      # bridge to F1, damping branch at F1
        series(0,1,zl);Y[1,1]+=y2                      # FL1 to F2, C113
        series(1,2,zr);Y[2,2]+=y3                      # relays to F3, Y capacitors in series
        series(2,None,zl);I[2]+=V2/zl                  # FL2 to F4 = grid (C122 sits across the grid source)
        v=np.linalg.solve(Y,I)
        return (v[2]-V2)/zl                            # current into the grid through FL2 (C122 current excluded)
    a=solve(1,0);b=solve(0,1)
    return a,b


def grid_command(P,Q):
    """@brief Modulation index and phase (deg) that should give P, Q at the grid (dead time 0)."""
    a,b=network();V2=GRID_PEAK+0j
    I=np.conj(2*(P+1j*Q)/V2)  # S = V·I*/2 with peak phasors
    V1=(I-b*V2)/a
    return float(abs(V1)/BUS),float(np.degrees(np.angle(V1)))


# ---------------------------------------------------------------- helpers
def phasor(t,y,f=GRID_HZ):
    """@brief Peak phasor of the f component over the whole cycles at the end of (t, y)."""
    n=int(np.floor((t[-1]-t[0])*f+0.01));t0=t[-1]-n/f;w=t>=t0-1e-9;tt,yy=t[w],y[w]  # a window one sample short still counts as whole cycles
    wt=np.gradient(tt)
    return 2*np.sum(yy*np.exp(-2j*np.pi*f*tt)*wt)/(tt[-1]-tt[0]+wt[-1])


def thd(t,y,f=GRID_HZ,n=40):
    f1=abs(phasor(t,y,f));h=[abs(phasor(t,y,k*f)) for k in range(2,n+1)]
    return float(np.sqrt(np.sum(np.square(h)))/f1) if f1 else float('nan')


def asymptote(t,v):
    """@brief Settled value of v from three equal windows at the end (Aitken). @return (value, settled, drift V/ms)."""
    w=(t[-1]-t[0])*0.6/3;means=[float(np.mean(v[(t>=t[-1]-(3-i)*w)&(t<t[-1]-(2-i)*w)])) for i in range(3)]
    y1,y2,y3=means;drift=(y3-y2)/(w*1e3)
    if abs(y3-y2)<=0.002*abs(y3):return y3,True,drift
    den=y1+y3-2*y2;r=(y3-y2)/(y2-y1) if y2!=y1 else 9
    if 0<r<0.9 and den!=0:return (y1*y3-y2*y2)/den,True,drift  # geometric convergence: extrapolate
    return y3,False,drift


SIGNALS={'boost':['V(Vout)','V(N2)','I(DC1_L3)','I(DC2_L3)','I(DC1_T1.p)','I(DC1_T1.s)','I(DC2_T1.p)','I(DC2_T1.s)',
                  'Is(probe_DC1_Iin)','Is(probe_DC2_Iin)','V(measurement_DC_bus)','V(measurement_DC1_Vin)'],
         'inverter':['V(INV_A)','V(INV_B)','I(AC_L1)','I(AC_L3)','V(F2_L)','V(F2_N)','V(GRID_L)','V(GRID_N)',
                     'Is(probe_Grid_current)','V(measurement_Grid_voltage)','Is(MOD_bus_source)']}


def run_one(spec,folder):
    """@brief Run one case (in a worker process), save its traces, return its metrics."""
    folder=Path(folder);folder.mkdir(parents=True,exist_ok=True)
    t0=time.monotonic();c=Case(spec);t,s=c.run()
    keep={k:s[:,c.ix[k]] for k in SIGNALS[spec['kind']] if k in c.ix}
    # last 2 ms at full resolution (continuing from the final state, same absolute time base)
    det={}
    if spec['kind']=='boost':
        c2=Case({**spec,'duration_s':0.002,'store_every':1})
        x=s[-1].copy();import pulsim as p
        b=c2.model.builder;mask=p.SwitchStateMask(b.graph.num_switches);sw={n:b.switch_index_of(n) for n in c2.gates(0)}
        def switch(tt):
            for n,v in c2.gates(tt).items():mask.set(sw[n],bool(v))
            return mask
        r=p.simulate(b,t_end=0.002,dt=spec['dt_s'],engine='pwl',initial_state=x,switch_fn=switch,store_every=1,auto_regularize=False,max_event_iterations=32)
        td,sd=np.asarray(r.times)+t[-1],np.asarray(r.states)
        det={'time':td,**{k:sd[:,c2.ix[k]] for k in keep}}
    if spec['kind']=='inverter':
        # one more grid cycle at full resolution: the bus and bridge currents are chopped at the
        # switching frequency, so their averages need every step
        import pulsim as p
        c2=Case({**spec,'duration_s':0.02,'store_every':1});x=s[-1].copy()
        b=c2.model.builder;mask=p.SwitchStateMask(b.graph.num_switches);sw={n:b.switch_index_of(n) for n in c2.gates(0)}
        off=t[-1]
        def switch(tt):
            for n,v in c2.gates(tt+off).items():mask.set(sw[n],bool(v))
            return mask
        r=p.simulate(b,t_end=0.02,dt=spec['dt_s'],engine='pwl',initial_state=x,switch_fn=switch,store_every=1,auto_regularize=False,max_event_iterations=32)
        sd=np.asarray(r.states);pick=lambda n:sd[:,c2.ix[n]] if n in c2.ix else None
        vb=pick('V(Vout)');ib=pick('Is(MOD_bus_source)');vab=pick('V(INV_A)')-pick('V(INV_B)');il=pick('I(AC_L1)')
        td_=np.asarray(r.times)+off
        det={'P_bus_source':float(np.mean(spec['bus_V']*ib)),'P_bridge':float(np.mean(vab*il)),'V1':phasor(td_,vab),'I_bridge':phasor(td_,il)}
        del sd
    m=metrics(spec,c,t,keep,det)
    m['wall_s']=round(time.monotonic()-t0,1);m['changes']=c.changes
    np.savez_compressed(folder/'run.npz',time=t,**{k.replace('(','_').replace(')',''):v for k,v in keep.items()},
                        **({'detail_'+k.replace('(','_').replace(')',''):v for k,v in det.items()} if det else {}))
    (folder/'spec.json').write_text(json.dumps(spec,indent=1)+'\n');(folder/'metrics.json').write_text(json.dumps(m,indent=1,default=float)+'\n')
    return m


def metrics(spec,c,t,k,det):
    m={}
    if spec['kind']=='boost':
        m['duty_eff']=c.effective_duty()
        v,settled,drift=asymptote(t,k['V(Vout)']);m.update(bus_V=v,settled=settled,drift_V_per_ms=drift)
        vl=float(np.mean(det['V(N2)']));m['VLow']=vl;m['analytic_V']=6*vl/(1-m['duty_eff'])
        m['error_pct']=100*(v-m['analytic_V'])/m['analytic_V']
        for leg in spec['legs']:
            il=det[f'I(DC{leg}_L3)'];m[f'IL{leg}_mean']=float(np.mean(il));m[f'IL{leg}_pp']=float(np.ptp(il))
            m[f'T{leg}_p_peak']=float(np.max(np.abs(det[f'I(DC{leg}_T1.p)'])));m[f'T{leg}_s_peak']=float(np.max(np.abs(det[f'I(DC{leg}_T1.s)'])))
        if len(spec['legs'])==2:m['share_leg1']=m['IL1_mean']/(m['IL1_mean']+m['IL2_mean']) if m['IL1_mean']+m['IL2_mean'] else float('nan')
        pin=float(np.mean(det['V(N2)']*sum(det[f'I(DC{leg}_L3)'] for leg in (1,2))))
        vb=det['V(Vout)'];rp=1/(1/spec['load_ohm']+1/BLEED)
        pout=float(np.mean(vb**2))/rp+120e-6*float(np.mean(vb))*m['drift_V_per_ms']*1e3
        m.update(P_in=pin,P_out=pout,efficiency=pout/pin if pin>0 else float('nan'))
        # measurement chain: probe vs physical, and the sampling instant
        m['probe_ILow1_vs_inductor']=float(np.mean(det['Is(probe_DC1_Iin)'])/np.mean(det['I(DC1_L3)'])) if 1 in spec['legs'] and abs(np.mean(det['I(DC1_L3)']))>1e-3 else None
        m['probe_VDCBus_vs_node']=float(np.mean(det['V(measurement_DC_bus)'])/np.mean(vb))
        m['probe_VLow_vs_node']=float(np.mean(det['V(measurement_DC1_Vin)'])/np.mean(det['V(N2)']))
        if 1 in spec['legs']:
            T=1/PWM_HZ;ph=((det['time']+1e-12)%T)/T;il=det['Is(probe_DC1_Iin)']
            sim=il[np.abs(ph-0.0)<0.011].mean() if np.any(np.abs(ph)<0.011) else il[ph<0.02].mean()
            board=il[np.abs(ph-0.53)<0.011].mean()  # board: 0.06·PER on the up-count = 3 % after the middle of the low switch's on-time
            m.update(sample_sim_A=float(sim),sample_board_A=float(board),sample_mean_A=float(np.mean(il)))
        if spec.get('startup'):
            il=np.abs(sum(k[f'I(DC{leg}_L3)'] for leg in spec['legs']));vb=k['V(Vout)']
            m.update(inrush_peak_A=float(il.max()),inrush_at_ms=float(1e3*t[int(il.argmax())]),
                     bus_at_ms={str(x):float(vb[min(len(vb)-1,np.searchsorted(t,x/1e3))]) for x in (10,25,50,100,150)})
            fin=vb[-1];hit=np.flatnonzero(vb>=0.9*fin)
            m['t90_ms']=float(1e3*t[hit[0]]) if len(hit) else None
    else:
        w=t>=t[-1]-0.04
        vab=k['V(INV_A)']-k['V(INV_B)']
        if spec['output']=='open':vo=k['V(F2_L)']-k['V(F2_N)']
        else:vo=k['V(GRID_L)']-k['V(GRID_N)']
        V=phasor(t[w],vo[w]);m.update(V_out_rms=float(abs(V)/np.sqrt(2)),V_out_THD=thd(t[w],vo[w]))
        td=spec.get('deadtime_s',DEADTIME);ideal=spec['m']*BUS/np.sqrt(2);loss=(4/np.pi)*2*td*PWM_HZ*BUS/np.sqrt(2)
        m.update(ideal_rms=ideal,deadtime_loss_rms=loss if spec['output']!='open' else 0.)
        m['expected_rms']=ideal-m['deadtime_loss_rms'];m['error_pct']=100*(m['V_out_rms']-m['expected_rms'])/m['expected_rms']
        ig=k['Is(probe_Grid_current)'];I=phasor(t[w],ig[w]);m['I_out_rms']=float(abs(I)/np.sqrt(2));m['I_THD']=thd(t[w],ig[w]) if abs(I)>1e-3 else None
        if spec['output']=='open':
            ww=t>=0.02;x=vo[ww]-np.mean(vo[ww]);fs=1/np.median(np.diff(t[ww]));F=np.abs(np.fft.rfft(x*np.hanning(len(x))));fr=np.fft.rfftfreq(len(x),1/fs)
            near_tick=np.abs(fr/1e4-np.round(fr/1e4))*1e4<300  # 10 kHz lines: the duty is updated every 100 µs
            band=(fr>1e3)&(fr<50e3)&~near_tick;m['resonance_peak_Hz']=float(fr[band][np.argmax(F[band])])
            m['resonance_analytic_Hz']=float(1/(2*np.pi*np.sqrt((0.5e-3+10e-6)*(0.12e-6+0.68e-6))))
        m.update(P_bridge=det['P_bridge'],P_bus=abs(det['P_bus_source']))
        if spec['output']=='resistor':
            pl=float(np.mean(vo[w]**2))/spec['load_ohm'];m.update(P_load=pl,losses_W=m['P_bus']-pl,bridge_losses_W=m['P_bus']-m['P_bridge'])
        if spec['output']=='grid':
            vg=k['V(GRID_L)']-k['V(GRID_N)'];V2=phasor(t[w],vg[w]);S=V2*np.conj(I)/2
            m.update(P=float(S.real),Q=float(S.imag),grid_V_rms=float(abs(V2)/np.sqrt(2)))
            # bridge voltage the gate pattern applied (ideal switches, dead time per spec), exact on the step grid
            tt=np.arange(int(round(0.04/spec['dt_s'])))*spec['dt_s']+(spec['duration_s']-0.04)
            T=1/PWM_HZ;ph=(tt+1e-14)%T;d=c.duty_bridge(tt)
            sa=(ph>=(1-d)*T/2)&(ph<(1+d)*T/2);sb=(ph>=d*T/2)&(ph<(2-d)*T/2)
            V1=phasor(tt,BUS*(sa.astype(float)-sb))
            a,b=network(ron=0.);Ie=a*det['V1']+b*V2;Se=V2*np.conj(Ie)/2   # bridge-node voltage: switch drops already in it
            m.update(P_expected=float(Se.real),Q_expected=float(Se.imag),S_error_pct=float(100*abs(S-Se)/max(abs(Se),1e-9)))
            a,b=network();Ig=a*V1+b*V2;Sg=V2*np.conj(Ig)/2                 # ideal gate pattern: shows how stiff the connection is
            m.update(P_gate_pattern=float(Sg.real),Q_gate_pattern=float(Sg.imag),V1_bridge_peak=float(abs(det['V1'])),V1_gate_pattern_peak=float(abs(V1)))
    return m


# ---------------------------------------------------------------- verdicts
def judge(results):
    """@return list of (id, verdict, description, limit, measured)."""
    rows=[]
    def R(*a):rows.append(a)
    for cid,(legs,rated,label) in CONFIGS.items():
        sw=[r for r in results if r['spec']['case']==cid and not r['spec'].get('startup') and 'error' not in r]
        if not any(r['spec']['case']==cid for r in results):continue
        bad=[r for r in sw if not r['m']['settled'] or abs(r['m']['error_pct'])>5]
        worst=max(sw,key=lambda r:abs(r['m']['error_pct'])) if sw else None
        R(f'{cid}.a','FAIL' if (bad or not sw) else 'PASS',f'Boost {label}: steady bus within ±5 % of 6·VLow/(1−d) at every duty and load','±5 %, settled',
          (f'{len(sw)-len(bad)}/{len(sw)} points within; worst {worst["m"]["error_pct"]:+.1f} % at d={worst["spec"]["duty"]:.2f}, {worst["spec"]["load_W"]:.0f} W'
           +(f'; {sum(not r["m"]["settled"] for r in sw)} not settled' if any(not r['m']['settled'] for r in sw) else '')) if sw else 'no run')
        nom=[r for r in sw if abs(r['spec']['load_W']-rated)<1 and abs(r['spec']['duty']-0.40)<1e-6]
        if nom:
            n=nom[0]['m'];R(f'{cid}.b','INFO',f'Boost {label}: input current mean and ripple at d=0.40, full load','',
              '; '.join(f'leg {l}: {n[f"IL{l}_mean"]:.2f} A mean, {n[f"IL{l}_pp"]:.2f} A pp' for l in legs))
            R(f'{cid}.c','INFO',f'Boost {label}: transformer peak currents at d=0.40, full load','',
              '; '.join(f'leg {l}: primary {n[f"T{l}_p_peak"]:.1f} A, secondary {n[f"T{l}_s_peak"]:.2f} A' for l in legs)
              +(f'; leg 1 carries {100*n["share_leg1"]:.0f} % of the input current' if 'share_leg1' in n else ''))
        eff=[r for r in sw if r['m']['P_in']>1]
        if eff:
            R(f'{cid}.d','INFO',f'Boost {label}: efficiency (bus power / input power)','',
              ', '.join(f'{r["spec"]["load_W"]:.0f} W d={r["spec"]["duty"]:.2f}: {100*r["m"]["efficiency"]:.1f} %' for r in sorted(eff,key=lambda r:(r['spec']['load_W'],r['spec']['duty'])) if abs(r['spec']['duty']-0.40)<1e-6))
        st=[r for r in results if r['spec']['case']==cid and r['spec'].get('startup') and 'error' not in r]
        if st:
            R(f'{cid}.e','INFO',f'Boost {label}: start-up from 0 V at duty 0.40','',
              '; '.join(f'{r["spec"]["load_W"]:.0f} W: inrush {r["m"]["inrush_peak_A"]:.1f} A at {r["m"]["inrush_at_ms"]:.2f} ms, bus {r["m"]["bus_at_ms"]["150"]:.0f} V at 150 ms' for r in st))
    op=sorted([r for r in results if r['spec']['case']=='L1-MOD-04' and 'error' not in r],key=lambda r:r['spec']['m'])
    if op:
        R('L1-MOD-04.a','INFO','Inverter unloaded: output voltage vs modulation index','',', '.join(f'm={r["spec"]["m"]:.1f}: {r["m"]["V_out_rms"]:.1f} V ({r["m"]["error_pct"]:+.1f} % vs m·Vbus/√2)' for r in op))
        R('L1-MOD-04.b','INFO','Inverter unloaded: filter resonance (output spectrum peak 1–50 kHz) vs analytic','',
          f'peak {op[-1]["m"]["resonance_peak_Hz"]/1e3:.2f} kHz; analytic 1/(2π√(LC)) {op[-1]["m"]["resonance_analytic_Hz"]/1e3:.2f} kHz')
    rs=sorted([r for r in results if r['spec']['case']=='L1-MOD-05' and 'error' not in r],key=lambda r:r['spec']['m'])
    if rs:
        bad=[r for r in rs if abs(r['m']['error_pct'])>5];worst=max(rs,key=lambda r:abs(r['m']['error_pct']))
        R('L1-MOD-05.a','FAIL' if bad else 'PASS','Inverter into a resistor: output fundamental within ±5 % of m·Vbus/√2 minus the first-order dead-time loss','±5 %',
          f'{len(rs)-len(bad)}/{len(rs)} within; worst {worst["m"]["error_pct"]:+.1f} % at m={worst["spec"]["m"]:.1f}')
        R('L1-MOD-05.b','INFO','Inverter into a resistor: output voltage THD','',', '.join(f'm={r["spec"]["m"]:.1f}: {100*r["m"]["V_out_THD"]:.2f} %' for r in rs))
        R('L1-MOD-05.c','INFO','Inverter into a resistor: output current','',', '.join(f'm={r["spec"]["m"]:.1f}: {r["m"]["I_out_rms"]:.2f} A' for r in rs))
        R('L1-MOD-05.d','INFO','Inverter into a resistor: losses (bus power − load power; of which the bridge)','',', '.join(f'm={r["spec"]["m"]:.1f}: {r["m"]["losses_W"]:.1f} W of {r["m"]["P_load"]:.0f} W (bridge {r["m"]["bridge_losses_W"]:.1f} W)' for r in rs if r['m'].get('losses_W') is not None))
    gr=[r for r in results if r['spec']['case']=='L1-MOD-06' and 'error' not in r]
    gj=[r for r in gr if not r['spec'].get('info_only')]
    if gj:
        bad=[r for r in gj if r['m']['S_error_pct']>10];worst=max(gj,key=lambda r:r['m']['S_error_pct'])
        R('L1-MOD-06.a','FAIL' if bad else 'PASS','Inverter into the grid: P and Q within ±10 % (of |S|) of the 50 Hz phasor solution of the filter, driven by the simulated bridge voltage','±10 % of |S|',
          f'{len(gj)-len(bad)}/{len(gj)} within; worst {worst["m"]["S_error_pct"]:.1f} % ({worst["spec"]["name"]})')
        R('L1-MOD-06.d','INFO','Inverter into the grid: P, Q predicted from the ideal gate pattern instead (sensitivity: the filter is about 0.17 Ω at 50 Hz)','',
          '; '.join(f'{r["spec"]["name"][8:]}: measured {r["m"]["P"]:.0f} W {r["m"]["Q"]:+.0f} var, ideal gate pattern {r["m"]["P_gate_pattern"]:.0f} W {r["m"]["Q_gate_pattern"]:+.0f} var, bridge {r["m"]["V1_bridge_peak"]:.2f} V vs {r["m"]["V1_gate_pattern_peak"]:.2f} V peak' for r in gj))
        R('L1-MOD-06.b','INFO','Inverter into the grid: grid current THD','',', '.join(f'{r["spec"]["name"][8:]}: {100*r["m"]["I_THD"]:.1f} %' for r in gj if r['m'].get('I_THD')))
    for r in gr:
        if r['spec'].get('info_only'):
            R('L1-MOD-06.c','INFO','Inverter into the grid with 100 ns dead time, same command as P = 400 W','',
              f'P {r["m"]["P"]:.0f} W, Q {r["m"]["Q"]:.0f} var (command aimed at 400 W, 0 var with no dead time); current THD {100*(r["m"]["I_THD"] or 0):.0f} %')
    dtr=sorted([r for r in results if r['spec']['case']=='L1-MOD-07' and 'error' not in r],key=lambda r:r['spec']['deadtime_s'])
    if len(dtr)==3:
        v0=dtr[0]['m']['V_out_rms']
        R('L1-MOD-07.a','INFO','Dead time: output fundamental loss vs the analytic (4/π)·2·td·f·Vbus/√2','',
          ', '.join(f'{r["spec"]["deadtime_s"]*1e9:.0f} ns: −{v0-r["m"]["V_out_rms"]:.1f} V (analytic −{r["m"]["deadtime_loss_rms"]:.1f} V), THD {100*r["m"]["V_out_THD"]:.1f} %' for r in dtr))
    # measurement chain
    b=[r for r in results if r['spec']['case']=='L1-MOD-03' and abs(r['spec']['duty']-0.40)<1e-6 and abs(r['spec']['load_W']-200)<1 and not r['spec'].get('startup')]
    inv=[r for r in results if r['spec']['case']=='L1-MOD-05' and abs(r['spec']['m']-0.8)<1e-6]
    if b and inv:
        mb,mi=b[0]['m'],inv[0]['m'];ratios={'ILow1 / inductor current':mb['probe_ILow1_vs_inductor'],'VDCBus / bus node':mb['probe_VDCBus_vs_node'],'VLow / input node':mb['probe_VLow_vs_node']}
        ok=all(x is not None and abs(x-1)<=0.01 for x in ratios.values())
        R('L1-MOD-08.a','PASS' if ok else 'FAIL','Measurement chain: each boost measurement equals the physical quantity in scale and sign','ratio 1 ± 1 %',
          ', '.join(f'{k} {v:.4f}' for k,v in ratios.items()))
        d=mb['sample_sim_A']-mb['sample_board_A'];rel=abs(d)/max(abs(mb['sample_mean_A']),1e-9)
        R('L1-MOD-08.b','PASS' if rel<=0.02 else 'FAIL','Measurement chain: ILow1 sampled at the simulation instant equals the board\'s ADC instant','difference ≤ 2 % of the mean',
          f'simulation instant {mb["sample_sim_A"]:.3f} A, board instant {mb["sample_board_A"]:.3f} A, mean {mb["sample_mean_A"]:.3f} A ({100*rel:.1f} %)')
        R('L1-MOD-08.c','INFO','Measurement chain: inverter output measurements at m=0.8 into the resistor','',f'VAC {mi["V_out_rms"]:.1f} V rms, IAC {mi["I_out_rms"]:.3f} A rms (load {R_AC:.1f} Ω)')
    for r in results:
        if 'error' in r:R(f'{r["spec"]["case"]}.run','FAIL',f'run {r["spec"]["name"]} failed','completes',r['error'][:200])
    return rows


# ---------------------------------------------------------------- figures and page
def page(out,results,rows,cat):
    from interactive_plot import Figure,assets
    from review_report import page_head,verdict_badge
    from make_record import findings_register,finding_for
    e=html.escape;findings=findings_register()
    rws=[dict(id=a,verdict=b,desc=c,limit=d,measured=x) for a,b,c,d,x in rows]
    for r in rws:r['finding']=finding_for(r['id'],findings) if r['verdict']=='FAIL' else None
    cnt={v:sum(r['verdict']==v for r in rws) for v in ('PASS','FAIL','INFO','BLOCKED')}
    p=[page_head('Model validation'),f'<h1>Model validation: open loop (L1-MOD)</h1><div class="sub">{e(out.name)} · firmware out of the loop · accepted model, variants listed per test</div>',
       f'<div class="banner{" bad" if cnt["FAIL"] else ""}"><div class="counts"><span>{cnt["PASS"]} PASS</span><span>{cnt["FAIL"]} FAIL</span><span>{cnt["INFO"]} INFO</span></div>'
       '<div class="sub">Simulation verdicts check the model against hand calculations (stated per test). The board comparison (same sequence on the hardware) is defined later.</div></div>']
    by=lambda cid:[r for r in results if r['spec']['case']==cid and 'error' not in r]
    for t in [x for x in cat['tests'] if x['tab']=='model']:
        cid=t['id'];mine=[r for r in rws if r['id'].startswith(cid)]
        p.append(f'<h2 id="{cid}">{e(cid)} · {e(t["title"])}</h2>')
        p.append('<h3>1 · Test conditions and objectives</h3><table class="kv">'
                 f'<tr><td>Conditions</td><td>{e(t.get("conditions",""))}</td></tr><tr><td>Criterion</td><td>{e(t.get("criterion",""))}</td></tr>'
                 f'<tr><td>Runs</td><td>{len(by(cid)) or "uses the runs of L1-MOD-03 and L1-MOD-05"}; step {e(", ".join(sorted({f"{r["spec"]["dt_s"]*1e9:g} ns" for r in by(cid)})) or "—")}; '
                 f'{e(", ".join(sorted({f"{r["spec"]["duration_s"]*1e3:g} ms" for r in by(cid)})) or "")} each</td></tr></table>')
        ch=sorted({c for r in by(cid) for c in r['m'].get('changes',[])})
        import re as _re
        kinds=sorted({_re.sub(r'\d+(\.\d+)?','N',c) for c in ch})  # the same change with different values counts once
        p.append('<h3>2 · Model snapshot</h3><p class="sub">Accepted model <code>tests/l1/plant/full_model.pulsim</code>, firmware block not attached; gate pattern imposed by '
                 '<code>tests/l1/plant/open_loop.py</code>. Changes from the accepted model (N: the value of each run):</p><ul>'+''.join(f'<li>{e(k)}</li>' for k in kinds)+'</ul>'
                 +(f'<details><summary>Every run\'s changes ({len(ch)})</summary><ul>'+''.join(f'<li>{e(c)}</li>' for c in ch)+'</ul></details>' if len(ch)>len(kinds) else '') if ch else
                 '<h3>2 · Model snapshot</h3><p class="sub">Same runs as the tests it reads.</p>')
        p.append('<h3>3 · Results</h3><table><tr><th>Criterion</th><th>Verdict</th><th>What</th><th>Limit</th><th>Measured</th></tr>'+''.join(
            f'<tr><td>{e(r["id"])}</td><td>{verdict_badge(r)}</td><td>{e(r["desc"])}</td><td>{e(r["limit"])}</td><td>{e(r["measured"])}</td></tr>' for r in mine)+'</table>')
        p.append('<h3>4 · Figures</h3>')
        for title,html_,cap in figures(cid,by(cid),out,Figure):
            p.append(f'<figure><div><b>{e(title)}</b></div>{html_}<figcaption>{e(cap)}</figcaption></figure>')
    p.append(assets()+'</main></body></html>')
    (out/'report.html').write_text('\n'.join(p),encoding='utf-8')


def figures(cid,rs,out,Figure):
    S=['#2a78d6','#eb6834','#1baf7a','#eda100'];GREY='#52514e'
    figs=[]
    def load_npz(r):return np.load(out/'runs'/r['spec']['name']/'run.npz')
    if cid in CONFIGS:
        sw=[r for r in rs if not r['spec'].get('startup')]
        f=Figure('','duty (commanded)');c=f.chart('V',300)
        for i,fr in enumerate(LOAD_FRACTIONS):
            pts=sorted([r for r in sw if abs(r['spec']['load_W']-fr*CONFIGS[cid][1])<1],key=lambda r:r['spec']['duty'])
            if not pts:continue
            d=np.array([r['spec']['duty'] for r in pts])
            f.line(c,f'bus, {fr*CONFIGS[cid][1]:.0f} W',d,[r['m']['bus_V'] for r in pts],S[i],2)
            if i==0:f.line(c,'analytic 6·VLow/(1−d_eff)',d,[r['m']['analytic_V'] for r in pts],GREY,1,dash=[6,4])
        c2=f.chart('% (model − analytic)',150)
        for i,fr in enumerate(LOAD_FRACTIONS):
            pts=sorted([r for r in sw if abs(r['spec']['load_W']-fr*CONFIGS[cid][1])<1],key=lambda r:r['spec']['duty'])
            if pts:f.line(c2,f'error, {fr*CONFIGS[cid][1]:.0f} W',[r['spec']['duty'] for r in pts],[r['m']['error_pct'] for r in pts],S[i],1.5)
        c2['hlines']=[{'v':5,'dash':[6,4],'color':GREY},{'v':-5,'dash':[6,4],'color':GREY}]
        c3=f.chart('efficiency %',130)
        for i,fr in enumerate(LOAD_FRACTIONS):
            pts=sorted([r for r in sw if abs(r['spec']['load_W']-fr*CONFIGS[cid][1])<1 and r['m']['P_in']>1],key=lambda r:r['spec']['duty'])
            if pts:f.line(c3,f'η, {fr*CONFIGS[cid][1]:.0f} W',[r['spec']['duty'] for r in pts],[100*r['m']['efficiency'] for r in pts],S[i],1.5)
        figs.append(('Gain curve: settled bus voltage vs duty, against the analytic gain',f.html(f'{cid}-gain','gain curve'),
                     'Thick lines: model, one per load. Dashed grey: analytic gain at the lightest load (the analytic value uses each point\'s own VLow and effective duty; the error panel compares each point with its own). Dashed lines in the error panel: ±5 % limits.'))
        nom=[r for r in sw if abs(r['spec']['duty']-0.40)<1e-6 and abs(r['spec']['load_W']-CONFIGS[cid][1])<1]
        if nom:
            z=load_npz(nom[0]);f=Figure('µs');tt=(z['detail_time']-z['detail_time'][0])*1e6;w=tt<=50
            c=f.chart('A',220)
            for i,leg in enumerate(nom[0]['spec']['legs']):f.line(c,f'input inductor current, leg {leg}',tt[w],z[f'detail_I_DC{leg}_L3'][w],S[i],1.5)
            c2=f.chart('A',180)
            for i,leg in enumerate(nom[0]['spec']['legs']):
                f.line(c2,f'transformer primary, leg {leg}',tt[w],z[f'detail_I_DC{leg}_T1.p'][w],S[i],1.2)
            figs.append(('Currents over 10 switching periods at d=0.40, full load (settled)',f.html(f'{cid}-ripple','currents'),'Full-resolution traces (50 ns) taken after the run settled.'))
        st=sorted([r for r in rs if r['spec'].get('startup')],key=lambda r:r['spec']['load_W'])
        if st:
            f=Figure('ms');c=f.chart('V',220);c2=f.chart('A',180)
            for i,r in enumerate(st):
                z=load_npz(r);k=slice(None,None,10);tt=z['time'][k]*1e3  # every 5 µs
                f.line(c,f'bus, {r["spec"]["load_W"]:.0f} W',tt,z['V_Vout'][k],S[i],2)
                f.line(c2,f'input current, {r["spec"]["load_W"]:.0f} W',tt,sum(z[f'I_DC{l}_L3'] for l in r['spec']['legs'])[k],S[i],1.2)
            figs.append(('Start-up from a discharged bus at duty 0.40',f.html(f'{cid}-startup','startup'),'Bus and total input current from t = 0, one line per load, shown every 5 µs: the inrush peak in the results table comes from the 500 ns record.'))
    elif cid in ('L1-MOD-04','L1-MOD-05','L1-MOD-07'):
        f=Figure('ms');c=f.chart('V',260);c2=f.chart('A',160)
        for i,r in enumerate(sorted(rs,key=lambda r:(r['spec']['m'],r['spec'].get('deadtime_s',0)))):
            z=load_npz(r);tt=z['time']*1e3;w=tt>=tt[-1]-40
            vo=(z['V_F2_L']-z['V_F2_N']) if r['spec']['output']=='open' else (z['V_GRID_L']-z['V_GRID_N'])
            lab=f'm={r["spec"]["m"]:.1f}' if cid!='L1-MOD-07' else f'{r["spec"]["deadtime_s"]*1e9:.0f} ns'
            f.line(c,f'output voltage, {lab}',tt[w],vo[w],S[i%4],1.5)
            f.line(c2,f'bridge current, {lab}',tt[w],z['I_AC_L1'][w],S[i%4],1.2)
        figs.append(('Output voltage and bridge current, last 40 ms',f.html(f'{cid}-wave','waveforms'),'One line per run; zoom in to see the switching ripple and the dead-time distortion near the current zero crossings.'))
        if cid=='L1-MOD-05':
            f=Figure('','modulation index m');c=f.chart('V rms',260);pts=sorted(rs,key=lambda r:r['spec']['m'])
            f.line(c,'output fundamental (model)',[r['spec']['m'] for r in pts],[r['m']['V_out_rms'] for r in pts],S[0],2)
            f.line(c,'expected: m·Vbus/√2 − dead-time loss',[r['spec']['m'] for r in pts],[r['m']['expected_rms'] for r in pts],GREY,1,dash=[6,4])
            f.line(c,'ideal m·Vbus/√2',[r['spec']['m'] for r in pts],[r['m']['ideal_rms'] for r in pts],GREY,1)
            figs.append(('Output fundamental vs modulation index',f.html('mod05-gain','gain'),'Thick blue: model. Dashed: the criterion\'s expected value. Thin grey: no dead time.'))
    elif cid=='L1-MOD-06':
        f=Figure('ms');c=f.chart('A',240);c2=f.chart('V',160)
        for i,r in enumerate(rs):
            z=load_npz(r);tt=z['time']*1e3;w=tt>=tt[-1]-40
            f.line(c,f'grid current, {r["spec"]["name"][8:]}',tt[w],z['Is_probe_Grid_current'][w],S[i%4] if i<4 else GREY,1.3)
            if i==0:f.line(c2,'grid voltage',tt[w],z['V_GRID_L'][w]-z['V_GRID_N'][w],S[0],1.2)
        figs.append(('Grid current per command, last 40 ms',f.html('mod06-wave','grid currents'),'One line per run (grey: 100 ns dead time). The current probe counts current into the grid as positive.'))
    return figs


def main():
    a=argparse.ArgumentParser();a.add_argument('--jobs',type=int,default=os.cpu_count());a.add_argument('--only');a.add_argument('--out',type=Path)
    a.add_argument('--report-only',action='store_true',help='rebuild verdicts and page from saved runs')
    a.add_argument('--reuse',action='store_true',help='with --only and --out: run the selected tests, reuse the saved runs of the others')
    args=a.parse_args()
    import catalogue
    cat=catalogue.load()
    sha=subprocess.run(['git','rev-parse','--short','HEAD'],cwd=REPO,capture_output=True,text=True).stdout.strip()
    dirty='-dirty' if subprocess.run(['git','status','--porcelain','--untracked-files=no'],cwd=REPO,capture_output=True,text=True).stdout.strip() else ''
    if args.out and not args.out.is_absolute():args.out=CALLER/args.out
    out=args.out or REPO/'tests/out'/f'{dt.date.today()}_{sha}{dirty}_{dt.datetime.now():%H%M%S}_model'
    out.mkdir(parents=True,exist_ok=True)
    sel=lambda c:not args.only or c['case'].startswith(tuple(args.only.split(',')))
    every=cases();todo=[c for c in every if sel(c)]
    results=[]
    if args.reuse:
        for c in every:
            f=out/'runs'/c['name']/'metrics.json'
            if not sel(c):results.append({'spec':c,'m':json.loads(f.read_text())} if f.exists() else {'spec':c,'error':'no saved run'})
    if args.report_only:
        for c in todo:
            f=out/'runs'/c['name']/'metrics.json'
            results.append({'spec':c,'m':json.loads(f.read_text())} if f.exists() else {'spec':c,'error':'no saved run'})
    else:
        cost=lambda c:c['duration_s']/c['dt_s']
        todo.sort(key=lambda c:-cost(c))
        print(f'{len(todo)} runs on {args.jobs} jobs -> {out}',flush=True)
        with ProcessPoolExecutor(max_workers=args.jobs) as pool:
            futs={pool.submit(run_one,c,str(out/'runs'/c['name'])):c for c in todo}
            from concurrent.futures import as_completed
            for i,fu in enumerate(as_completed(futs),1):
                c=futs[fu]
                try:results.append({'spec':c,'m':fu.result()});print(f'[{i}/{len(todo)}] {c["name"]} done',flush=True)
                except Exception:results.append({'spec':c,'error':traceback.format_exc().strip().splitlines()[-1]});print(f'[{i}/{len(todo)}] {c["name"]} FAILED: {results[-1]["error"]}',flush=True)
    rows=judge(results)
    with (out/'l1_model.log').open('w') as log:
        for r in rows:
            line='@@\t'+'\t'.join(str(x) for x in r);print(line);log.write(line+'\n')
    subprocess.run([sys.executable,str(REPO/'tests/common/make_record.py'),'--out',str(out),'--levels','L1',str(out/'l1_model.log')])
    page(out,results,rows,cat)
    subprocess.run([sys.executable,str(REPO/'tests/common/control_center.py'),'--out',str(out.parent)],capture_output=True)
    print('Review:',out/'report.html')
    return 1 if any(r[1]=='FAIL' for r in rows) else 0


if __name__=='__main__':sys.exit(main())
