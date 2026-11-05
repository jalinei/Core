#!/usr/bin/env python3
## SPDX-License-Identifier: LGPL-2.1
## @file gui.py
## @brief Run/inspect the same Core C-Block and accepted plant in PulsimGUI.
"""No application sequencing or alternate controller implementation lives in the GUI."""
from pathlib import Path
import argparse,json,sys
import numpy as np
from PySide6.QtCore import QTimer
from PySide6.QtWidgets import QApplication
from pulsimgui.services.backend_adapter import BackendLoader,BackendInfo,BackendRunResult,PlaceholderBackend
from pulsimgui.models.project import Project
from runtime import HERE,CONTRACT
from app_constants import STARTUP_RAMP_RATE
from runner import run,save
from plant.create_gui import create

INFO=BackendInfo(identifier='core-controlstep',name='Core controlStep C-Block',version='2.0.0',status='available',capabilities={'transient'},message=f'Actual Core app-v1 / {STARTUP_RAMP_RATE:g} V/s / synchronization only')
class Backend(PlaceholderBackend):
    """GUI adapter delegates execution to the same runner as command-line checks."""
    def __init__(self):super().__init__(INFO)
    def run_transient(self,data,settings,callbacks):
        try:
            # Reconstruct the editable GUI project without parsing a separate copy.
            project=Project(name='GUI input',circuits={'main':__import__('pulsimgui.models.circuit',fromlist=['Circuit']).Circuit.from_dict(data)})
            constants={p.name:p.parameters['value'] for p in project.get_active_circuit().components.values() if p.type.name=='CONSTANT'}
            command={'time_s':0,**{n:constants['Scenario_'+n] for n in ['vac_updated','mode','sync_request','v_bus_ref']}}
            block=next(p for p in project.get_active_circuit().components.values() if p.name=='Core_controlStep')
            timeline=block.parameters.get('scenario_timeline') or [command]
            timeline=[{**timeline[0],**command},*timeline[1:]]
            scenario={'name':block.parameters.get('scenario_name','gui-core-app'),'duration_s':settings.t_stop,'dt_s':settings.t_step,'store_period_s':min(.0001,settings.t_stop/2000),'commands':timeline}
            def progress(n,msg):callbacks.wait_if_paused();callbacks.progress(n,msg)
            result=run(scenario,project,progress,callbacks.check_cancelled)
            save(result,HERE.parent/('out/l1-gui-pv' if result[3]['pv_model_available'] else 'out/l1-gui-dcdc' if 'fixture' in result[3] else 'out/l1-gui'))
            t,signals,rows,metadata=result
            return BackendRunResult(time=t.tolist(),signals={k:v.tolist() for k,v in signals.items()},statistics=metadata)
        except Exception as exc:return BackendRunResult(error_message=str(exc))

def main():
    """Open the derived signal diagram, and optionally check its actual Run path."""
    parser=argparse.ArgumentParser()
    parser.add_argument('--pv',action='store_true',help='Synthetic PV source, bus load and actual Core MPPT')
    parser.add_argument('--results',type=Path,help='Load waveforms.npz and manifest.json from this result folder')
    parser.add_argument('--scenario',type=Path,help='Open the switched fixture and command timeline defined by this JSON scenario')
    parser.add_argument('--check',action='store_true')
    parser.add_argument('--check-run',action='store_true')
    args=parser.parse_args()
    def discover(self):return {INFO.identifier:BackendLoader._BackendCandidate(info=INFO,factory=Backend)}
    BackendLoader._discover_candidates=discover
    from pulsimgui.views.main_window import MainWindow
    app=QApplication(sys.argv);app.setApplicationName('PulsimGui');app.setOrganizationName('Core-L1')
    app.setStyle('Fusion');window=MainWindow();window.resize(1700,1000)
    scenario=json.loads(args.scenario.read_text()) if args.scenario else None
    if args.results and scenario is None:
        saved=json.loads((args.results/'manifest.json').read_text())
        if 'fixture' in saved:scenario={**saved['scenario'],'fixture':saved['fixture']}
    project=Project.load(create(pv=args.pv,scenario=scenario))
    if '--check-run' in sys.argv:project.simulation_settings.tstop=.002
    window._place_project_in_tab(project);window.library_dock.hide();window.show()
    def ready():
        window._schematic_view.zoom_to_fit()
        window.statusBar().showMessage('Native Core controlStep C-Block. Mode 3 = MPPT; all active boost modes require a timestep <= 50 ns.')
        if args.results:
            from pulsimgui.services.simulation_service import SimulationResult
            meta=json.loads((args.results/'manifest.json').read_text())
            from plant.pv import parameters
            if bool(meta['pv_model_available'])!=args.pv:raise ValueError('Use --pv for PV results')
            if args.pv and parameters(project.get_active_circuit())!=meta['pv_parameters']:
                raise ValueError('Saved PV parameters differ from the current profile')
            with np.load(args.results/'waveforms.npz',allow_pickle=False) as data:
                result=SimulationResult(time=data['time'].tolist(),signals={n:data[n].tolist() for n in data.files if n!='time'},statistics=meta)
            window._on_simulation_finished(result);window.waveform_dock.show()
            viewer=window._waveform_viewer
            viewer.clear_traces()
            for n in list(viewer._signal_list_panel.get_visible_signals()):viewer._signal_list_panel.set_signal_visible(n,False)
            selected=['pv_power','pv_mpp_power'] if args.pv else ['VDCBus','startup_ref'] if 'fixture' in meta and meta['scenario']['commands'][0]['mode']==1 else ['VDCBus'] if 'fixture' in meta else ['VAC']
            for n in selected:
                viewer.add_trace(n)
                viewer._signal_list_panel.set_signal_visible(n,True)
            app.processEvents();viewer._auto_range()
            viewer._plot_widget.setXRange(result.time[0],result.time[-1],padding=.01)
            window._schematic_view.zoom_to_fit()
            print('Loaded saved results:',args.results,flush=True)
        if '--check-run' in sys.argv:
            def finished(r):
                assert r.is_valid,r.error_message
                assert r.statistics['native_cblock'] and r.statistics['profile']==CONTRACT['application_profile']
                if args.pv:assert 'pv_power' in r.signals and r.statistics['pv_model_available']
                print('GUI Run executed the actual Core C-Block:',len(r.time),'points',flush=True)
                QTimer.singleShot(50,app.quit)
            window._simulation_service.simulation_finished.connect(finished)
            window._on_run_simulation()
        elif '--check' in sys.argv:
            assert window.action_run.isEnabled()
            assert any(p.name=='Core_controlStep' for p in window._project.get_active_circuit().components.values())
            out=HERE.parent/('out/l1-gui-pv' if args.pv else 'out/l1-gui');out.mkdir(parents=True,exist_ok=True);window.grab().save(str(out/'core_cblock.png'))
            print('GUI native firmware-block view loaded.',flush=True);QTimer.singleShot(50,app.quit)
    QTimer.singleShot(1000,ready)
    sys.exit(app.exec())
if __name__=='__main__':main()
