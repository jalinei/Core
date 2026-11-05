#!/usr/bin/env python3
## SPDX-License-Identifier: LGPL-2.1
## @file pv.py
## @brief Synthetic single-diode source and wired PV/MPPT plant variant.
"""PV physics and test loads only; application logic remains in controlStep."""
import json
from pathlib import Path
import numpy as np
from scipy.optimize import brentq, minimize_scalar
from pulsimgui.models.component import Component, ComponentType as T
from pulsimgui.models.wire import Wire, WireSegment, WireConnection

PROFILE = Path(__file__).with_name('pv-profile.json')


def current(v, params):
    """Independent implicit module equation, positive current delivered to load."""
    il, i0, a, rs, rsh = (params[n] for n in ['IL_A','I0_A','a_V','Rs_ohm','Rsh_ohm'])
    def f(i):
        return il-i0*np.expm1(np.clip((v+i*rs)/a,-700,700))-(v+i*rs)/rsh-i
    lo, hi = -max(10., abs(v)/rs+il), max(10.,il)
    return brentq(f,lo,hi)


def current_lambertw(v, params, light=1.0):
    """@brief Explicit single-diode current at terminal voltage v, through Lambert W.
    @param v Terminal voltage (V), scalar or array; the panel's Rs and Rsh are inside the equation.
    @param params Profile parameters (IL_A, I0_A, a_V, Rs_ohm, Rsh_ohm).
    @param light Irradiance scale applied to IL (1 = profile value).
    @return Current delivered to the load (A).
    @details I = (Rsh (IL + I0) - V) / (Rs + Rsh) - (a / Rs) W(x), with
    x = Rs I0 Rsh / (a (Rs + Rsh)) exp(Rsh (Rs IL + Rs I0 + V) / (a (Rs + Rsh))).
    W(x) is evaluated as wrightomega(ln x) so a large V cannot overflow exp()."""
    from scipy.special import wrightomega
    il, i0, a, rs, rsh = (params[n] for n in ['IL_A','I0_A','a_V','Rs_ohm','Rsh_ohm'])
    il = il*light
    s_ = rs+rsh
    log_x = np.log(rs*i0*rsh/(a*s_)) + rsh*(rs*il+rs*i0+np.asarray(v, dtype=float))/(a*s_)
    return (rsh*(il+i0)-np.asarray(v, dtype=float))/s_ - (a/rs)*np.real(wrightomega(log_x))


def reference(params):
    """Calculate available MPP independently from the controller trajectory."""
    voc=brentq(lambda v:current(v,params),0.,200.)
    m=minimize_scalar(lambda v:-v*current(v,params),bounds=(0.,voc),method='bounded')
    return dict(Voc_V=voc,Vmp_V=float(m.x),Imp_A=current(m.x,params),Pmp_W=float(-m.fun))


def parameters(circuit):
    """Read the actual editable GUI elements rather than assume profile values."""
    parts={a.name:a for a in circuit.components.values()}
    if 'PV_light' not in parts:return None
    if parts['PV_light'].parameters.get('pv_lambertw'):return dict(parts['PV_light'].parameters['pv_lambertw'])
    d=parts['PV_junction'].parameters
    return dict(IL_A=parts['PV_light'].parameters['waveform']['value'],I0_A=d['is_'],
                a_V=d['n']*d['thermal_voltage'],Rs_ohm=parts['Rsource'].parameters['resistance'],
                Rsh_ohm=parts['PV_shunt'].parameters['resistance'])


