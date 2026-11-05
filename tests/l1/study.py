#!/usr/bin/env python3
## SPDX-License-Identifier: LGPL-2.1
## @file study.py
## @brief Run a study: many exploratory scenario runs in parallel, one review page updated as they finish.
## @par Usage
##   .venv/bin/python tests/l1/study.py tests/l1/studies/<study>.json [--jobs N] [--out DIR]
##   tests/l1/study.sh tests/l1/studies/<study>.json     # detached, survives logout, keeps the machine awake
## @par Study file
##   {"name", "title", "purpose", "cases": [{"folder", "base", "set", "figures"}]}
##   - base: a file in tests/l1/scenarios/; set: keys replacing the base's (title, purpose, commands,
##     duration_s, fixture, grid, light, pv_model, open_loop_duty...); figures: review-plot specs
##     (tests/l1/review_plots.json format, without "scenario").
## @par Output (tests/out/study_<name>_<date>_<time>/)
##   status.json (state of every case), <folder>/ (run.log, scenario.json, figures.json, then the
##   runner's control.csv, waveforms.npz, manifest.json), report.html (rewritten after each case).
## @return 0 when every case finished, 1 otherwise. Studies give no PASS/FAIL verdicts.
"""Exploratory runs only: verdicts belong to the campaign (tests/run_all.sh)."""
from pathlib import Path
from concurrent.futures import ThreadPoolExecutor
import argparse,datetime as dt,json,os,subprocess,sys,threading,time

HERE=Path(__file__).resolve().parent
REPO=HERE.parents[1]
PY=sys.executable
# Measured cost per 50 ns step on Luiz's laptop (2026-11-06), used only to order and estimate runs.
COST_S_PER_STEP={'ideal':95e-6,'lambertw':150e-6,'diode':680e-6}


def estimate(sc):
    """@brief Rough wall time of one run, s (alone on the machine)."""
    kind=sc.get('pv_model','diode') if sc.get('plant_variant')=='pv' else 'ideal'
    return sc['duration_s']/sc['dt_s']*COST_S_PER_STEP[kind]


def build_cases(study,out):
    """@brief Write each case's scenario.json and figures.json. @return list of case dicts."""
    cases=[]
    for c in study['cases']:
        sc=json.loads((HERE/'scenarios'/c['base']).read_text())
        sc.update(c.get('set',{}))
        sc.setdefault('criteria',[]);sc['study']=study['name'];sc['name']=c['folder']
        folder=out/c['folder'];folder.mkdir(parents=True,exist_ok=True)
        (folder/'scenario.json').write_text(json.dumps(sc,indent=2)+'\n')
        (folder/'figures.json').write_text(json.dumps(c.get('figures',[]),indent=2)+'\n')
        cases.append({'folder':c['folder'],'title':sc.get('title',c['folder']),'state':'queued',
                      'simulated_s':sc['duration_s'],'estimate_s':round(estimate(sc))})
    return cases


def main():
    a=argparse.ArgumentParser();a.add_argument('study',type=Path)
    a.add_argument('--jobs',type=int,default=os.cpu_count());a.add_argument('--out',type=Path)
    args=a.parse_args()
    study=json.loads(args.study.read_text())
    stamp=dt.datetime.now().strftime('%Y-%m-%d_%H%M')
    out=args.out or REPO/'tests/out'/f'study_{study["name"]}_{stamp}';out.mkdir(parents=True,exist_ok=True)
    subprocess.run(['bash',str(HERE/'firmware_block/build.sh')],check=True)
    status={'study':study,'started':dt.datetime.now().isoformat(timespec='seconds'),'jobs':args.jobs,
            'cases':build_cases(study,out)}
    lock=threading.Lock();report_lock=threading.Lock()
    def save():
        (out/'status.json').write_text(json.dumps(status,indent=2)+'\n')
    def report():
        r=subprocess.run([PY,str(REPO/'tests/common/review_report.py'),str(out)],capture_output=True,text=True)
        if r.returncode:print('report failed:',r.stderr.strip().splitlines()[-1:],flush=True)
    def run(case):
        folder=out/case['folder']
        with lock:case.update(state='running',started=dt.datetime.now().isoformat(timespec='seconds'));save()
        t0=time.monotonic()
        with (folder/'run.log').open('w') as log:
            r=subprocess.run([PY,str(HERE/'runner.py'),'--scenario',str(folder/'scenario.json'),'--out',str(folder)],
                             cwd=HERE,stdout=log,stderr=subprocess.STDOUT)
        with lock:
            case.update(state='done' if r.returncode==0 else 'failed',wall_s=round(time.monotonic()-t0),
                        finished=dt.datetime.now().isoformat(timespec='seconds'))
            if r.returncode:case['error']=((folder/'run.log').read_text().strip().splitlines() or ['?'])[-1][:300]
            save()
        with report_lock:report()
        print(f"{case['folder']}: {case['state']} after {case['wall_s']} s",flush=True)
    save();report()
    # Longest first, so the night ends with short runs filling the cores.
    order=sorted(status['cases'],key=lambda c:-c['estimate_s'])
    print(f'{len(order)} cases on {args.jobs} jobs -> {out}',flush=True)
    with ThreadPoolExecutor(max_workers=args.jobs) as pool:list(pool.map(run,order))
    status['finished']=dt.datetime.now().isoformat(timespec='seconds');save();report()
    return 0 if all(c['state']=='done' for c in status['cases']) else 1


if __name__=='__main__':sys.exit(main())
