#!/usr/bin/env python3
## SPDX-License-Identifier: LGPL-2.1
## @file snapshot.py
## @brief Save the exact model a scenario ran on, a picture of it and what its fixture changed.
## @par Usage
##   QT_QPA_PLATFORM=offscreen .venv/bin/python tests/l1/plant/snapshot.py tests/out/<campaign>/<scenario>
## @par Writes, in the scenario folder
##   - model.pulsim: the derived project (accepted model + fixture + firmware block), opens in PulsimGUI;
##   - model_power.png: the power stage; model_full.png: everything, including the firmware block;
##   - model_changes.json: every parameter the fixture changed and every component it added,
##     compared with the accepted plant/full_model.pulsim.
"""Rebuilds the model from the run's manifest; it never changes the accepted model."""
from pathlib import Path
import json,shutil,sys,tempfile
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from runtime import HERE


def changes(accepted,derived,signal_only):
    """@brief Differences between the accepted and the derived circuit.
    @param signal_only Names of the firmware-block/view components, listed separately.
    @return dict with 'changed' parameters and 'added' components."""
    a={c.name:c for c in accepted.get_active_circuit().components.values()}
    d={c.name:c for c in derived.get_active_circuit().components.values()}
    changed=[];added=[];view=[]
    for name,comp in d.items():
        if name in a:
            for k,v in comp.parameters.items():
                if a[name].parameters.get(k)!=v and not isinstance(v,dict):
                    changed.append({'component':name,'parameter':k,'accepted':a[name].parameters.get(k),'this_run':v})
        elif name in signal_only or not name:
            view.append(name)
        else:
            added.append({'component':name,'type':comp.type.name,'parameters':{k:v for k,v in comp.parameters.items() if not isinstance(v,dict)}})
    return {'changed':changed,'added':added,'firmware_view_components':len(view)}


def render(scene,rect,path,width):
    """@brief Render part of a schematic scene to a PNG on a white background."""
    from PySide6.QtGui import QImage,QPainter,QColor
    from PySide6.QtCore import QRectF
    h=int(width*rect.height()/rect.width())
    img=QImage(width,h,QImage.Format_ARGB32);img.fill(QColor('#ffffff'))
    p=QPainter(img);p.setRenderHint(QPainter.Antialiasing);scene.render(p,QRectF(0,0,width,h),rect);p.end()
    img.save(str(path))


def main(folder):
    folder=Path(folder);meta=json.loads((folder/'manifest.json').read_text())
    scenario={**meta['scenario'],'fixture':meta['fixture']} if meta.get('fixture') else meta['scenario']
    pv=bool(meta.get('pv_model_available'))
    from PySide6.QtWidgets import QApplication
    from PySide6.QtCore import Qt,QRectF
    from pulsimgui.models.project import Project
    from pulsimgui.views.schematic.items import wire_item
    from plant.create_gui import create
    # Wires are drawn with the painter's current brush; when rendering a whole scene that
    # brush leaks from the previous item and fills wire polygons grey. Paint them unfilled.
    original=wire_item.WireItem.paint
    def paint(self,painter,*a,**k):
        painter.setBrush(Qt.BrushStyle.NoBrush);return original(self,painter,*a,**k)
    wire_item.WireItem.paint=paint
    app=QApplication.instance() or QApplication([])
    from pulsimgui.views.main_window import MainWindow
    with tempfile.TemporaryDirectory() as tmp:
        derived_path=create(pv=pv,scenario=scenario,dest=tmp)
        shutil.copy(derived_path,folder/'model.pulsim')
    accepted=Project.load(HERE/'plant/full_model.pulsim');derived=Project.load(folder/'model.pulsim')
    if pv:
        from plant.pv import apply
        base=apply(Project.load(HERE/'plant/full_model.pulsim'))
    else:
        base=None
    signal=set()
    view_types={'C_BLOCK','CONSTANT','PWM_GENERATOR','GOTO_LABEL','FROM_LABEL'}
    for c in derived.get_active_circuit().components.values():
        if c.name not in {x.name for x in accepted.get_active_circuit().components.values()} and c.type.name in view_types:
            signal.add(c.name)
    diff=changes(accepted,derived,signal);diff['pv_variant']=pv
    (folder/'model_changes.json').write_text(json.dumps(diff,indent=1,default=str)+'\n')
    w=MainWindow();w.resize(1700,1000);w._place_project_in_tab(derived);w.show();app.processEvents()
    scene=w._schematic_view.scene()
    render(scene,scene.itemsBoundingRect(),folder/'model_full.png',3600)
    power=[c for c in derived.get_active_circuit().components.values() if c.name not in signal]
    xs=[c.x for c in power];ys=[c.y for c in power]
    render(scene,QRectF(min(xs)-120,min(ys)-160,max(xs)-min(xs)+240,max(ys)-min(ys)+280),folder/'model_power.png',3600)
    print('Snapshot:',folder)


if __name__=='__main__':
    if len(sys.argv)!=2:sys.exit(__doc__)
    main(sys.argv[1])
