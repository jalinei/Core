#!/usr/bin/env python3
## SPDX-License-Identifier: LGPL-2.1
## @file import_model.py
## @brief Create/check the versioned parameter manifest of the accepted switched model.
"""Parameter values are JSON (also valid YAML 1.2); no silent schematic regeneration."""
from pathlib import Path
import argparse,hashlib,json
HERE=Path(__file__).resolve().parent
PHYSICAL={'RESISTOR','CAPACITOR','INDUCTOR','TRANSFORMER','COUPLED_INDUCTOR','SWITCH','DIODE','VOLTAGE_SOURCE'}
def snapshot(path):
    """Extract physical parameters and their current provenance/confidence."""
    doc=json.loads(path.read_text());parts=doc['circuits'][doc['active_circuit']]['components'];result={}
    for p in parts:
        if p['type'] not in PHYSICAL:continue
        name=p['name'];q=p['parameters']
        confidence='provided schematic/netlist value'
        source='provided current-fed half-bridge model'
        if name in {'FL1','FL2','AC_L1','AC_L3','C120','C7_damp','R3_damp','C113','C10_Y','C5_Y','C122'}:source='reference/output_filter.png'
        if name in {'FL1','FL2'}:confidence='5 mH label interpreted per winding; coupling 0.999 ASSUMED; DCR/saturation TBD'
        if name.startswith('INV_') or name.startswith('Relay_'):confidence='placeholder device/contact behavior; physical part identification TBD'
        if name in {'Vsource','Vgrid','Rsource','Rbus_bleed'}:source='declared test fixture in accepted schematic'
        result[name]={'type':p['type'],'parameters':q,'source':source,'confidence':confidence}
    return {'model_sha256':hashlib.sha256(path.read_bytes()).hexdigest(),'components':result,
            'capabilities':{'switched':True,'averaged':False,'pv_curve':False,'physical_device_calibration':False}}
def main():
    """Explicit import/update or verify current accepted snapshot."""
    a=argparse.ArgumentParser();a.add_argument('--write',action='store_true');args=a.parse_args()
    data=snapshot(HERE/'full_model.pulsim');path=HERE/'params.yaml'
    if args.write:path.write_text(json.dumps(data,indent=2,ensure_ascii=False)+'\n')
    else:
        if json.loads(path.read_text())!=data:raise SystemExit('Model/parameter manifest mismatch; review the import and explicitly update params.yaml')
        print('Accepted model and versioned parameter manifest agree:',data['model_sha256'])
if __name__=='__main__':main()