def apply(project, model='diode'):
    """Replace the ideal source, retain explicit wires and add a bus load.
    @param model 'diode': light current source, Shockley diode and shunt in the circuit, solved by
    Newton at every step (reference). 'lambertw': one current source whose value the runner sets at
    every step from the terminal voltage, with the explicit Lambert W equation (Rs and Rsh included,
    so the series resistor is reduced to 1 µΩ); faster, lags the terminal voltage by one step."""
    if model not in ('diode','lambertw'):raise ValueError('PV model must be diode or lambertw')
    cfg=json.loads(PROFILE.read_text());q=cfg['parameters'];c=project.get_active_circuit()
    parts={a.name:a for a in c.components.values()};old=parts['Vsource']
    # Same electrical pins/UUID preserve every existing supply wire.
    light=Component(id=old.id,type=T.CURRENT_SOURCE,name='PV_light',x=old.x,y=old.y,
                    parameters={'waveform':{'type':'dc','value':q['IL_A']},'pv_profile':cfg['name']})
    c.components[old.id]=light
    parts['Rsource'].parameters['resistance']=q['Rs_ohm']
    if model=='lambertw':
        light.parameters['waveform']['value']=0.0
        light.parameters['pv_lambertw']=dict(q)
        parts['Rsource'].parameters['resistance']=1e-6
    else:
        _junction(c,old,light,q,cfg)
    _bus(c,parts,q,cfg)
    return project


def _junction(c,old,light,q,cfg):
    """Shockley diode and shunt in parallel with the light current (diode model only)."""
    diode=Component(type=T.DIODE,name='PV_junction',x=old.x-240,y=old.y,rotation=90,
                    parameters={'pv_shockley':True,'is_':q['I0_A'],'n':q['a_V']/cfg['thermal_voltage_V'],
                                'thermal_voltage':cfg['thermal_voltage_V'],'g_min':1e-12,'enable_thermal_port':False})
    shunt=Component(type=T.RESISTOR,name='PV_shunt',x=old.x-460,y=old.y,rotation=90,
                    parameters={'resistance':q['Rsh_ohm']})
    c.add_component(diode);c.add_component(shunt)
    def wire(a,pa,b,pb):
        x,y=a.get_pin_position(pa);u,v=b.get_pin_position(pb)
        c.add_wire(Wire(segments=[WireSegment(x,y,u,y),WireSegment(u,y,u,v)],
                        start_connection=WireConnection(a.id,pa),end_connection=WireConnection(b.id,pb)))
    for part in [diode,shunt]:
        for pin in [0,1]:
            x,y=light.get_pin_position(pin);u,v=part.get_pin_position(pin)
            # Shared rails clear the full 80 px resistor symbol, with short
            # source stubs outside the existing series-resistor power path.
            turn=x-80;rail=old.y+(-80 if pin==0 else 80)
            points=[(x,y),(turn,y),(turn,rail),(u,rail),(u,v)]
            c.add_wire(Wire(segments=[WireSegment(*a,*b) for a,b in zip(points,points[1:])],
                            start_connection=WireConnection(light.id,pin),end_connection=WireConnection(part.id,pin)))


def _bus(c,parts,q,cfg):
    """Bus test load and initial voltages, shared by both PV models."""
    def wire(a,pa,b,pb):
        x,y=a.get_pin_position(pa);u,v=b.get_pin_position(pb)
        c.add_wire(Wire(segments=[WireSegment(x,y,u,y),WireSegment(u,y,u,v)],
                        start_connection=WireConnection(a.id,pa),end_connection=WireConnection(b.id,pb)))
    # Separate, labeled test load: the existing board bleed remains unchanged.
    bleed=parts['Rbus_bleed']
    load=Component(type=T.RESISTOR,name='PV_bus_test_load',x=bleed.x-160,y=bleed.y,
                   rotation=90,parameters={'resistance':cfg['bus_load_ohm']})
    c.add_component(load);wire(bleed,0,load,0);wire(bleed,1,load,1)
    ref=reference(q)
    for prefix in ['DC1','DC2']:
        parts[prefix+'_C7'].parameters['initial_voltage']=ref['Voc_V']
        parts[prefix+'_C2'].parameters['initial_voltage']=ref['Voc_V']
        for suffix in ['_C4','_C6']:
            parts[prefix+suffix].parameters['initial_voltage']=cfg['initial_bus_V']/2
    parts['Cbus'].parameters['initial_voltage']=cfg['initial_bus_V']
