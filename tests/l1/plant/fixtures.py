#!/usr/bin/env python3
## SPDX-License-Identifier: LGPL-2.1
## @file fixtures.py
## @brief Explicit switched DCDC test loads and capacitor initial conditions.
"""Scenario fixtures never alter the production controller or accepted snapshot."""
from pulsimgui.models.component import Component, ComponentType as T
from pulsimgui.models.wire import Wire, WireSegment, WireConnection


def apply(project, fixture):
    """Wire a separate bus load and initialize the declared capacitor energies."""
    c=project.get_active_circuit();parts={a.name:a for a in c.components.values()}
    voltage=float(fixture['initial_bus_V'])
    if voltage<0 or fixture['load_ohm']<=0:raise ValueError('Invalid DCDC fixture')
    bus=parts['Cbus'];bus.parameters['initial_voltage']=voltage
    bus.parameters['dcdc_fixture']=dict(fixture)
    for prefix in ['DC1','DC2']:
        ratio=parts[prefix+'_T1'].parameters['turns_ratio']
        vin=parts['Vsource'].parameters['waveform']['value']
        parts[prefix+'_C1'].parameters['initial_voltage']=max(0.,voltage*ratio-vin)
        for suffix in ['_C4','_C6']:
            parts[prefix+suffix].parameters['initial_voltage']=voltage/2
    def wire(a,pa,b,pb):
        x,y=a.get_pin_position(pa);u,v=b.get_pin_position(pb)
        c.add_wire(Wire(segments=[WireSegment(x,y,u,y),WireSegment(u,y,u,v)],
                        start_connection=WireConnection(a.id,pa),end_connection=WireConnection(b.id,pb)))
    bleed=parts['Rbus_bleed']
    load=Component(type=T.RESISTOR,name='DCDC_test_load',x=1340,y=560,rotation=90,
                   parameters={'resistance':fixture['load_ohm']})
    c.add_component(load);wire(bleed,0,load,0);wire(bleed,1,load,1)
    if 'load_step_s' in fixture:
        extra=Component(type=T.RESISTOR,name='DCDC_step_load',x=1260,y=560,rotation=90,
                        parameters={'resistance':fixture['step_load_ohm']})
        switch=Component(type=T.SWITCH,name='DCDC_load_step',x=1260,y=680,rotation=90,
                         parameters={'ron':.001,'roff':1e12,'fixture_step_time_s':fixture['load_step_s']})
        c.add_component(extra);c.add_component(switch)
        wire(bleed,0,extra,0);wire(extra,1,switch,0)
        x,y=switch.get_pin_position(1);u,v=bleed.get_pin_position(1)
        c.add_wire(Wire(segments=[WireSegment(x,y,x,740),WireSegment(x,740,u,740),WireSegment(u,740,u,v)],
                        start_connection=WireConnection(switch.id,1),end_connection=WireConnection(bleed.id,1)))
    return project
